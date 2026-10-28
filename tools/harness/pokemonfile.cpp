/*
 * Pokemon files against PKHeX's own: every .pkX / .pbX / .paX file in PKHeX's test tree.
 *
 * PKHeX wrote these files, so they are the reference, and each one is checked for:
 *
 *   - it opens (except the files listed in KNOWN_REFUSALS, each with the reason PKSE refuses it);
 *   - it opens as the species its file name starts with -- PKHeX names its test files
 *     "<species> - <name> - <id>", so the number is an expectation that did not come from PKSE;
 *   - written back, it is PKHeX's bytes again, over the length of PKHeX's file -- except that PKSE
 *     fills a party record's battle-stat tail where PKHeX's file left it all zero (PKSE opens a
 *     Pokemon with its level and stats in place; PKHeX's test files often carry none);
 *   - encrypted, under PKHeX's encrypted name (.ek3 ... .ea9), it opens as the same Pokemon;
 *   - renamed to another format of a different size, it is refused.
 *
 * A directory of PKHeX-ENCRYPTED files (.ek3 ... .ea9) is read too: each must open, as the species
 * its name says.
 *
 * Build and run:  tools/harness/build-pokemonfile.sh [PKHeX tests directory]
 *                 (default ~/repos/PKHeX/Tests)
 * Exits non-zero on any failure.
 */
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <span>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "Trainer/PokemonFile.h"
#include "Trainer/Bank.h"
#include "Pokemon/Pokemon.h"
#include "Enums/GameVersion.h"

// Logging, and the one filesystem read PokemonFile needs, for the host.
namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logEventToFile(const std::string &) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
    uint8_t *readAllBytes(const char *path, size_t *outSize)
    {
        if (outSize)
        {
            *outSize = 0;
        }
        FILE *file = std::fopen(path, "rb");
        if (!file)
        {
            return nullptr;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        uint8_t *fileBytes = new uint8_t[fileSize > 0 ? static_cast<size_t>(fileSize) : 1];
        const size_t readByteCount = (fileBytes && fileSize > 0) ? std::fread(fileBytes, 1, static_cast<size_t>(fileSize), file) : 0;
        std::fclose(file);
        if (!fileBytes)
        {
            return nullptr;
        }
        if (outSize)
        {
            *outSize = readByteCount;
        }
        return fileBytes;
    }
}

namespace
{

    int g_checkCount = 0;
    int g_failureCount = 0;

    void fail(const std::string &filePath, const std::string &failureDescription)
    {
        std::printf("  [FAIL] %s\n         %s\n", failureDescription.c_str(), filePath.c_str());
        ++g_failureCount;
    }

    /// Files PKSE refuses on purpose, each with why. Matched against the end of the path.
    const struct
    {
        const char *fileName;
        const char *reason;
    } KNOWN_REFUSALS[] = {
        {"235 - \xEC\x95\x8C - 10DB.pk2", "Korean Gold/Silver, which PKSE does not support"},
    };

    const char *knownRefusal(const std::string &filePath)
    {
        for (const auto &refusal : KNOWN_REFUSALS)
        {
            const std::string refusalFileName = refusal.fileName;
            if (filePath.size() >= refusalFileName.size() && filePath.compare(filePath.size() - refusalFileName.size(), refusalFileName.size(), refusalFileName) == 0)
            {
                return refusal.reason;
            }
        }
        return nullptr;
    }

    bool isEntityExtension(const std::string &fileName)
    {
        const size_t dotIndex = fileName.rfind('.');
        if (dotIndex == std::string::npos)
        {
            return false;
        }
        const std::string extension = fileName.substr(dotIndex);
        // clang-format off
        for (const char *knownExtension : {".pk1", ".pk2", ".pk3", ".pk4", ".pk5", ".pk6", ".pk7", ".pk8", ".pk9",
                                           ".pb7", ".pb8", ".pa8", ".pa9", ".ek3", ".ek4", ".ek5", ".ek6", ".ek7",
                                           ".ek8", ".ek9", ".eb7", ".eb8", ".ea8", ".ea9"})
        // clang-format on
        {
            if (extension == knownExtension)
            {
                return true;
            }
        }
        return false;
    }

    void collect(const std::string &directory, std::vector<std::string> &entityFilePaths)
    {
        DIR *directoryStream = opendir(directory.c_str());
        if (!directoryStream)
        {
            return;
        }
        while (dirent *entry = readdir(directoryStream))
        {
            const std::string entryName = entry->d_name;
            if (entryName == "." || entryName == "..")
            {
                continue;
            }
            const std::string entryPath = directory + "/" + entryName;
            struct stat entryStatus{};
            if (stat(entryPath.c_str(), &entryStatus) != 0)
            {
                continue;
            }
            if (S_ISDIR(entryStatus.st_mode))
            {
                collect(entryPath, entityFilePaths);
            }
            else if (isEntityExtension(entryName))
            {
                entityFilePaths.push_back(entryPath);
            }
        }
        closedir(directoryStream);
    }

