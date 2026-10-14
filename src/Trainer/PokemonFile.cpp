#include "Trainer/PokemonFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "Encryption/Encryption3FRLG.h"
#include "Encryption/Encryption4HGSS.h"
#include "Encryption/Encryption5B2W2.h"
#include "Encryption/Encryption6ORAS.h"
#include "Encryption/Encryption7LGPE.h"
#include "Encryption/Encryption7USUM.h"
#include "Encryption/Encryption8BDSP.h"
#include "Encryption/Encryption8LA.h"
#include "Encryption/Encryption8SWSH.h"
#include "Encryption/Encryption9LZA.h"
#include "Encryption/Encryption9SV.h"
#include "Enums/GameVersion.h"
#include "Globals.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Pokemon/Pokemon4HGSS.h"
#include "Pokemon/Pokemon5B2W2.h"
#include "Pokemon/Pokemon6ORAS.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Pokemon8BDSP.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon9SV.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Pokemon/Experience.h"
#include "Trainer/Bank.h"
#include "Utils/FileUtilities.h"

namespace Trainer::PokemonFile
{
    namespace
    {
        using Enums::GameVersion;

        struct FormatSpec
        {
            const char *extension;
            GameVersion group;
            size_t shortSize;
            size_t longSize;
        };

        // Gen 1/2 use the two sizes for Japanese/international single-entry lists. Later formats use
        // them for stored/party records. The extension chooses the entity format; the length chooses
        // the valid representation inside that format.
        constexpr FormatSpec FORMAT_SPECS[] = {
            {".pk1", GameVersion::RBY, Pokemon::SIZE_1JLIST, Pokemon::SIZE_1ULIST},
            {".pk2", GameVersion::GSC, Pokemon::SIZE_2JLIST, Pokemon::SIZE_2ULIST},
            {".pk3", GameVersion::FRLG, Encryption::SIZE_STORED3_FRLG, Encryption::SIZE_PARTY3_FRLG},
            {".pk4", GameVersion::HGSS, Encryption::SIZE_STORED4_HGSS, Encryption::SIZE_PARTY4_HGSS},
            {".pk5", GameVersion::B2W2, Encryption::SIZE_STORED5_B2W2, Encryption::SIZE_PARTY5_B2W2},
            {".pk6", GameVersion::ORAS, Encryption::SIZE_STORED6_ORAS, Encryption::SIZE_PARTY6_ORAS},
            {".pk7", GameVersion::USUM, Encryption::SIZE_STORED7_USUM, Encryption::SIZE_PARTY7_USUM},
            {".pb7", GameVersion::GG, Encryption::SIZE_STORED7_LGPE, Encryption::SIZE_PARTY7_LGPE},
            {".pk8", GameVersion::SWSH, Encryption::SIZE_STORED8_SWSH, Encryption::SIZE_PARTY8_SWSH},
            {".pb8", GameVersion::BDSP, Encryption::SIZE_STORED8_BDSP, Encryption::SIZE_PARTY8_BDSP},
            {".pa8", GameVersion::PLA, Encryption::SIZE_STORED8_LA, Encryption::SIZE_PARTY8_LA},
            {".pk9", GameVersion::SV, Encryption::SIZE_STORED9_SV, Encryption::SIZE_PARTY9_SV},
            {".pa9", GameVersion::ZA, Encryption::SIZE_STORED9_LZA, Encryption::SIZE_PARTY9_LZA},
        };

        std::string lowerExtension(const std::string &path)
        {
            const size_t slash = path.find_last_of("/\\");
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                return {};

            std::string result = path.substr(dot);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return result;
        }

        const FormatSpec *specForExtension(const std::string &extension)
        {
            for (const FormatSpec &spec : FORMAT_SPECS)
                if (extension == spec.extension)
                    return &spec;
            return nullptr;
        }

        bool sizeAllowed(const FormatSpec &spec, size_t byteCount) noexcept
        {
            return byteCount == spec.shortSize || byteCount == spec.longSize;
        }

        bool fileExists(const std::string &path)
        {
            struct stat info{};
            return stat(path.c_str(), &info) == 0;
        }


        // Constructors consume encrypted records. File extensions identify plaintext, not cipher text.
        std::byte *encryptBankRecord(GameVersion group, std::span<const std::byte> data,
                                     uint32_t encryptionConstant);

