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
 *   - renamed to another format of a different size, it is refused.
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
#include "Pokemon/Pokemon.h"
#include "Enums/GameVersion.h"

// Logging, and the one filesystem read PokemonFile needs, for the host.
namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logEventToFile(const std::string&) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
    uint8_t* readAllBytes(const char* path, size_t* outSize) {
        if (outSize) *outSize = 0;
        FILE* file = std::fopen(path, "rb");
        if (!file) return nullptr;
        std::fseek(file, 0, SEEK_END);
        const long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        uint8_t* bytes = static_cast<uint8_t*>(std::malloc(length > 0 ? (size_t)length : 1));
        const size_t got = (bytes && length > 0) ? std::fread(bytes, 1, (size_t)length, file) : 0;
        std::fclose(file);
        if (!bytes) return nullptr;
        if (outSize) *outSize = got;
        return bytes;
    }
}

namespace {

int g_checks = 0;
int g_failures = 0;

void fail(const std::string& file, const std::string& what) {
    std::printf("  [FAIL] %s\n         %s\n", what.c_str(), file.c_str());
    ++g_failures;
}

/// Files PKSE refuses on purpose, each with why. Matched against the end of the path.
const struct { const char* fileName; const char* reason; } KNOWN_REFUSALS[] = {
    { "235 - \xEC\x95\x8C - 10DB.pk2", "Korean Gold/Silver, which PKSE does not support" },
};

const char* knownRefusal(const std::string& path) {
    for (const auto& refusal : KNOWN_REFUSALS) {
        const std::string name = refusal.fileName;
        if (path.size() >= name.size() && path.compare(path.size() - name.size(), name.size(), name) == 0)
            return refusal.reason;
    }
    return nullptr;
}

bool isEntityExtension(const std::string& name) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string extension = name.substr(dot);
    for (const char* known : { ".pk1", ".pk2", ".pk3", ".pk4", ".pk5", ".pk6", ".pk7", ".pk8", ".pk9",
                               ".pb7", ".pb8", ".pa8", ".pa9" })
        if (extension == known) return true;
    return false;
}

void collect(const std::string& directory, std::vector<std::string>& out) {
    DIR* listing = opendir(directory.c_str());
    if (!listing) return;
    while (dirent* entry = readdir(listing)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = directory + "/" + name;
        struct stat status{};
        if (stat(path.c_str(), &status) != 0) continue;
        if (S_ISDIR(status.st_mode)) collect(path, out);
        else if (isEntityExtension(name)) out.push_back(path);
    }
    closedir(listing);
}

std::vector<std::byte> readFile(const std::string& path) {
    std::vector<std::byte> bytes;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return bytes;
    std::byte buffer[4096];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0) bytes.insert(bytes.end(), buffer, buffer + count);
    std::fclose(file);
    return bytes;
}

/// The species number PKHeX's file name starts with, or 0 when it does not start with one. A few
/// names start with a date ("2026.06.08 - 0502 - Dewott ..."); the species is the next field then.
int speciesFromFileName(const std::string& path) {
    std::string name = path.substr(path.rfind('/') + 1);
    auto leadingNumber = [](const std::string& text, size_t& length) {
        int value = 0;
        length = 0;
        while (length < text.size() && std::isdigit(static_cast<unsigned char>(text[length])))
            value = value * 10 + (text[length++] - '0');
        return value;
    };
    size_t length = 0;
    int species = leadingNumber(name, length);
    if (length > 0 && length < name.size() && name[length] == '.') {
        const size_t separator = name.find(" - ");
        if (separator == std::string::npos) return 0;
        name = name.substr(separator + 3);
        species = leadingNumber(name, length);
    }
    return length > 0 ? species : 0;
}

/// Where a file's party battle-stat tail sits: from the end of the box record to the end of the
/// party record (PKHeX's sizes), offset by the list header Gens 1 and 2 put in front. Empty when the
/// file has no tail.
struct ByteRange { size_t begin, end; };
ByteRange partyTailFor(const std::string& extension, size_t fileSize) {
    if (extension == ".pk1") return { 3 + 0x21, 3 + 0x2C };
    if (extension == ".pk2") return { 3 + 0x20, 3 + 0x30 };
    struct Sizes { const char* extension; size_t box, party; };
    for (const Sizes& sizes : { Sizes{ ".pk3", 80, 100 },   Sizes{ ".pk4", 136, 236 }, Sizes{ ".pk5", 136, 220 },
                                Sizes{ ".pk6", 232, 260 },  Sizes{ ".pk7", 232, 260 }, Sizes{ ".pb7", 232, 260 },
                                Sizes{ ".pk8", 0x148, 0x158 }, Sizes{ ".pb8", 0x148, 0x158 },
                                Sizes{ ".pk9", 0x148, 0x158 }, Sizes{ ".pa8", 0x168, 0x178 } })
        if (extension == sizes.extension && fileSize >= sizes.party) return { sizes.box, sizes.party };
    return { 0, 0 };
}