    std::vector<std::byte> readFile(const std::string &filePath)
    {
        std::vector<std::byte> fileBytes;
        FILE *file = std::fopen(filePath.c_str(), "rb");
        if (!file)
        {
            return fileBytes;
        }
        std::byte chunkBuffer[4096];
        size_t readByteCount;
        while ((readByteCount = std::fread(chunkBuffer, 1, sizeof chunkBuffer, file)) > 0)
        {
            fileBytes.insert(fileBytes.end(), chunkBuffer, chunkBuffer + readByteCount);
        }
        std::fclose(file);
        return fileBytes;
    }

    /// The species number PKHeX's file name starts with, or 0 when it does not start with one. A few
    /// names start with a date ("2026.06.08 - 0502 - Dewott ..."); the species is the next field then.
    int speciesFromFileName(const std::string &filePath)
    {
        std::string fileName = filePath.substr(filePath.rfind('/') + 1);
        auto leadingNumber = [](const std::string &fieldText, size_t &digitCount)
        {
            int leadingValue = 0;
            digitCount = 0;
            while (digitCount < fieldText.size() && std::isdigit(static_cast<unsigned char>(fieldText[digitCount])))
            {
                leadingValue = leadingValue * 10 + (fieldText[digitCount++] - '0');
            }
            return leadingValue;
        };
        size_t digitCount = 0;
        int speciesId = leadingNumber(fileName, digitCount);
        if (digitCount > 0 && digitCount < fileName.size() && fileName[digitCount] == '.')
        {
            const size_t separatorIndex = fileName.find(" - ");
            if (separatorIndex == std::string::npos)
            {
                return 0;
            }
            fileName = fileName.substr(separatorIndex + 3);
            speciesId = leadingNumber(fileName, digitCount);
        }
        return digitCount > 0 ? speciesId : 0;
    }

    /// Where a file's party battle-stat tail sits: from the end of the box record to the end of the
    /// party record (PKHeX's sizes), offset by the list header Gens 1 and 2 put in front. Empty when the
    /// file has no tail.
    struct ByteRange
    {
        size_t begin;
        size_t end;
    };
    ByteRange partyTailFor(const std::string &extension, size_t fileSize)
    {
        if (extension == ".pk1")
        {
            return {3 + 0x21, 3 + 0x2C};
        }
        if (extension == ".pk2")
        {
            return {3 + 0x20, 3 + 0x30};
        }
        struct RecordSizes
        {
            const char *extension;
            size_t boxSize;
            size_t partySize;
        };
        // clang-format off
        for (const RecordSizes &recordSizes : {RecordSizes{".pk3", 80, 100}, RecordSizes{".pk4", 136, 236}, RecordSizes{".pk5", 136, 220},
                                               RecordSizes{".pk6", 232, 260}, RecordSizes{".pk7", 232, 260}, RecordSizes{".pb7", 232, 260},
                                               RecordSizes{".pk8", 0x148, 0x158}, RecordSizes{".pb8", 0x148, 0x158},
                                               RecordSizes{".pk9", 0x148, 0x158}, RecordSizes{".pa8", 0x168, 0x178}})
        // clang-format on
        {
            if (extension == recordSizes.extension && fileSize >= recordSizes.partySize)
            {
                return {recordSizes.boxSize, recordSizes.partySize};
            }
        }
        return {0, 0};
    }