        uint32_t encryptionSeed(std::span<const std::byte> record)
        {
            return static_cast<uint32_t>(static_cast<uint8_t>(record[0])) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(record[1])) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(record[2])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(record[3])) << 24);
        }

        bool validGen12Header(std::span<const std::byte> record, GameVersion group)
        {
            const auto octet = [&](size_t index) { return static_cast<uint8_t>(record[index]); };
            if (octet(0) != 1 || octet(2) != 0xFF)
                return false;
            if (group == GameVersion::GSC)
            {
                const uint8_t species = octet(3);
                if (species == 0 || (octet(1) != species && octet(1) != 0xFD))
                    return false;
                const uint8_t level = octet(3 + 0x1F);
                if (level < 1 || level > 100)
                    return false;
                // Korean G/S uses a distinct charset which this editor cannot decode.
                if (record.size() == Pokemon::SIZE_2ULIST && octet(51) <= 0x0B)
                    return false;
            }
            else
            {
                if (octet(1) == 0 || octet(1) == 0xFF || octet(1) != octet(3))
                    return false;
            }
            return true;
        }

        bool matchesGameFormat(const Pokemon::Pokemon &pokemon, GameVersion group)
        {
            const uint8_t origin = pokemon.originGame();
            if (group == GameVersion::GG)
                return origin == static_cast<uint8_t>(GameVersion::GP) ||
                       origin == static_cast<uint8_t>(GameVersion::GE);
            if (group == GameVersion::USUM)
                return origin != static_cast<uint8_t>(GameVersion::GP) &&
                       origin != static_cast<uint8_t>(GameVersion::GE);
            return true;
        }

        std::unique_ptr<Pokemon::Pokemon> parseUnchecked(std::span<const std::byte> bytes,
                                                         const std::string &fileName,
                                                         std::string *error)
        {
            const std::string extension = lowerExtension(fileName);
            const FormatSpec *spec = specForExtension(extension);
            if (!spec)
            {
                if (error)
                    *error = extension.empty() ? "Pokemon file has no supported extension"
                                               : "unsupported Pokemon file extension: " + extension;
                return nullptr;
            }
            if (!sizeAllowed(*spec, bytes.size()))
            {
                if (error)
                {
                    *error = "wrong byte size for " + extension + ": expected " +
                             std::to_string(spec->shortSize);
                    if (spec->longSize != spec->shortSize)
                        *error += " or " + std::to_string(spec->longSize);
                    *error += ", got " + std::to_string(bytes.size());
                }
                return nullptr;
            }

            if ((spec->group == GameVersion::RBY || spec->group == GameVersion::GSC) &&
                !validGen12Header(bytes, spec->group))
            {
                if (error) *error = "invalid Gen 1/2 list header, level, or unsupported locale";
                return nullptr;
            }

            auto valid = [&](const std::unique_ptr<Pokemon::Pokemon> &candidate) {
                return candidate && candidate->speciesID() != 0 &&
                       candidate->isStructurallyValid() &&
                       matchesGameFormat(*candidate, spec->group) &&
                       (candidate->getDataSize() == bytes.size() ||
                        (spec->group == GameVersion::PLA &&
                         bytes.size() == spec->shortSize &&
                         candidate->getDataSize() == spec->longSize));
            };
            auto pokemon = Bank::makePokemon(spec->group, bytes);
            if (!valid(pokemon) && spec->group != GameVersion::RBY && spec->group != GameVersion::GSC)
            {
                // Standard PKHeX/PKSM .pk* files are decrypted. Retry by encrypting the input
                // into the representation expected by PKSE's existing entity constructors.
                std::byte *encrypted = encryptBankRecord(spec->group, bytes, encryptionSeed(bytes));
                if (encrypted)
                {
                    pokemon = Bank::makePokemon(
                        spec->group, std::span<const std::byte>(encrypted, bytes.size()));
                    delete[] encrypted;
                }
            }
            if (!valid(pokemon))
            {
                if (error) *error = "Pokemon record failed structural/checksum or format validation";
                return nullptr;
            }

            if (error) error->clear();
            return pokemon;
        }

        std::byte *encryptBankRecord(GameVersion group, std::span<const std::byte> data,
                                     uint32_t encryptionConstant)
        {
            switch (group)
            {
            case GameVersion::RBY:
            case GameVersion::GSC:
            {
                auto *result = new std::byte[data.size()];
                std::memcpy(result, data.data(), data.size());
                return result;
            }
            case GameVersion::FRLG:
            case GameVersion::RSE:
                return Encryption::encryptArray3FRLG(data);
            case GameVersion::DP:
            case GameVersion::PT:
            case GameVersion::HGSS:
                return Encryption::encryptArray4HGSS(data);
            case GameVersion::BW:
            case GameVersion::B2W2:
                return Encryption::encryptArray5B2W2(data);
            case GameVersion::XY:
            case GameVersion::ORAS:
                return Encryption::encryptArray6ORAS(data);
            case GameVersion::SM:
            case GameVersion::USUM:
                return Encryption::encryptArray7USUM(data);
            case GameVersion::GG:
                return Encryption::encryptArray7LGPE(data, encryptionConstant);
            case GameVersion::SWSH:
                return Encryption::encryptArray8SWSH(data, encryptionConstant);
            case GameVersion::BDSP:
                return Encryption::encryptArray8BDSP(data, encryptionConstant);
            case GameVersion::PLA:
                return Encryption::encryptArray8LA(data, encryptionConstant);
            case GameVersion::SV:
                return Encryption::encryptArray9SV(data, encryptionConstant);
            case GameVersion::ZA:
                return Encryption::encryptArray9LZA(data, encryptionConstant);
            default:
                return nullptr;
            }
        }

        std::string extensionForGroup(GameVersion group)
        {
            switch (group)
            {
            case GameVersion::RBY: return ".pk1";
            case GameVersion::GSC: return ".pk2";
            case GameVersion::FRLG:
            case GameVersion::RSE: return ".pk3";
            case GameVersion::DP:
            case GameVersion::PT:
            case GameVersion::HGSS: return ".pk4";
            case GameVersion::BW:
            case GameVersion::B2W2: return ".pk5";
            case GameVersion::XY:
            case GameVersion::ORAS: return ".pk6";
            case GameVersion::SM:
            case GameVersion::USUM: return ".pk7";
            case GameVersion::GG: return ".pb7";
            case GameVersion::SWSH: return ".pk8";
            case GameVersion::BDSP: return ".pb8";
            case GameVersion::PLA: return ".pa8";
            case GameVersion::SV: return ".pk9";
            case GameVersion::ZA: return ".pa9";
            default: return {};
            }
        }

        std::string safeSpeciesName(const Pokemon::Pokemon &pokemon)
        {
            std::string name = pokemon.species() ? pokemon.species() : "Pokemon";
            for (char &character : name)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (!std::isalnum(value) && character != '-' && character != '_')
                    character = '_';
            }
            while (!name.empty() && name.back() == '_')
                name.pop_back();
            return name.empty() ? "Pokemon" : name;
        }
    }

    const std::vector<std::string> &extensions()
    {
        static const std::vector<std::string> values = {
            ".pk1", ".pk2", ".pk3", ".pk4", ".pk5", ".pk6", ".pk7",
            ".pb7", ".pk8", ".pb8", ".pa8", ".pk9", ".pa9",
        };
        return values;
    }

    bool supportsFileName(const std::string &fileName)
    {
        return specForExtension(lowerExtension(fileName)) != nullptr;
    }

    LoadResult parse(std::span<const std::byte> bytes, const std::string &fileName)
    {
        std::string error;
        auto pokemon = parseUnchecked(bytes, fileName, &error);
        if (!pokemon)
            return {nullptr, std::move(error)};

        // No caller may receive a short PK8/PB8/PK9/PB7 buffer: party accessors can
        // read outside it. Promote before returning, not just during Bank insertion.
        std::string promotionError;
        auto promoted = prepareForBank(*pokemon, &promotionError);
        if (!promoted)
            return {nullptr, std::move(promotionError)};
        std::string roundTripError;
        if (serialize(*promoted, &roundTripError).empty())
            return {nullptr, "native round-trip failed: " + roundTripError};
        return {std::move(promoted), {}};
    }

    LoadResult load(const std::string &path)
    {
        const FormatSpec *fileFormat = specForExtension(lowerExtension(path));
        if (!fileFormat)
            return {nullptr, "unsupported Pokemon file extension"};
        struct stat fileDetails{};
        if (stat(path.c_str(), &fileDetails) != 0 || !S_ISREG(fileDetails.st_mode) ||
            fileDetails.st_size < 0 ||
            !sizeAllowed(*fileFormat, static_cast<size_t>(fileDetails.st_size)))
            return {nullptr, "Pokemon file has an unexpected size"};
        size_t byteCount = 0;
        uint8_t *raw = Utils::readAllBytes(path.c_str(), &byteCount);
        if (!raw)
            return {nullptr, "could not read Pokemon file"};

        const auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte *>(raw), byteCount);
        LoadResult result = parse(bytes, path);
        // readAllBytes() allocates with malloc(); pairing it with delete[] is undefined behaviour.
        std::free(raw);
        return result;
    }


    std::unique_ptr<Pokemon::Pokemon> prepareForBank(const Pokemon::Pokemon &pokemon, std::string *error)
    {
        const GameVersion bankGroup = Bank::groupAsBanked(pokemon.getGameGroup());
        if (bankGroup == GameVersion::Invalid)
        {
            if (error) *error = "Pokemon format cannot be stored in the PKSE Bank";
            return nullptr;
        }

        // Gen 1/2 have locale-dependent native lengths that the Bank tags explicitly preserve.
        // Gen 3 already has a dedicated 80-byte depositedSpan() path in Bank.cpp. Keep those
        // byte-preserving instead of manufacturing a different native representation.
        if (bankGroup == GameVersion::RBY || bankGroup == GameVersion::GSC ||
            bankGroup == GameVersion::FRLG)
        {
            auto copy = pokemon.clone();
            if (!copy && error) *error = "Pokemon entity does not support cloning";
            else if (error) error->clear();
            return copy;
        }

        const size_t bankSize = Bank::recordSizeFor(bankGroup);
        if (bankSize == 0)
        {
            if (error) *error = "PKSE Bank has no record size for this Pokemon format";
            return nullptr;
        }
        if (pokemon.getDataSize() == bankSize)
        {
            auto copy = pokemon.clone();
            if (!copy && error) *error = "Pokemon entity does not support cloning";
            else if (error) error->clear();
            return copy;
        }
        if (pokemon.getDataSize() > bankSize)
        {
            if (error) *error = "Pokemon record is larger than the PKSE Bank format";
            return nullptr;
        }

        // The native file is a stored/box record. Build the Bank's party-sized representation from
        // the already-validated DECRYPTED stored bytes, then let the existing entity class calculate
        // the party-only level/stats tail. Padding encrypted bytes would be wrong because every
        // generation encrypts that tail as part of its native record.
        std::vector<std::byte> padded(bankSize, std::byte{0});
        const auto source = pokemon.getData();
        std::memcpy(padded.data(), source.data(), source.size());
        // Modern formats keep the battle level in a party-only byte. Leaving it zero
        // makes recalculateStats() return early, giving a Level 0 / 0 HP import.
        if ((bankGroup == GameVersion::SWSH || bankGroup == GameVersion::BDSP ||
             bankGroup == GameVersion::SV || bankGroup == GameVersion::ZA) && bankSize > 0x148)
        {
            padded[0x148] = static_cast<std::byte>(
                Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(pokemon.speciesID())));
        }
        if (bankGroup == GameVersion::PLA && bankSize > 0x168)
        {
            padded[0x168] = static_cast<std::byte>(
                Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(pokemon.speciesID())));
        }

        std::byte *encrypted = encryptBankRecord(bankGroup, padded, pokemon.encryptionConstant());
        if (!encrypted)
        {
            if (error) *error = "Pokemon format has no Bank promotion serializer";
            return nullptr;
        }

        auto promoted = Bank::makePokemon(
            bankGroup, std::span<const std::byte>(encrypted, bankSize));
        delete[] encrypted;
        if (!promoted)
        {
            if (error) *error = "could not construct party-sized Pokemon record for the Bank";
            return nullptr;
        }

        promoted->recalculateStats();
        promoted->refreshChecksum();
        if (!promoted->isStructurallyValid())
        {
            if (error) *error = "party-sized Bank record failed structural/checksum validation";
            return nullptr;
        }

        // Prove the promoted entity can still pass the same native serializer/reparse contract as a
        // directly loaded file before exposing it to Storage.
        std::string verifyError;
        if (serialize(*promoted, &verifyError).empty())
        {
            if (error) *error = "Bank promotion round-trip failed: " + verifyError;
            return nullptr;
        }

        if (error) error->clear();
        return promoted;
    }

    std::vector<std::byte> serialize(const Pokemon::Pokemon &pokemon, std::string *error)
    {
        const std::string extension = extensionFor(pokemon);
        const FormatSpec *spec = specForExtension(extension);
        if (!spec)
        {
            if (error) *error = "Pokemon entity format has no supported native extension";
            return {};
        }
        if (!sizeAllowed(*spec, pokemon.getDataSize()))
        {
            if (error) *error = "Pokemon entity size does not match its native file format";
            return {};
        }

        auto copy = pokemon.clone();
        if (!copy)
        {
            if (error) *error = "Pokemon entity does not support cloning";
            return {};
        }
        // These formats may have cached party stats but no party level on box exports.
        // Only adjust the clone; the source Pokémon must never be mutated by export.
        if (copy->getGameGroup() == GameVersion::RBY && copy->getDataSize() > 0x24)
            copy->getData()[3 + 0x21] = static_cast<std::byte>(copy->level());
        if (copy->getGameGroup() == GameVersion::PLA && copy->getDataSize() > 0x168)
            copy->getData()[0x168] = static_cast<std::byte>(copy->level());
        copy->refreshChecksum();
        if (!copy->isStructurallyValid())
        {
            if (error) *error = "Pokemon entity is not structurally valid after checksum refresh";
            return {};
        }

        // Canonical PKHeX .pk* / .pb* / .pa* files are DECRYPTED; the old version
        // emitted .ek* bytes under a .pk* filename and was not interoperable.
        const auto decoded = copy->getData();
        std::vector<std::byte> native(decoded.begin(), decoded.end());
        std::string verifyError;
        auto roundTripped = parseUnchecked(native, extension, &verifyError);
        if (!roundTripped)
        {
            if (error) *error = "export verification failed: " + verifyError;
            return {};
        }
        if (roundTripped->getDataSize() != copy->getDataSize() ||
            std::memcmp(roundTripped->getData().data(), decoded.data(), decoded.size()) != 0)
        {
            if (error) *error = "export verification changed Pokemon bytes after native reparse";
            return {};
        }

        if (error) error->clear();
        return native;
    }

    std::string extensionFor(const Pokemon::Pokemon &pokemon)
    {
        return extensionForGroup(pokemon.getGameGroup());
    }

    std::string defaultExportPath(const Pokemon::Pokemon &pokemon)
    {
        const std::string extension = extensionFor(pokemon);
        if (extension.empty())
            return {};

        const std::string directory = BASE_SAVE_DIRECTORY + "/exports";
        char identity[9]{};
        const uint32_t value = pokemon.pid() != 0 ? pokemon.pid() : pokemon.id32();
        std::snprintf(identity, sizeof(identity), "%08X", static_cast<unsigned int>(value));

        const std::string stem = directory + "/" + std::to_string(pokemon.speciesID()) + "-" +
                                 safeSpeciesName(pokemon) + "-" + identity;
        std::string path = stem + extension;
        for (unsigned int suffix = 2; fileExists(path) && suffix < 10000; ++suffix)
            path = stem + "-" + std::to_string(suffix) + extension;
        return fileExists(path) ? std::string{} : path;
    }

    bool write(const Pokemon::Pokemon &pokemon, const std::string &path, std::string *error)
    {
        if (path.empty())
        {
            if (error) *error = "export path is empty";
            return false;
        }
        if (fileExists(path))
        {
            if (error) *error = "export path already exists";
            return false;
        }

        std::string serializeError;
        const std::vector<std::byte> native = serialize(pokemon, &serializeError);
        if (native.empty())
        {
            if (error) *error = serializeError;
            return false;
        }

        mkdir(BASE_SAVE_DIRECTORY.c_str(), 0777); // ignore EEXIST
        const std::string exportDirectory = BASE_SAVE_DIRECTORY + "/exports";
        mkdir(exportDirectory.c_str(), 0777); // ignore EEXIST

        const std::string temporary = path + ".tmp";
        FILE *file = std::fopen(temporary.c_str(), "wb");
        if (!file)
        {
            if (error) *error = "could not open temporary export file";
            return false;
        }

        const size_t written = std::fwrite(native.data(), 1, native.size(), file);
        const int flushResult = std::fflush(file);
        const int closeResult = std::fclose(file);
        if (written != native.size() || flushResult != 0 || closeResult != 0)
        {
            std::remove(temporary.c_str());
            if (error) *error = "could not write complete Pokemon export";
            return false;
        }

        // Verify the actual bytes that reached the SD card before the temporary file is promoted.
        size_t verifySize = 0;
        uint8_t *verifyRaw = Utils::readAllBytes(temporary.c_str(), &verifySize);
        const bool verified = verifyRaw && verifySize == native.size() &&
                              std::memcmp(verifyRaw, native.data(), native.size()) == 0;
        std::free(verifyRaw); // readAllBytes() uses malloc().
        if (!verified)
        {
            std::remove(temporary.c_str());
            if (error) *error = "Pokemon export failed disk read-back verification";
            return false;
        }

        if (std::rename(temporary.c_str(), path.c_str()) != 0)
        {
            std::remove(temporary.c_str());
            if (error) *error = "could not promote verified Pokemon export";
            return false;
        }

        if (error) error->clear();
        return true;
    }
}