void checkFile(const std::string& path) {
    const std::vector<std::byte> bytes = readFile(path);
    const std::string fileName = path.substr(path.rfind('/') + 1);
    const char* refusal = knownRefusal(path);

    ++g_checks;
    Trainer::PokemonFile::LoadResult loaded =
        Trainer::PokemonFile::parse(std::span<const std::byte>(bytes.data(), bytes.size()), fileName);
    if (refusal) {
        if (loaded) fail(path, std::string("a file PKSE is listed as refusing opened (") + refusal + ")");
        return;
    }
    if (!loaded) {
        fail(path, "a PKHeX file did not open: " + loaded.error);
        return;
    }

    const int expectedSpecies = speciesFromFileName(path);
    if (expectedSpecies) {
        ++g_checks;
        if (loaded.pokemon->speciesID() != expectedSpecies)
            fail(path, "opened as species " + std::to_string(loaded.pokemon->speciesID()) + ", the file name says " +
                           std::to_string(expectedSpecies));
    }

    // Written back, PKHeX's bytes again over the length of PKHeX's file. PKSE may write a party
    // record where PKHeX wrote a box one; what both wrote must agree.
    std::string error;
    const std::vector<std::byte> written = Trainer::PokemonFile::serialize(*loaded.pokemon, &error);
    ++g_checks;
    if (written.size() < bytes.size()) {
        fail(path, "written back shorter than PKHeX's file (" + std::to_string(written.size()) + " < " +
                       std::to_string(bytes.size()) + ") " + error);
    } else {
        const ByteRange tail = partyTailFor(fileName.substr(fileName.rfind('.')), bytes.size());
        bool tailWasEmpty = tail.end > tail.begin;
        for (size_t index = tail.begin; index < tail.end && index < bytes.size(); ++index)
            tailWasEmpty = tailWasEmpty && bytes[index] == std::byte{0};
        size_t differing = 0, first = 0;
        for (size_t index = 0; index < bytes.size(); ++index) {
            if (written[index] == bytes[index]) continue;
            if (tailWasEmpty && index >= tail.begin && index < tail.end) continue; // PKSE filled the stats
            if (!differing) first = index;
            ++differing;
        }
        if (differing) {
            char detail[96];
            std::snprintf(detail, sizeof detail, "%zu byte(s) differ, first at 0x%zX", differing, first);
            fail(path, std::string("written back, the file is not PKHeX's bytes: ") + detail);
        }
    }

    // Another format of a different size, by name only. Gen 3's file is the smallest from Gen 3 on,
    // so a Gen 3 file pretends to be Gen 9 and everything else pretends to be Gen 3.
    const std::string extension = fileName.substr(fileName.rfind('.'));
    const std::string wrongExtension = extension == ".pk3" ? ".pk9" : ".pk3";
    ++g_checks;
    Trainer::PokemonFile::LoadResult renamed = Trainer::PokemonFile::parse(
        std::span<const std::byte>(bytes.data(), bytes.size()), fileName.substr(0, fileName.rfind('.')) + wrongExtension);
    if (renamed) fail(path, "accepted when renamed to " + wrongExtension);
}

} // namespace

int main(int argc, char** argv) {
    const char* home = std::getenv("HOME");
    const std::string root = argc > 1 ? argv[1] : std::string(home ? home : ".") + "/repos/PKHeX/Tests";
    std::printf("Pokemon files against PKHeX's test files\n  root: %s\n\n", root.c_str());

    std::vector<std::string> files;
    collect(root, files);
    if (files.empty()) {
        std::printf("  [FAIL] no PKHeX entity files found under %s\n", root.c_str());
        return 1;
    }
    for (const std::string& path : files) checkFile(path);

    std::printf("\n%zu files, %d checks, %d failure(s)\n", files.size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
