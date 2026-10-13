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
#include "Pokemon/Experience.h"
#include "Trainer/Bank.h"
#include "Utils/FileUtilities.h"
#include "Utils/Logger.h"

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
        // the valid representation inside that format. Every group is the one Bank::groupAsBanked()
        // answers for its format, which is what lets extensionForGroup() read this table.
        //
        // This is the ONE list of supported extensions: extensions() and extensionForGroup() are both
        // derived from it.
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

        /// Pokemon GO's origin-game id (PKHeX GameVersion.GO). Every GO Park transfer into Let's Go
        /// carries it, including every Meltan and Melmetal, so a .pb7 must accept it.
        constexpr uint8_t ORIGIN_GAME_GO = 34;

        /// Sets the caller's error and logs it. Every refusal in this file goes through here, so an
        /// import or export that fails always leaves its reason in the debug log.
        void reportFailure(std::string *error, const std::string &message)
        {
            Utils::logErrorToFile("PokemonFile: " + message);
            if (error)
                *error = message;
        }

        std::string lowerExtension(const std::string &path)
        {
            const size_t slash = path.find_last_of("/\\");
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                return {};

            std::string extension = path.substr(dot);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return extension;
        }

        const FormatSpec *specForExtension(const std::string &extension)
        {
            for (const FormatSpec &formatSpec : FORMAT_SPECS)
                if (extension == formatSpec.extension)
                    return &formatSpec;
            return nullptr;
        }

        bool sizeAllowed(const FormatSpec &formatSpec, size_t byteCount) noexcept
        {
            return byteCount == formatSpec.shortSize || byteCount == formatSpec.longSize;
        }

        bool fileExists(const std::string &path)
        {
            struct stat fileStatus{};
            return stat(path.c_str(), &fileStatus) == 0;
        }

        uint8_t byteAt(std::span<const std::byte> record, size_t offset)
        {
            return static_cast<uint8_t>(record[offset]);
        }

        uint16_t readUInt16At(std::span<const std::byte> record, size_t offset)
        {
            return static_cast<uint16_t>(byteAt(record, offset) | (byteAt(record, offset + 1) << 8));
        }

        uint32_t readUInt32At(std::span<const std::byte> record, size_t offset)
        {
            return static_cast<uint32_t>(readUInt16At(record, offset)) |
                   (static_cast<uint32_t>(readUInt16At(record, offset + 2)) << 16);
        }

        bool anyByteSet(std::span<const std::byte> record, size_t firstOffset, size_t endOffset)
        {
            for (size_t offset = firstOffset; offset < endOffset; ++offset)
                if (byteAt(record, offset) != 0)
                    return true;
            return false;
        }

        bool validGen12Header(std::span<const std::byte> record, GameVersion group)
        {
            if (byteAt(record, 0) != 1 || byteAt(record, 2) != 0xFF)
                return false;
            if (group == GameVersion::GSC)
            {
                const uint8_t species = byteAt(record, 3);
                if (species == 0 || (byteAt(record, 1) != species && byteAt(record, 1) != 0xFD))
                    return false;
                const uint8_t level = byteAt(record, 3 + 0x1F);
                if (level < 1 || level > 100)
                    return false;
                // Korean G/S uses a distinct charset which this editor cannot decode.
                if (record.size() == Pokemon::SIZE_2ULIST && byteAt(record, 51) <= 0x0B)
                    return false;
            }
            else
            {
                if (byteAt(record, 1) == 0 || byteAt(record, 1) == 0xFF || byteAt(record, 1) != byteAt(record, 3))
                    return false;
            }
            return true;
        }

        // ---------------------------------------------------------------------------------------
        // Which format the CONTENT says a record is. Several formats share a length, a crypt and a
        // checksum (PK4/PK5 box records, PK6/PK7, PK8/PB8/PK9/PA9), so a renamed file decodes, passes
        // its checksum, and is then read with the wrong layout. PKHeX's EntityFormat tells them apart
        // from the decrypted bytes; these are its tests, with its offsets. Each returns the set of
        // extensions the content is consistent with -- more than one where PKHeX itself cannot tell,
        // in which case the extension decides, as it does in PKHeX.
        // ---------------------------------------------------------------------------------------

        /// PKHeX EntityFormat.GetFormat45.
        std::vector<std::string> extensionsForGen45(std::span<const std::byte> record)
        {
            if (readUInt16At(record, 0x04) != 0)
                return {}; // a Battle Revolution record (non-zero sanity), which PKSE has no entity for
            const bool lengthIsGen5Party = record.size() == Encryption::SIZE_PARTY5_B2W2;
            if (byteAt(record, 0x5F) < 0x10 && readUInt16At(record, 0x80) < 0x3333)
                return lengthIsGen5Party ? std::vector<std::string>{} : std::vector<std::string>{".pk4"};
            if (readUInt16At(record, 0x46) != 0) // PK4's extended met location, unused by PK5
                return lengthIsGen5Party ? std::vector<std::string>{} : std::vector<std::string>{".pk4"};
            return {".pk5"};
        }

        /// PKHeX EntityFormat.IsFormatReally7.
        std::vector<std::string> extensionsForGen67(std::span<const std::byte> record)
        {
            const uint8_t originGame = byteAt(record, 0xDF);
            if (originGame > static_cast<uint8_t>(GameVersion::OR))
            {
                if (originGame == static_cast<uint8_t>(GameVersion::GP) ||
                    originGame == static_cast<uint8_t>(GameVersion::GE) || originGame == ORIGIN_GAME_GO)
                    return {".pb7"};
                return {".pk7"};
            }

            const uint16_t species = readUInt16At(record, 0x08);
            if (species > 721) // past the Gen 6 National Dex
                return {".pk7"};
            constexpr uint16_t MAX_MOVE_ID_6 = 621;
            for (size_t moveOffset : {0x5Au, 0x5Cu, 0x5Eu, 0x60u, 0x6Au, 0x6Cu, 0x6Eu, 0x70u})
                if (readUInt16At(record, moveOffset) > MAX_MOVE_ID_6)
                    return {".pk7"};
            if (byteAt(record, 0x14) > 191) // past the Gen 6 ability list
                return {".pk7"};
            if (readUInt16At(record, 0x0A) > 775) // past the Gen 6 item list
                return {".pk7"};

            // PK6's Gen 4 ground tile becomes PK7's hyper-training flags. Only a level 100 Pokemon can
            // be hyper trained, so below 100 a non-zero byte must be a ground tile.
            const uint8_t groundTile = byteAt(record, 0xDE);
            if (groundTile != 0)
            {
                const uint8_t currentLevel =
                    Pokemon::getLevelFromExp(readUInt32At(record, 0x10), Pokemon::getGrowthRate(species));
                if (currentLevel < 100)
                    return {".pk6"};
                const bool fromGen4 = originGame == static_cast<uint8_t>(GameVersion::HG) ||
                                      originGame == static_cast<uint8_t>(GameVersion::SS) ||
                                      originGame == static_cast<uint8_t>(GameVersion::D) ||
                                      originGame == static_cast<uint8_t>(GameVersion::P) ||
                                      originGame == static_cast<uint8_t>(GameVersion::Pt);
                constexpr uint8_t GROUND_TILE_MAX_PLATINUM = 24;
                if (!fromGen4 || groundTile > GROUND_TILE_MAX_PLATINUM)
                    return {".pk7"};
            }

            // Gen 6 markings are one bit each; Gen 7's are two bits that are never both set.
            const uint16_t markings = readUInt16At(record, 0x16);
            if (markings > 0xAAA)
                return {".pk6"};
            for (int markingIndex = 0; markingIndex < 6; ++markingIndex)
                if (((markings >> (markingIndex * 2)) & 3) == 3)
                    return {".pk6"};
            if (byteAt(record, 0x2A) > 20) // Gen 7's Poke Pelago event status is always below 20
                return {".pk6"};
            return {".pk6", ".pk7"};
        }

        /// PKHeX EntityFormat.IsFormatReally8b.
        std::vector<std::string> extensionsForGen8(std::span<const std::byte> record)
        {
            if (readUInt16At(record, 0x08) > 493) // past the Gen 4 National Dex, so not Brilliant Diamond
                return {".pk8"};
            if (byteAt(record, 0x90) != 0) // Dynamax level
                return {".pk8"};
            if ((byteAt(record, 0x16) & 0x10) != 0) // Gigantamax factor
                return {".pk8"};
            return {".pk8", ".pb8"};
        }

        /// PKHeX EntityFormat.GetFormat89. Its last clue (a held Mega Stone, which Scarlet/Violet
        /// have none of) is left out, so such a record reads as either Gen 9 format and the extension
        /// decides -- the lenient direction.
        std::vector<std::string> extensionsForGen89(std::span<const std::byte> record)
        {
            const uint8_t originGameZA = static_cast<uint8_t>(GameVersion::ZA);
            if (readUInt32At(record, 0x120) == 0) // never captured: no egg or met location
            {
                if (byteAt(record, 0xCE) == originGameZA)
                    return {".pa9"};
                if (byteAt(record, 0xE8) == 0) // Gen 8 formats keep 0xFF here for the affixed ribbon
                    return {".pk9"};
                return {".pk8", ".pb8"};
            }

            if (byteAt(record, 0x11F) == 0) // unused in Gen 8; Gen 9's obedience level
            {
                const bool isEgg = ((readUInt32At(record, 0x8C) >> 30) & 1) != 0;
                if (!isEgg || byteAt(record, 0xDE) != 0) // a Gen 8 non-egg, or a Gen 8 egg
                    return extensionsForGen8(record);
                return {".pk9"}; // a Scarlet/Violet egg
            }

            if (byteAt(record, 0xCE) == originGameZA)
                return {".pa9"};
            if (byteAt(record, 0x23) != 0) // alpha
                return {".pa9"};
            if (anyByteSet(record, 0x96, 0xA0) || anyByteSet(record, 0xD6, 0xF7)) // Z-A plus flags
                return {".pa9"};
            if (anyByteSet(record, 0x82, 0x8A)) // relearn moves, unused by Z-A
                return {".pk9"};
            return {".pk9", ".pa9"};
        }

        /// Empty when the decrypted `record` is consistent with `extension`; otherwise the refusal.
        std::string contentMismatch(std::span<const std::byte> record, const std::string &extension)
        {
            std::vector<std::string> consistentExtensions;
            if (extension == ".pk4" || extension == ".pk5")
                consistentExtensions = extensionsForGen45(record);
            else if (extension == ".pk6" || extension == ".pk7" || extension == ".pb7")
                consistentExtensions = extensionsForGen67(record);
            else if (extension == ".pk8" || extension == ".pb8" || extension == ".pk9" || extension == ".pa9")
                consistentExtensions = extensionsForGen89(record);
            else
                return {};

            if (std::find(consistentExtensions.begin(), consistentExtensions.end(), extension) !=
                consistentExtensions.end())
                return {};
            if (consistentExtensions.empty())
                return "the contents of this " + extension + " file are not a format PKSE can read";
            return "this file is named " + extension + " but its contents are a " + consistentExtensions.front() +
                   " Pokemon (rename it to " + consistentExtensions.front() + ")";
        }

        std::unique_ptr<Pokemon::Pokemon> parseUnchecked(std::span<const std::byte> bytes,
                                                         const std::string &fileName,
                                                         std::string *error)
        {
            const std::string extension = lowerExtension(fileName);
            const FormatSpec *formatSpec = specForExtension(extension);
            if (!formatSpec)
            {
                reportFailure(error, extension.empty() ? "Pokemon file has no supported extension"
                                                       : "unsupported Pokemon file extension: " + extension);
                return nullptr;
            }
            if (!sizeAllowed(*formatSpec, bytes.size()))
            {
                std::string sizeMessage = "wrong byte size for " + extension + ": expected " +
                                          std::to_string(formatSpec->shortSize);
                if (formatSpec->longSize != formatSpec->shortSize)
                    sizeMessage += " or " + std::to_string(formatSpec->longSize);
                reportFailure(error, sizeMessage + ", got " + std::to_string(bytes.size()));
                return nullptr;
            }

            const bool gameBoyFormat = formatSpec->group == GameVersion::RBY || formatSpec->group == GameVersion::GSC;
            if (gameBoyFormat && !validGen12Header(bytes, formatSpec->group))
            {
                reportFailure(error, "invalid Gen 1/2 list header, level, or unsupported locale");
                return nullptr;
            }

            auto structurallyValid = [&](const std::unique_ptr<Pokemon::Pokemon> &candidate)
            {
                return candidate && candidate->speciesID() != 0 && candidate->isStructurallyValid() &&
                       (candidate->getDataSize() == bytes.size() ||
                        (formatSpec->group == GameVersion::PLA && bytes.size() == formatSpec->shortSize &&
                         candidate->getDataSize() == formatSpec->longSize));
            };

            // Standard PKHeX/PKSM .pk* files are DECRYPTED, so that reading is tried first; the
            // entity constructors take encrypted bytes, so the plaintext is encrypted on the way in.
            // A file holding encrypted bytes under a .pk* name is the fallback. Plaintext first
            // matters: a plaintext record has about a 1-in-65,536 chance of also passing as
            // encrypted, and would then decode as garbage.
            std::unique_ptr<Pokemon::Pokemon> pokemon;
            std::byte *encrypted = Bank::encryptRecord(formatSpec->group, bytes, readUInt32At(bytes, 0));
            if (encrypted)
            {
                pokemon = Bank::makePokemon(formatSpec->group, std::span<const std::byte>(encrypted, bytes.size()));
                delete[] encrypted;
            }
            if (!structurallyValid(pokemon) && !gameBoyFormat)
                pokemon = Bank::makePokemon(formatSpec->group, bytes);
            if (!structurallyValid(pokemon))
            {
                reportFailure(error, "Pokemon record failed structural/checksum validation");
                return nullptr;
            }

            const std::string contentError =
                contentMismatch(pokemon->getData().first(bytes.size()), extension);
            if (!contentError.empty())
            {
                reportFailure(error, contentError);
                return nullptr;
            }

            if (error)
                error->clear();
            return pokemon;
        }

        std::string extensionForGroup(GameVersion group)
        {
            const GameVersion bankedGroup = Bank::groupAsBanked(group);
            for (const FormatSpec &formatSpec : FORMAT_SPECS)
                if (formatSpec.group == bankedGroup)
                    return formatSpec.extension;
            return {};
        }

        std::string safeSpeciesName(const Pokemon::Pokemon &pokemon)
        {
            std::string name = pokemon.species() ? pokemon.species() : "Pokemon";
            for (char &character : name)
            {
                const unsigned char nameByte = static_cast<unsigned char>(character);
                if (!std::isalnum(nameByte) && character != '-' && character != '_')
                    character = '_';
            }
            while (!name.empty() && name.back() == '_')
                name.pop_back();
            return name.empty() ? "Pokemon" : name;
        }
    }

    const std::vector<std::string> &extensions()
    {
        static const std::vector<std::string> supportedExtensions = []
        {
            std::vector<std::string> fromTable;
            for (const FormatSpec &formatSpec : FORMAT_SPECS)
                fromTable.emplace_back(formatSpec.extension);
            return fromTable;
        }();
        return supportedExtensions;
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
        auto bankReady = prepareForBank(*pokemon, &error);
        if (!bankReady)
            return {nullptr, std::move(error)};

        // Prove the record the Bank will hold survives the same export-and-reparse contract as a
        // file written by PKSE, once, before Storage ever sees it.
        if (serialize(*bankReady, &error).empty())
        {
            const std::string roundTripError = "native round-trip failed: " + error;
            reportFailure(nullptr, roundTripError);
            return {nullptr, roundTripError};
        }
        return {std::move(bankReady), {}};
    }

    LoadResult load(const std::string &path)
    {
        std::string error;
        const FormatSpec *fileFormat = specForExtension(lowerExtension(path));
        if (!fileFormat)
        {
            reportFailure(&error, "unsupported Pokemon file extension");
            return {nullptr, std::move(error)};
        }
        struct stat fileStatus{};
        if (stat(path.c_str(), &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode) || fileStatus.st_size < 0 ||
            !sizeAllowed(*fileFormat, static_cast<size_t>(fileStatus.st_size)))
        {
            reportFailure(&error, "Pokemon file has an unexpected size");
            return {nullptr, std::move(error)};
        }
        size_t byteCount = 0;
        uint8_t *fileContents = Utils::readAllBytes(path.c_str(), &byteCount);
        if (!fileContents)
        {
            reportFailure(&error, "could not read Pokemon file");
            return {nullptr, std::move(error)};
        }

        const auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(fileContents), byteCount);
        LoadResult loadResult = parse(bytes, path);
        // readAllBytes() allocates with malloc(); pairing it with delete[] is undefined behaviour.
        std::free(fileContents);
        return loadResult;
    }

    std::unique_ptr<Pokemon::Pokemon> prepareForBank(const Pokemon::Pokemon &pokemon, std::string *error)
    {
        const GameVersion bankGroup = Bank::groupAsBanked(pokemon.getGameGroup());
        if (bankGroup == GameVersion::Invalid)
        {
            reportFailure(error, "Pokemon format cannot be stored in the PKSE Bank");
            return nullptr;
        }

        // Gen 1/2 have locale-dependent native lengths that the Bank tags explicitly preserve.
        // Gen 3 already has a dedicated 80-byte depositedSpan() path in Bank.cpp. Keep those
        // byte-preserving instead of manufacturing a different native representation.
        if (bankGroup == GameVersion::RBY || bankGroup == GameVersion::GSC || bankGroup == GameVersion::FRLG)
        {
            auto bankCopy = pokemon.clone();
            if (!bankCopy)
            {
                reportFailure(error, "Pokemon entity does not support cloning");
                return nullptr;
            }
            // A Gen 1/2 file written from a BOX record carries the party body with every stat 0
            // (PKHeX's own test files include three). The game computes those stats when a Pokemon
            // leaves the PC, which is what Trainer1RBY and Trainer2GSC do on load too -- without it
            // the Bank shows 0 HP and a withdrawal into a party writes it.
            if ((bankGroup == GameVersion::RBY || bankGroup == GameVersion::GSC) && bankCopy->statHPMax() == 0)
            {
                bankCopy->recalculateStats();
                bankCopy->refreshChecksum();
            }
            if (error)
                error->clear();
            return bankCopy;
        }

        const size_t bankSize = Bank::recordSizeFor(bankGroup);
        if (bankSize == 0)
        {
            reportFailure(error, "PKSE Bank has no record size for this Pokemon format");
            return nullptr;
        }
        if (pokemon.getDataSize() == bankSize)
        {
            auto bankCopy = pokemon.clone();
            if (!bankCopy)
            {
                reportFailure(error, "Pokemon entity does not support cloning");
                return nullptr;
            }
            if (error)
                error->clear();
            return bankCopy;
        }
        if (pokemon.getDataSize() > bankSize)
        {
            reportFailure(error, "Pokemon record is larger than the PKSE Bank format");
            return nullptr;
        }

        // The native file is a stored/box record. Build the Bank's party-sized representation from
        // the already-validated DECRYPTED stored bytes, then let the existing entity class calculate
        // the party-only level/stats tail. Padding encrypted bytes would be wrong because every
        // generation encrypts that tail as part of its native record.
        std::vector<std::byte> partyRecord(bankSize, std::byte{0});
        const auto storedBytes = pokemon.getData();
        std::memcpy(partyRecord.data(), storedBytes.data(), storedBytes.size());
        // Modern formats keep the battle level in a party-only byte. Leaving it zero
        // makes recalculateStats() return early, giving a Level 0 / 0 HP import.
        const auto levelFromExp = static_cast<std::byte>(
            Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(pokemon.speciesID())));
        if ((bankGroup == GameVersion::SWSH || bankGroup == GameVersion::BDSP || bankGroup == GameVersion::SV ||
             bankGroup == GameVersion::ZA) &&
            bankSize > 0x148)
            partyRecord[0x148] = levelFromExp;
        if (bankGroup == GameVersion::PLA && bankSize > 0x168)
            partyRecord[0x168] = levelFromExp;

        std::byte *encrypted = Bank::encryptRecord(bankGroup, partyRecord, pokemon.encryptionConstant());
        if (!encrypted)
        {
            reportFailure(error, "Pokemon format has no Bank promotion serializer");
            return nullptr;
        }

        auto promoted = Bank::makePokemon(bankGroup, std::span<const std::byte>(encrypted, bankSize));
        delete[] encrypted;
        if (!promoted)
        {
            reportFailure(error, "could not construct party-sized Pokemon record for the Bank");
            return nullptr;
        }

        promoted->recalculateStats();
        promoted->refreshChecksum();
        if (!promoted->isStructurallyValid())
        {
            reportFailure(error, "party-sized Bank record failed structural/checksum validation");
            return nullptr;
        }

        if (error)
            error->clear();
        return promoted;
    }

    std::vector<std::byte> serialize(const Pokemon::Pokemon &pokemon, std::string *error)
    {
        const std::string extension = extensionFor(pokemon);
        const FormatSpec *formatSpec = specForExtension(extension);
        if (!formatSpec)
        {
            reportFailure(error, "Pokemon entity format has no supported native extension");
            return {};
        }
        if (!sizeAllowed(*formatSpec, pokemon.getDataSize()))
        {
            reportFailure(error, "Pokemon entity size does not match its native file format");
            return {};
        }

        auto exportCopy = pokemon.clone();
        if (!exportCopy)
        {
            reportFailure(error, "Pokemon entity does not support cloning");
            return {};
        }
        // These formats may have cached party stats but no party level on box exports.
        // Only adjust the clone; the source Pokémon must never be mutated by export.
        if (exportCopy->getGameGroup() == GameVersion::RBY && exportCopy->getDataSize() > 0x24)
            exportCopy->getData()[3 + 0x21] = static_cast<std::byte>(exportCopy->level());
        if (exportCopy->getGameGroup() == GameVersion::PLA && exportCopy->getDataSize() > 0x168)
            exportCopy->getData()[0x168] = static_cast<std::byte>(exportCopy->level());
        exportCopy->refreshChecksum();
        if (!exportCopy->isStructurallyValid())
        {
            reportFailure(error, "Pokemon entity is not structurally valid after checksum refresh");
            return {};
        }

        // Canonical PKHeX .pk* / .pb* / .pa* files are DECRYPTED; the old version
        // emitted .ek* bytes under a .pk* filename and was not interoperable.
        const auto decryptedBytes = exportCopy->getData();
        std::vector<std::byte> fileBytes(decryptedBytes.begin(), decryptedBytes.end());
        std::string verifyError;
        auto roundTripped = parseUnchecked(fileBytes, extension, &verifyError);
        if (!roundTripped)
        {
            reportFailure(error, "export verification failed: " + verifyError);
            return {};
        }
        if (roundTripped->getDataSize() != exportCopy->getDataSize() ||
            std::memcmp(roundTripped->getData().data(), decryptedBytes.data(), decryptedBytes.size()) != 0)
        {
            reportFailure(error, "export verification changed Pokemon bytes after native reparse");
            return {};
        }

        if (error)
            error->clear();
        return fileBytes;
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

        char identity[9]{};
        const uint32_t identityValue = pokemon.hasPID() ? pokemon.pid() : pokemon.id32();
        std::snprintf(identity, sizeof(identity), "%08X", static_cast<unsigned int>(identityValue));

        const std::string stem = POKEMON_EXPORT_DIRECTORY + "/" + std::to_string(pokemon.speciesID()) + "-" +
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
            reportFailure(error, "export path is empty");
            return false;
        }
        if (fileExists(path))
        {
            reportFailure(error, "export path already exists");
            return false;
        }

        const std::vector<std::byte> fileBytes = serialize(pokemon, error);
        if (fileBytes.empty())
            return false;

        mkdir(BASE_SAVE_DIRECTORY.c_str(), 0777);      // ignore EEXIST
        mkdir(POKEMON_EXPORT_DIRECTORY.c_str(), 0777); // ignore EEXIST

        const std::string temporaryPath = path + ".tmp";
        FILE *file = std::fopen(temporaryPath.c_str(), "wb");
        if (!file)
        {
            reportFailure(error, "could not open temporary export file");
            return false;
        }

        const size_t written = std::fwrite(fileBytes.data(), 1, fileBytes.size(), file);
        const int flushResult = std::fflush(file);
        const int closeResult = std::fclose(file);
        if (written != fileBytes.size() || flushResult != 0 || closeResult != 0)
        {
            std::remove(temporaryPath.c_str());
            reportFailure(error, "could not write complete Pokemon export");
            return false;
        }

        // Verify the actual bytes that reached the SD card before the temporary file is promoted.
        size_t verifySize = 0;
        uint8_t *verifyContents = Utils::readAllBytes(temporaryPath.c_str(), &verifySize);
        const bool verified = verifyContents && verifySize == fileBytes.size() &&
                              std::memcmp(verifyContents, fileBytes.data(), fileBytes.size()) == 0;
        std::free(verifyContents); // readAllBytes() uses malloc().
        if (!verified)
        {
            std::remove(temporaryPath.c_str());
            reportFailure(error, "Pokemon export failed disk read-back verification");
            return false;
        }

        if (std::rename(temporaryPath.c_str(), path.c_str()) != 0)
        {
            std::remove(temporaryPath.c_str());
            reportFailure(error, "could not promote verified Pokemon export");
            return false;
        }

        if (error)
            error->clear();
        return true;
    }
}