    void checkFile(const std::string &filePath)
    {
        const std::vector<std::byte> fileBytes = readFile(filePath);
        const std::string fileName = filePath.substr(filePath.rfind('/') + 1);
        const char *refusalReason = knownRefusal(filePath);

        ++g_checkCount;
        Trainer::PokemonFile::LoadResult loaded = Trainer::PokemonFile::parse(std::span<const std::byte>(fileBytes.data(), fileBytes.size()), fileName);
        if (refusalReason)
        {
            if (loaded)
            {
                fail(filePath, std::string("a file PKSE is listed as refusing opened (") + refusalReason + ")");
            }
            return;
        }
        if (!loaded)
        {
            fail(filePath, "a PKHeX file did not open: " + loaded.error);
            return;
        }

        const int expectedSpeciesId = speciesFromFileName(filePath);
        if (expectedSpeciesId)
        {
            ++g_checkCount;
            if (loaded.pokemon->speciesID() != expectedSpeciesId)
            {
                fail(filePath, "opened as species " + std::to_string(loaded.pokemon->speciesID()) + ", the file name says " + std::to_string(expectedSpeciesId));
            }
        }

        // A file PKHeX saved ENCRYPTED (.ekX) has nothing more to check here: PKSE writes the plain
        // form, which the plain files cover.
        if (fileName[fileName.rfind('.') + 1] == 'e')
        {
            return;
        }

        // Written back, PKHeX's bytes again over the length of PKHeX's file. PKSE may write a party
        // record where PKHeX wrote a box one; what both wrote must agree.
        std::string serializeError;
        const std::vector<std::byte> writtenBytes = Trainer::PokemonFile::serialize(*loaded.pokemon, &serializeError);
        ++g_checkCount;
        if (writtenBytes.size() < fileBytes.size())
        {
            fail(filePath, "written back shorter than PKHeX's file (" + std::to_string(writtenBytes.size()) + " < " + std::to_string(fileBytes.size()) + ") " + serializeError);
        }
        else
        {
            const ByteRange partyTail = partyTailFor(fileName.substr(fileName.rfind('.')), fileBytes.size());
            bool tailWasEmpty = partyTail.end > partyTail.begin;
            for (size_t byteIndex = partyTail.begin; byteIndex < partyTail.end && byteIndex < fileBytes.size(); ++byteIndex)
            {
                tailWasEmpty = tailWasEmpty && fileBytes[byteIndex] == std::byte{0};
            }
            size_t differingByteCount = 0;
            size_t firstDifferenceOffset = 0;
            for (size_t byteIndex = 0; byteIndex < fileBytes.size(); ++byteIndex)
            {
                if (writtenBytes[byteIndex] == fileBytes[byteIndex])
                {
                    continue;
                }
                if (tailWasEmpty && byteIndex >= partyTail.begin && byteIndex < partyTail.end)
                {
                    continue; // PKSE filled the stats
                }
                if (!differingByteCount)
                {
                    firstDifferenceOffset = byteIndex;
                }
                ++differingByteCount;
            }
            if (differingByteCount)
            {
                char differenceDetail[96];
                std::snprintf(differenceDetail, sizeof differenceDetail, "%zu byte(s) differ, first at 0x%zX", differingByteCount, firstDifferenceOffset);
                fail(filePath, std::string("written back, the file is not PKHeX's bytes: ") + differenceDetail);
            }
        }

        // Another format of a different size, by name only. Gen 3's file is the smallest from Gen 3 on,
        // so a Gen 3 file pretends to be Gen 9 and everything else pretends to be Gen 3.
        const std::string fileExtension = fileName.substr(fileName.rfind('.'));
        // The same record encrypted, under PKHeX's encrypted name (.pk3 -> .ek3 ...): PKHeX's
        // "save as encrypted" form, which opens as the same Pokemon. Gens 1 and 2 have no encryption.
        if (fileExtension != ".pk1" && fileExtension != ".pk2" && fileBytes.size() >= 4)
        {
            std::string encryptedExtension = fileExtension;
            encryptedExtension[1] = 'e';
            const uint32_t encryptionKey = static_cast<uint32_t>(fileBytes[0]) | (static_cast<uint32_t>(fileBytes[1]) << 8) | (static_cast<uint32_t>(fileBytes[2]) << 16) | (static_cast<uint32_t>(fileBytes[3]) << 24);
            std::byte *encryptedBytes = Trainer::Bank::encryptRecord(Trainer::Bank::groupAsBanked(loaded.pokemon->getGameGroup()), std::span<const std::byte>(fileBytes.data(), fileBytes.size()), encryptionKey);
            ++g_checkCount;
            if (!encryptedBytes)
            {
                fail(filePath, "could not encrypt the record for the " + encryptedExtension + " check");
            }
            else
            {
                Trainer::PokemonFile::LoadResult encryptedLoaded = Trainer::PokemonFile::parse(std::span<const std::byte>(encryptedBytes, fileBytes.size()), fileName.substr(0, fileName.rfind('.')) + encryptedExtension);
                delete[] encryptedBytes;
                if (!encryptedLoaded)
                {
                    fail(filePath, "encrypted as " + encryptedExtension + ", it did not open: " + encryptedLoaded.error);
                }
                else if (encryptedLoaded.pokemon->speciesID() != loaded.pokemon->speciesID() || encryptedLoaded.pokemon->pid() != loaded.pokemon->pid())
                {
                    fail(filePath, "encrypted as " + encryptedExtension + ", it opened as a different Pokemon");
                }
            }
        }

        const std::string wrongExtension = fileExtension == ".pk3" ? ".pk9" : ".pk3";
        ++g_checkCount;
        Trainer::PokemonFile::LoadResult renamedLoaded = Trainer::PokemonFile::parse(std::span<const std::byte>(fileBytes.data(), fileBytes.size()), fileName.substr(0, fileName.rfind('.')) + wrongExtension);
        if (renamedLoaded)
        {
            fail(filePath, "accepted when renamed to " + wrongExtension);
        }
    }

} // namespace

int main(int argc, char **argv)
{
    const char *homeDirectory = std::getenv("HOME");
    const std::string testsRoot = argc > 1 ? argv[1] : std::string(homeDirectory ? homeDirectory : ".") + "/repos/PKHeX/Tests";
    std::printf("Pokemon files against PKHeX's test files\n  root: %s\n\n", testsRoot.c_str());

    std::vector<std::string> entityFilePaths;
    collect(testsRoot, entityFilePaths);
    if (entityFilePaths.empty())
    {
        std::printf("  [FAIL] no PKHeX entity files found under %s\n", testsRoot.c_str());
        return 1;
    }
    for (const std::string &filePath : entityFilePaths)
    {
        checkFile(filePath);
    }

    std::printf("\n%zu files, %d checks, %d failure(s)\n", entityFilePaths.size(), g_checkCount, g_failureCount);
    return g_failureCount == 0 ? 0 : 1;
}
