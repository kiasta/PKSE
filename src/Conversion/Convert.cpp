#include "Conversion/Convert.h"

#include <cstdint>
#include <span>
#include <vector>
#include <bit>
#include <ctime> // std::time / std::localtime -> HOME-style transfer date for Gen 3 (no met date)

#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Pokemon8BDSP.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Pokemon7SM.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Pokemon/Pokemon9SV.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Pokemon/Pokemon3RSE.h"      // PK3 entity (species conversion: SpeciesConverter3.h)
#include "Pokemon/Pokemon1RBY.h"       // PK1 -- Poke Transporter reads it through its own accessors
#include "Pokemon/Pokemon2GSC.h"       // PK2 -- same
#include "Pokemon/PersonalInfo7USUM.h" // Gen 7's own growth/friendship/abilities -- the Transporter landing point
#include "Pokemon/SpeciesConverter9.h" // gen9InternalToNational / gen9NationalToInternal (PK9/PA9 species field)
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/PokemonTypes.h"  // getPokemonTypes -> Tera type for cross-generation PK8->PK9
#include "Pokemon/LearnsetTable.h" // isLearnable -> sanitize the moveset to the destination game
#include "Pokemon/Experience.h"    // getLevelFromExp / getGrowthRate -> seed a fresh party-stat tail
#include "Encryption/Encryption8SWSH.h"
#include "Encryption/Encryption8BDSP.h"
#include "Encryption/Encryption8LA.h"
#include "Encryption/Encryption7LGPE.h"
#include "Encryption/Encryption7SM.h"
#include "Encryption/Encryption7USUM.h"
#include "Encryption/Encryption9SV.h"
#include "Encryption/Encryption9LZA.h"
#include "Encryption/Encryption3FRLG.h"
#include "Encryption/Encryption3RSE.h"
#include "Enums/Ball.h"            // maxBallForGroup -> the destination format's ball ceiling
#include "Pokemon/EvolutionTable.h" // getPreEvolutionChain -- a Wartortle is a caught Squirtle
#include "Legality/EncounterTable.h" // a Gen 3 landing spot comes from a REAL encounter
#include "Names/ItemNames.h"       // itemG3ToModern / itemModernToG3 (Gen 3 <-> modern held item)
#include "Names/ItemPresence.h"    // isHeldItemPresent -> sanitize the held item to the destination
#include "Names/MoveInfo.h"        // getMoveMaxPP -> clamp carried-over PP to the destination's max
#include "Names/MovePresence.h"    // isMovePresent -> the only move filter a Transporter run applies
#include "Globals.h"               // g_allowIllegalEdits -- lifts the BDSP Spinda/Nincada transfer block
#include "Enums/LanguageID.h"      // safeLanguageForGroup -- a language the destination actually shipped in
#include "Names/NameLanguage.h"    // languageIndexFor -- record language -> name-table index
#include "Utils/Gen3Text.h"        // the Gen 3 character sets, shared with Pokemon3FRLG + Trainer3FRLG
#include "Utils/HelperUtilities.h" // readUInt32LittleEndian; pulls in Utils::utf8ToUtf16
#include "Utils/Logger.h"          // logEventToFile -- a dropped OT name is a refusal worth recording
#include "Utils/StringHelpers.h"   // utf8ToUtf16 -- the transferred nickname is a species name

namespace Names
{
    // Species names are generation-stable, so there is one table and this is the whole of
    // the dependency on it. Forward-declared rather than included: the name table is
    // declared in Trainer/Trainer.h, and a Pokemon-layer source must not include the
    // trainer layer to reach it.
    const char *getSpeciesName(uint16_t speciesId);

    // The same table read in a NAMED language rather than the display one. A down-convert into a
    // format with no is-nicknamed flag writes the species name to mean "not nicknamed", and it has
    // to be the name the RECORD's language calls it -- otherwise a Japanese Pokemon lands in Gen 3
    // called DITTO and reads as deliberately nicknamed to every game and checker that looks.
    const char *getSpeciesNameLocalized(uint16_t speciesId, size_t languageIndex);
}


using Enums::GameVersion;

namespace Conversion
{
    namespace
    {
        // Rebuild the destination entity from an ENCRYPTED record (the subclass ctor decrypts).
        std::unique_ptr<Pokemon::Pokemon> makePokemon(GameVersion gameVersion, std::span<const std::byte> record)
        {
            switch (gameVersion)
            {
            case GameVersion::GG:
                return std::make_unique<Pokemon::Pokemon7LGPE>(record);
            case GameVersion::SM:
                return std::make_unique<Pokemon::Pokemon7SM>(record);
            case GameVersion::USUM:
                return std::make_unique<Pokemon::Pokemon7USUM>(record);
            case GameVersion::SWSH:
                return std::make_unique<Pokemon::Pokemon8SWSH>(record);
            case GameVersion::BDSP:
                return std::make_unique<Pokemon::Pokemon8BDSP>(record);
            case GameVersion::PLA:
                return std::make_unique<Pokemon::Pokemon8LA>(record);
            case GameVersion::SV:
                return std::make_unique<Pokemon::Pokemon9SV>(record);
            case GameVersion::ZA:
                return std::make_unique<Pokemon::Pokemon9LZA>(record);
            case GameVersion::FRLG:
                return std::make_unique<Pokemon::Pokemon3FRLG>(record);
            case GameVersion::RSE:
                return std::make_unique<Pokemon::Pokemon3RSE>(record);
            default:
                return nullptr;
            }
        }

        // Encrypt a decrypted buffer with the destination format's crypto (all four use Encrypt8).
        std::byte *encryptFor(GameVersion gameVersion, std::span<const std::byte> decryptedRecord,
                              uint32_t encryptionConstant)
        {
            switch (gameVersion)
            {
            case GameVersion::GG:
                return Encryption::encryptArray7LGPE(decryptedRecord, encryptionConstant);
            case GameVersion::SM:
                return Encryption::encryptArray7SM(decryptedRecord, encryptionConstant);
            case GameVersion::USUM:
                return Encryption::encryptArray7USUM(decryptedRecord, encryptionConstant);
            case GameVersion::SWSH:
                return Encryption::encryptArray8SWSH(decryptedRecord, encryptionConstant);
            case GameVersion::BDSP:
                return Encryption::encryptArray8BDSP(decryptedRecord, encryptionConstant);
            case GameVersion::PLA:
                return Encryption::encryptArray8LA(decryptedRecord, encryptionConstant);
            case GameVersion::SV:
                return Encryption::encryptArray9SV(decryptedRecord, encryptionConstant);
            case GameVersion::ZA:
                return Encryption::encryptArray9LZA(decryptedRecord, encryptionConstant);
            case GameVersion::FRLG:
                return Encryption::encryptArray3FRLG(decryptedRecord); // Gen 3 keys off the in-buffer PID
            case GameVersion::RSE:
                return Encryption::encryptArray3RSE(decryptedRecord); // one crypt for all five GBA games
            default:
                return nullptr;
            }
        }

        uint8_t presenceBit(GameVersion gameVersion)
        {
            switch (gameVersion)
            {
            case GameVersion::GG:
                return Pokemon::PERSONAL_GAME_GG;
            case GameVersion::SWSH:
                return Pokemon::PERSONAL_GAME_SWSH;
            case GameVersion::BDSP:
                return Pokemon::PERSONAL_GAME_BDSP;
            case GameVersion::PLA:
                return Pokemon::PERSONAL_GAME_PLA;
            case GameVersion::SV:
                return Pokemon::PERSONAL_GAME_SV;
            case GameVersion::ZA:
                return Pokemon::PERSONAL_GAME_ZA;
            default:
                return 0;
            }
        }

        // PK8-layout group (SwSh/BDSP) vs PK9-layout group (S/V/Z-A). LGPE (GG) and Legends: Arceus
        // (PLA) use different containers/crypto and are handled by neither -> Unsupported.
        inline bool isG8(GameVersion gameVersion)
        {
            return gameVersion == GameVersion::SWSH || gameVersion == GameVersion::BDSP;
        }
        inline bool isG9(GameVersion gameVersion)
        {
            return gameVersion == GameVersion::SV || gameVersion == GameVersion::ZA;
        }

        // PK7-layout group (Sun/Moon and Ultra Sun/Ultra Moon). Sun/Moon and Ultra Sun/Moon store
        // the SAME 232/260-byte record and differ only in how far their dex, move and item spaces
        // run, so the two interconvert by copying and everything else routes through the PK8 hub.
        // Let's Go is generation 7 as well and is NOT this: PB7 is its own container (GG above).
        inline bool isG7(GameVersion gameVersion)
        {
            return gameVersion == GameVersion::SM || gameVersion == GameVersion::USUM;
        }

        // Red/Blue/Yellow and Gold/Silver/Crystal. These are a SOURCE and never a destination:
        // nothing has ever moved a Pokemon back into a Game Boy game, and a PK1 has no room for
        // the fields a modern one carries. See gate().
        inline bool isVirtualConsole(GameVersion gameVersion)
        {
            return gameVersion == GameVersion::RBY || gameVersion == GameVersion::GSC;
        }

        // Highest National Dex id a Gen 7 game has an entry for. Sun/Moon stops at Necrozma and
        // Ultra Sun/Moon adds the six Ultra Beasts and Zeraora above it -- one number for "Gen 7"
        // quietly grants Sun/Moon six Pokemon it does not have.
        inline uint16_t maxSpeciesG7(GameVersion gameVersion) { return gameVersion == GameVersion::SM ? 802 : 807; }

        // PK8/PK9 party record: the 0x148 stored block + a 0x10-byte battle-stat tail (level @0x148,
        // HP/ATK/DEF/SPE/SPA/SPD @0x14A-0x155). The tail is NOT part of the checksummed/encrypted region,
        // but it IS real storage the entity's level()/statXXX() accessors index -- so every buffer handed
        // to a Gen 8/9 entity must be this long, not the stored size.
        constexpr size_t PARTY_SIZE_G89 = 0x158;

        // Whether a supported+allowed conversion from source -> destination exists (no allocation).
        Result gate(const Pokemon::Pokemon &source, GameVersion destination)
        {
            const GameVersion from = source.getGameGroup();
            if (from == destination)
                return Result::SameGroup;

            // Every mainline pairing PKSE can express. All conversions normalize through the PK8
            // layout: same-generation siblings (SwSh<->BDSP, S/V<->Z-A, SM<->USUM) share it; PK9 reaches it
            // via transformG9toG8; Legends: Arceus (PA8), Let's Go (PB7), Sun/Moon (PK7) and Gen 3
            // (PK3) reach it via their remaps. Full two-way for all of them -- deliberately beyond
            // HOME's one-way Let's Go limit; the dex-presence check below is the only species gate
            // (LGPE = Kanto + Meltan/Melmetal + Alolan forms).
            auto inFamily = [](GameVersion gameVersion)
            {
                return isG8(gameVersion) || isG9(gameVersion) || isG7(gameVersion) || gameVersion == GameVersion::PLA ||
                       gameVersion == GameVersion::GG || isGen3Group(gameVersion);
            };
            // Gen 1 and Gen 2 join the family ONE WAY. They leave through Poke Transporter, which
            // builds a Gen 7 record -- that is the only route out of those games that has ever
            // existed, and PKSE follows it: a Gen 1/2 Pokemon becomes a PK7 first and reaches
            // anything later through the same hub every other transfer uses. Nothing comes BACK.
            // A PK1 has no nature, no ability, no PID, no ball and no met data to put a modern
            // Pokemon's into, so a down-convert would not be lossy, it would be fabrication.
            if (isVirtualConsole(destination))
                return Result::Unsupported;
            if (!(inFamily(from) || isVirtualConsole(from)) || !inFamily(destination))
                return Result::Unsupported;

            const uint16_t species = source.speciesID();
            const uint8_t form = source.form();
            const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(species, form);
            // A form >= the species' form count can't exist in ANY destination. Guard it explicitly:
            // getPersonalInfo() clamps an out-of-range form back to form 0, so without this a corrupt
            // form would read form-0's presence and silently false-pass the dex checks below (then get
            // written verbatim into the destination as an out-of-range form -> bad egg in-game).
            if (form != 0 && form >= Pokemon::getPersonalInfo(species, 0).formCount)
                return Result::NotInDex;
            // Dex-presence gate. Gen 3 has no personal-table presence bit, so use the Gen 3 species range
            // (National <= 386 that maps to a valid internal id). Every other game uses its presence bit.
            //
            // Both Gen 3 groups take this branch and take it identically: Ruby/Sapphire/Emerald and
            // FireRed/LeafGreen share one 386-species dex and one PK3 layout, so what a Kanto save
            // can hold a Hoenn save can hold. The GAMES differ on which species are catchable in
            // them, but that is an encounter question, not a storage one, and the bank has always
            // been allowed to deposit a Pokemon a game could not itself have caught.
            if (isGen3Group(destination))
            {
                // not obtainable in Gen 3
                if (Pokemon::gen3NationalToInternal(species) == 0) return Result::NotInDex;
                // Gen 3 stores no general form byte, so an alternate form (Alolan/Galarian/Hisuian/Paldean
                // variant, a Paldean Tauros breed, an alternate Deoxys, ...) would be silently FLATTENED to
                // the base form on write -- a Combat Breed Tauros arriving in FireRed as a plain Tauros.
                // Refuse instead of corrupting, mirroring PKHeX/HOME. Unown is the one exception: its form
                // rides on the PID (which the remap carries), so Gen 3 re-derives the same letter.
                if (form != 0 && species != 201 /*Unown*/)
                    return Result::NotInDex;
            }
            else if (isG7(destination))
            {
                // Gen 7 predates the personal-table presence bitmask, so presenceBit() has no bit
                // for it and filtering by that would refuse EVERYTHING. Ask Gen 7's own table
                // instead: the species has to be inside that game's dex range, and the form has to
                // be one Gen 7 defines -- a Paldean Tauros or a Hisuian Zorua has no Gen 7 entry,
                // and writing its form index into a PK7 makes a Bad Egg in-game.
                if (species == 0 || species > maxSpeciesG7(destination))
                    return Result::NotInDex;
                if (form != 0 && form >= Pokemon::getPersonalInfo7USUM(species, 0).formCount)
                    return Result::NotInDex;
            }
            else if ((pi.presence & presenceBit(destination)) == 0)
            {
                return Result::NotInDex; // species, or this form specifically, out-of-dex -> refuse
            }
            // Spinda and Nincada into BDSP. This is a LEGALITY rule, not a format one: BDSP holds both
            // species perfectly well -- each is catchable in the Grand Underground and each has a dex
            // entry (Spinda even has its own spot-pattern block) -- but Pokemon HOME refuses to move
            // them in, so one that did not originate there reads as illegal. PKHeX says the same:
            // `Species is Spinda or Nincada && !pk.BDSP -> TransferNotPossible`.
            //
            // That is exactly the "the game can hold it but shouldn't" case g_allowIllegalEdits exists
            // for, so the setting lifts it. Left on by default, since the arriving Pokemon really would
            // be flagged illegal.
            if (destination == GameVersion::BDSP && (species == 327 /*Spinda*/ || species == 290 /*Nincada*/) &&
                !g_allowIllegalEdits)
                return Result::Blocked;
            return Result::Ok;
        }

        // PK8 (SwSh/BDSP) and PK9 (S/V/Z-A) share the 0x148 stored size + Add16 checksum, but a set
        // of fields live at different offsets or bit positions. We CARRY origin identity + every stat
        // field verbatim (PKSE bank philosophy) and only re-lay-out the diverging regions -- unlike
        // PKHeX we do NOT regenerate moves/ability from the destination learnset and do NOT drop the
        // held item (the legality checker flags an out-of-dex moveset instead). Offsets: PKHeX
        // G8PKM.cs / PK9.cs.
        inline uint8_t rd8(const std::vector<std::byte> &recordBytes, size_t offset)
        {
            return static_cast<uint8_t>(recordBytes[offset]);
        }
        inline void wr8(std::vector<std::byte> &recordBytes, size_t offset, uint8_t value)
        {
            recordBytes[offset] = static_cast<std::byte>(value);
        }
        inline uint64_t rd64(const std::vector<std::byte> &recordBytes, size_t offset)
        {
            uint64_t value = 0;
            for (int index = 0; index < 8; ++index)
                value |= static_cast<uint64_t>(rd8(recordBytes, offset + index)) << (8 * index);
            return value;
        }
        inline void wr64(std::vector<std::byte> &recordBytes, size_t offset, uint64_t value)
        {
            for (int index = 0; index < 8; ++index)
                wr8(recordBytes, offset + index, static_cast<uint8_t>(value >> (8 * index)));
        }
        inline void zeroRange(std::vector<std::byte> &recordBytes, size_t offset, size_t byteCount)
        {
            for (size_t bIndex = 0; bIndex < byteCount && (offset + bIndex) < recordBytes.size(); ++bIndex)
                recordBytes[offset + bIndex] = std::byte{0};
        }
        inline void wrf32(std::vector<std::byte> &recordBytes, size_t offset, float value)
        {
            const uint32_t bits = std::bit_cast<uint32_t>(value);
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                wr8(recordBytes, offset + moveIndex, static_cast<uint8_t>(bits >> (8 * moveIndex)));
        }
        inline uint16_t rd16(const std::vector<std::byte> &recordBytes, size_t offset)
        {
            return static_cast<uint16_t>(rd8(recordBytes, offset)) |
                   (static_cast<uint16_t>(rd8(recordBytes, offset + 1)) << 8);
        }
        inline uint32_t rd32(const std::vector<std::byte> &recordBytes, size_t offset)
        {
            return static_cast<uint32_t>(rd16(recordBytes, offset)) |
                   (static_cast<uint32_t>(rd16(recordBytes, offset + 2)) << 16);
        }
        inline void wr16(std::vector<std::byte> &recordBytes, size_t offset, uint16_t value)
        {
            wr8(recordBytes, offset, static_cast<uint8_t>(value));
            wr8(recordBytes, offset + 1, static_cast<uint8_t>(value >> 8));
        }
        inline void wr32(std::vector<std::byte> &recordBytes, size_t offset, uint32_t value)
        {
            wr16(recordBytes, offset, static_cast<uint16_t>(value));
            wr16(recordBytes, offset + 2, static_cast<uint16_t>(value >> 16));
        }

        /// PK3 name field widths, in CHARACTERS -- Gen 3 stores one byte per glyph. The nickname is
        /// 10 at 0x08 and the OT name 7 at 0x14, and neither field has slack past it (0x1B right
        /// after the OT name is Markings). Both the down-convert and canStoreOriginalTrainerName
        /// measure against these, and they have to agree or the warning would describe a different
        /// truncation than the one that happens.
        constexpr int GEN3_NICKNAME_CHARS = 10;
        constexpr int GEN3_OT_NAME_CHARS = 7;

        //
        // EVERY ONE OF THESE TAKES THE RECORD'S LANGUAGE, because Gen 3 has two character tables and
        // a byte means a different glyph in each. Which one a PK3 is written in is decided by its own
        // language byte at 0x12, exactly as PKHeX decides it (PK3.Nickname passes Language into
        // StringConverter3). Getting it wrong is silent: an international table has no slot for most
        // kana and none at all for the full-width Latin a Japanese cartridge stores a Roman name in,
        // so the name simply ends at the first character it cannot place.

        // Gen 3 name (maxG3 bytes @ s+soff) -> UTF-16LE (dstBytes @ d+doff, zero-terminated + zero-padded).
        void g3NameToUtf16(std::vector<std::byte> &destinationBytes, size_t doff,
                           const std::vector<std::byte> &sourceBytes, size_t soff, int maxG3, int dstBytes,
                           uint8_t languageId)
        {
            int destinationIndex = 0;
            for (int index = 0; index < maxG3 && destinationIndex + 2 <= dstBytes; ++index)
            {
                const uint8_t packedByte = rd8(sourceBytes, soff + index);
                if (packedByte == Utils::GEN3_TERMINATOR)
                    break;
                const char16_t character = Utils::gen3ToChar(packedByte, languageId);
                // no glyph at that byte -> skip it
                if (character == u'\0') continue;
                wr16(destinationBytes, doff + destinationIndex, static_cast<uint16_t>(character));
                destinationIndex += 2;
            }
            for (; destinationIndex + 1 < dstBytes; destinationIndex += 2)
                wr16(destinationBytes, doff + destinationIndex, 0); // terminator + padding
        }

        /**
         * UTF-16LE name (srcBytes @ s+soff) -> Gen 3 (maxG3 bytes @ d+doff, 0xFF-terminated/padded).
         *
         * Returns false when the name could NOT be written, and then writes nothing but 0xFF. That is
         * the difference between the two ways a name fails to fit, and the caller has to treat them
         * differently:
         *
         *   TOO LONG is not a failure. Gen 3's fields are 10 and 7 characters where a modern one is
         *   12, so a long name is truncated and reported as carried -- every real transfer into an
         *   older generation does this, and PKHeX's SetString caps the same way.
         *
         *   A CHARACTER WITH NO GEN 3 GLYPH is a failure, and it is all-or-nothing. Stopping at it
         *   would store the prefix that happened to map, which is not the trainer's name and not
         *   obviously wrong to anyone reading it -- a Korean OT would land as one or two stray
         *   letters. An empty field is at least honestly empty, and the legality checker already
         *   reports it.
         */
        bool utf16ToG3Name(std::vector<std::byte> &destinationBytes, size_t doff,
                           const std::vector<std::byte> &sourceBytes, size_t soff, int srcBytes, int maxG3,
                           uint8_t languageId)
        {
            // The name is staged here rather than written straight out, so a character with no glyph
            // can discard the whole thing instead of leaving a prefix in the field. Gen 3's widest
            // name field is the 10-character nickname; the cap keeps this honest if that ever moves.
            constexpr int packedNameCapacity = 10;
            uint8_t packedName[packedNameCapacity] = {0};
            const int writableCount = maxG3 < packedNameCapacity ? maxG3 : packedNameCapacity;
            int gen3Index = 0;
            bool everyCharacterMapped = true;
            for (int sourceIndex = 0; sourceIndex + 2 <= srcBytes; sourceIndex += 2)
            {
                const char16_t character = static_cast<char16_t>(rd16(sourceBytes, soff + sourceIndex));
                if (character == 0)
                    break;
                // over the field's width: truncate, which is not a failure
                if (gen3Index >= writableCount) break;
                const uint8_t packedByte = Utils::charToGen3(character, languageId);
                if (packedByte == Utils::GEN3_TERMINATOR)
                {
                    everyCharacterMapped = false;
                    break;
                }
                packedName[gen3Index] = packedByte;
                ++gen3Index;
            }
            if (!everyCharacterMapped)
                gen3Index = 0;
            for (int index = 0; index < gen3Index; ++index)
                wr8(destinationBytes, doff + index, packedName[index]);
            for (int index = gen3Index; index < maxG3; ++index)
                wr8(destinationBytes, doff + index, Utils::GEN3_TERMINATOR); // Gen 3 pads names with 0xFF
            return everyCharacterMapped;
        }

        // Write a UTF-8 string UPPERCASED as a Gen 3 name (0xFF-terminated/padded) -- the species
        // name, for the Gen 3 nickname. Must decode UTF-8 rather than walk bytes: the species table
        // is game-canonical, so five of the names in Gen 3's own range carry a multi-byte character
        // (Nidoran♀/♂, Farfetch'd) and a per-byte walk would encode their continuation bytes as
        // garbage or drop the character. Only ASCII is uppercased; ♀/♂, kana and the accented
        // letters have no case, so the Japanese and European tables pass through untouched.
        void utf8UpperToG3Name(std::vector<std::byte> &destinationBytes, size_t doff, const char *text, int maxG3,
                               uint8_t languageId)
        {
            const std::u16string utf16Text = Utils::utf8ToUtf16(std::string(text));
            int gen3Index = 0;
            for (char16_t character : utf16Text)
            {
                if (gen3Index >= maxG3)
                    break;
                if (character >= u'a' && character <= u'z')
                    character = static_cast<char16_t>(character - u'a' + u'A');
                const uint8_t packedByte = Utils::charToGen3(character, languageId);
                if (packedByte == Utils::GEN3_TERMINATOR)
                    continue;
                wr8(destinationBytes, doff + gen3Index, packedByte);
                ++gen3Index;
            }
            for (; gen3Index < maxG3; ++gen3Index)
                wr8(destinationBytes, doff + gen3Index, Utils::GEN3_TERMINATOR);
        }

        // PK8 -> PK9, in place. `species`/`form` are the source's (for the imported Tera type).
        void transformG8toG9(std::vector<std::byte> &recordBytes, uint16_t species, uint8_t form)
        {
            if (recordBytes.size() < 0x148)
                return;
            // 0x08 species: the PK8 hub stores the NATIONAL dex number, but PK9/PA9 store the Gen 9
            // INTERNAL index (diverges from #917 on) -- convert, or the game shows a shifted species.
            wr16(recordBytes, 0x08, Pokemon::gen9NationalToInternal(rd16(recordBytes, 0x08)));
            // 0x16 bit4 CanGigantamax -> PK9 has no G-Max: clear it.
            wr8(recordBytes, 0x16, rd8(recordBytes, 0x16) & ~0x10);
            // 0x22 gender: PK8 (byte>>2)&3 -> PK9 (byte>>1)&3. Keep Fateful(bit0); drop PK8 Flag2(bit1).
            {
                uint8_t value = rd8(recordBytes, 0x22);
                uint8_t fateful = value & 0x01;
                uint8_t gender = (value >> 2) & 0x03;
                wr8(recordBytes, 0x22, fateful | (gender << 1));
            }
            // 0x48-0x57: PK8 Sociability(0x48) + Height(0x50)/Weight(0x51) -> PK9 Height(0x48)/Weight(0x49)/
            //            Scale(0x4A) + DLC move-record flags(0x4B-0x57). PK8 has no scale -> reuse height.
            {
                uint8_t heightScalar = rd8(recordBytes, 0x50), w = rd8(recordBytes, 0x51);
                zeroRange(recordBytes, 0x48, 0x10);
                wr8(recordBytes, 0x48, heightScalar);
                wr8(recordBytes, 0x49, w);
                wr8(recordBytes, 0x4A, heightScalar);
            }
            // 0x90-0x9F: PK8 DynamaxLevel(0x90)/Status(0x94)/Palma(0x98) -> PK9 Status(0x90)/Tera(0x94,0x95).
            //            Zero, then import a Tera from the species' primary type (Normal falls back to type2).
            {
                zeroRange(recordBytes, 0x90, 0x10);
                // SV, not the source game: the Tera type is being written INTO a PK9, so it has
                // to name a type Scarlet/Violet has (and the source here is PK8 either way).
                Pokemon::TypePair typePair = Pokemon::getPokemonTypes(species, form, Enums::GameVersion::SV);
                uint8_t tera =
                    (typePair.type1 == 0 /*Normal*/ && typePair.type2 != 255) ? typePair.type2 : typePair.type1;
                wr8(recordBytes, 0x94, tera);
                wr8(recordBytes, 0x95, tera);
            }
            // 0xCE-0xF7 Block C: PK8 PokeJob/Version(0xDE)/BattleVer(0xDF)/Language(0xE2)/FormArg(0xE4)/
            //            AffixedRibbon(0xE8) -> PK9 Version(0xCE)/BattleVer(0xCF)/FormArg(0xD0)/Affixed(0xD4)/
            //            Language(0xD5). Capture, wipe the whole block, rewrite at PK9 offsets.
            {
                uint8_t version = rd8(recordBytes, 0xDE), battleVer = rd8(recordBytes, 0xDF),
                        language = rd8(recordBytes, 0xE2), affixed = rd8(recordBytes, 0xE8);
                uint8_t formArgument0 = rd8(recordBytes, 0xE4), f1 = rd8(recordBytes, 0xE5),
                        f2 = rd8(recordBytes, 0xE6), f3 = rd8(recordBytes, 0xE7);
                zeroRange(recordBytes, 0xCE, 0x2A); // 0xCE..0xF7
                wr8(recordBytes, 0xCE, version);
                wr8(recordBytes, 0xCF, battleVer);
                wr8(recordBytes, 0xD0, formArgument0);
                wr8(recordBytes, 0xD1, f1);
                wr8(recordBytes, 0xD2, f2);
                wr8(recordBytes, 0xD3, f3);
                wr8(recordBytes, 0xD4, affixed);
                wr8(recordBytes, 0xD5, language);
            }
            // 0x11F ObedienceLevel (PK9 adds this; PK8 leaves it padding) = MetLevel.
            wr8(recordBytes, 0x11F, rd8(recordBytes, 0x125) & 0x7F);
            // 0x127-0x147: PK8 TR flags(0x127-0x134) + HOME Tracker(0x135) -> PK9 HOME Tracker(0x127) +
            //              move-record base flags(0x12F-0x147). Carry the tracker; drop the record flags.
            {
                uint64_t tracker = rd64(recordBytes, 0x135);
                zeroRange(recordBytes, 0x127, 0x21);
                wr64(recordBytes, 0x127, tracker);
            }
            // 0x156 DynamaxType (PK8 party region) -> PK9 has none. (Bounds-guarded for stored-size buffers.)
            zeroRange(recordBytes, 0x156, 0x02);
        }

        // PK9 -> PK8, in place (the mirror of transformG8toG9). Tera / ObedienceLevel / records are dropped.
        void transformG9toG8(std::vector<std::byte> &recordBytes)
        {
            if (recordBytes.size() < 0x148)
                return;
            // 0x08 species: PK9/PA9 store the Gen 9 INTERNAL index; the PK8 hub (and every format fed
            // from it) uses the NATIONAL dex number -- convert (mirror of transformG8toG9).
            wr16(recordBytes, 0x08, Pokemon::gen9InternalToNational(rd16(recordBytes, 0x08)));
            // 0x22 gender: PK9 (byte>>1)&3 -> PK8 (byte>>2)&3. Keep Fateful(bit0); PK8 Flag2(bit1) stays 0.
            {
                uint8_t value = rd8(recordBytes, 0x22);
                uint8_t fateful = value & 0x01;
                uint8_t gender = (value >> 1) & 0x03;
                wr8(recordBytes, 0x22, fateful | (gender << 2));
            }
            // 0x48-0x57: PK9 Height(0x48)/Weight(0x49)/Scale(0x4A) -> PK8 Sociability(0x48) + Height(0x50)/
            //            Weight(0x51). Capture height/weight, wipe, rewrite; Scale is dropped (no PK8 field).
            {
                uint8_t heightScalar = rd8(recordBytes, 0x48), w = rd8(recordBytes, 0x49);
                zeroRange(recordBytes, 0x48, 0x10);
                wr8(recordBytes, 0x50, heightScalar);
                wr8(recordBytes, 0x51, w);
            }
            // 0x90-0x9F: PK9 Status(0x90)/Tera(0x94,0x95) -> PK8 DynamaxLevel(0x90)/Status(0x94)/Palma.
            //            Tera is dropped; leave Dynamax/Status/Palma zeroed.
            zeroRange(recordBytes, 0x90, 0x10);
            // 0xCE-0xF7 Block C: PK9 Version(0xCE)/BattleVer(0xCF)/FormArg(0xD0)/Affixed(0xD4)/Language(0xD5)
            //            -> PK8 Version(0xDE)/BattleVer(0xDF)/Language(0xE2)/FormArg(0xE4)/Affixed(0xE8).
            {
                uint8_t version = rd8(recordBytes, 0xCE), battleVer = rd8(recordBytes, 0xCF),
                        affixed = rd8(recordBytes, 0xD4), language = rd8(recordBytes, 0xD5);
                uint8_t formArgument0 = rd8(recordBytes, 0xD0), f1 = rd8(recordBytes, 0xD1),
                        f2 = rd8(recordBytes, 0xD2), f3 = rd8(recordBytes, 0xD3);
                zeroRange(recordBytes, 0xCE, 0x2A); // 0xCE..0xF7
                wr8(recordBytes, 0xDE, version);
                wr8(recordBytes, 0xDF, battleVer);
                wr8(recordBytes, 0xE2, language);
                wr8(recordBytes, 0xE4, formArgument0);
                wr8(recordBytes, 0xE5, f1);
                wr8(recordBytes, 0xE6, f2);
                wr8(recordBytes, 0xE7, f3);
                wr8(recordBytes, 0xE8, affixed);
            }
            // 0x11F ObedienceLevel -> PK8 padding.
            wr8(recordBytes, 0x11F, 0);
            // 0x127-0x147: PK9 HOME Tracker(0x127) + record flags(0x12F-0x147) -> PK8 TR flags(0x127-0x134)
            //              + HOME Tracker(0x135). Carry the tracker; TR flags come out zeroed.
            {
                uint64_t tracker = rd64(recordBytes, 0x127);
                zeroRange(recordBytes, 0x127, 0x21);
                wr64(recordBytes, 0x135, tracker);
            }
        }

        // PA8 (0x168 stored / 0x178 party) shares the header + block-A core (EC..pokerus) and the
        // ribbon/mark region at identical offsets with PK8, but its whole Block B/C/D is at different
        // offsets (moves 0x54, nickname 0x60, IV32 0x94, HT 0xB8, Version 0xEE, Language 0xF2, OT 0x110,
        // ball 0x137, ...). So this is a field RELOCATION into a fresh destination-sized buffer, not an in-place
        // fixup. PA8-only data (GVs, Alpha flag/move, move-mastery/purchase records, absolute size) is
        // dropped; PK8<->PK9 legs are handled by composing with transformG8toG9 / transformG9toG8. Offsets
        // per PKHeX PA8.cs / G8PKM.cs. Party stats are recomputed on load, so 0x148+/0x168+ is left zero.
        inline void copyBytes(std::vector<std::byte> &destinationBytes, size_t doff,
                              const std::vector<std::byte> &sourceBytes, size_t soff, size_t byteCount)
        {
            for (size_t index = 0;
                 index < byteCount && (soff + index) < sourceBytes.size() && (doff + index) < destinationBytes.size();
                 ++index)
                destinationBytes[doff + index] = sourceBytes[soff + index];
        }

        // PA8 -> PK8 layout (SwSh/BDSP), returns a 0x148 stored buffer.
        std::vector<std::byte> remapPA8toPK8(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x148, std::byte{0});
            // EC..pokerus/pad (species/item/id/exp/ability/PID/nature/EVs/contest); gender byte 0x22,
            // which shares the PK8 bit layout
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x34);
            copyBytes(destinationBytes, 0x34, sourceBytes, 0x34, 0x0A);   // ribbons u32 x2 + ribbon counts (0x34..0x3D)
            // marks (0x40..0x47); skips PA8 AlphaMove at 0x3E-0x3F
            copyBytes(destinationBytes, 0x40, sourceBytes, 0x40, 0x08);
            copyBytes(destinationBytes, 0x50, sourceBytes, 0x50, 0x02);   // Height + Weight scalars (shared 0x50/0x51)
            // clear PA8-only 0x16 bit5 (IsAlpha) / bit6 (IsNoble)
            wr8(destinationBytes, 0x16, rd8(destinationBytes, 0x16) & ~0x60);
            copyBytes(destinationBytes, 0x72, sourceBytes, 0x54, 0x08);   // Move 1-4
            copyBytes(destinationBytes, 0x7A, sourceBytes, 0x5C, 0x04);   // Move PP
            copyBytes(destinationBytes, 0x7E, sourceBytes, 0x86, 0x04);   // Move PP-Ups
            copyBytes(destinationBytes, 0x82, sourceBytes, 0x8A, 0x08);   // Relearn 1-4
            copyBytes(destinationBytes, 0x58, sourceBytes, 0x60, 0x1A);   // Nickname (26 bytes)
            copyBytes(destinationBytes, 0x8C, sourceBytes, 0x94, 0x04);   // IV32
            copyBytes(destinationBytes, 0xA8, sourceBytes, 0xB8, 0x1A);   // HT name (26 bytes)
            // HT gender / HT language / CurrentHandler (0xD2..0xD4 -> 0xC2..0xC4)
            copyBytes(destinationBytes, 0xC2, sourceBytes, 0xD2, 0x03);
            // HT friendship + HT memory (0xD8..0xDD -> 0xC8..0xCD)
            copyBytes(destinationBytes, 0xC8, sourceBytes, 0xD8, 0x06);
            copyBytes(destinationBytes, 0xDE, sourceBytes, 0xEE, 0x02);   // Version + BattleVersion
            copyBytes(destinationBytes, 0xE2, sourceBytes, 0xF2, 0x01);   // Language
            copyBytes(destinationBytes, 0xE4, sourceBytes, 0xF4, 0x04);   // FormArgument (u32)
            copyBytes(destinationBytes, 0xE8, sourceBytes, 0xF8, 0x01);   // AffixedRibbon
            copyBytes(destinationBytes, 0xF8, sourceBytes, 0x110, 0x1A);  // OT name (26 bytes)
            // OT friendship + OT memory (0x12A..0x130 -> 0x112..0x118)
            copyBytes(destinationBytes, 0x112, sourceBytes, 0x12A, 0x07);
            // egg date + met date (0x131..0x136 -> 0x119..0x11E)
            copyBytes(destinationBytes, 0x119, sourceBytes, 0x131, 0x06);
            copyBytes(destinationBytes, 0x120, sourceBytes, 0x138, 0x04); // egg location + met location (u16 each)
            // Ball rides the hub VERBATIM. Clamping the Hisui balls away here applied Sword/Shield's
            // rule to every destination, and Sword/Shield is the only destination that has it.
            copyBytes(destinationBytes, 0x124, sourceBytes, 0x137, 0x01); // Ball
            copyBytes(destinationBytes, 0x125, sourceBytes, 0x13D, 0x01); // MetLevel(bits0-6) + OTGender(bit7)
            copyBytes(destinationBytes, 0x126, sourceBytes, 0x13E, 0x01); // HyperTrain flags
            copyBytes(destinationBytes, 0x135, sourceBytes, 0x14D, 0x08); // HOME Tracker (u64)
            return destinationBytes;
        }

        // PK8 layout (SwSh/BDSP) -> PA8, returns a 0x178 party buffer. Mirror of remapPA8toPK8; the
        // PA8-only fields (GVs, Alpha/Noble, AlphaMove, move-mastery, absolute height/weight) stay zero,
        // so the result is a well-formed non-Alpha Hisui entity carrying the source's identity + moves.
        std::vector<std::byte> remapPK8toPA8(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x178, std::byte{0});
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x34);   // EC..pokerus/pad
            copyBytes(destinationBytes, 0x34, sourceBytes, 0x34, 0x0A);   // ribbons + counts
            copyBytes(destinationBytes, 0x40, sourceBytes, 0x40, 0x08);   // marks
            copyBytes(destinationBytes, 0x50, sourceBytes, 0x50, 0x02);   // Height + Weight scalars
            // PA8 Scale := Height (PK8/PK9 scale was dropped en route)
            wr8(destinationBytes, 0x52, rd8(sourceBytes, 0x50));
            // clear PK8-only 0x16 bit4 (CanGigantamax)
            wr8(destinationBytes, 0x16, rd8(destinationBytes, 0x16) & ~0x10);
            copyBytes(destinationBytes, 0x54, sourceBytes, 0x72, 0x08);   // Move 1-4
            copyBytes(destinationBytes, 0x5C, sourceBytes, 0x7A, 0x04);   // Move PP
            copyBytes(destinationBytes, 0x86, sourceBytes, 0x7E, 0x04);   // Move PP-Ups
            copyBytes(destinationBytes, 0x8A, sourceBytes, 0x82, 0x08);   // Relearn 1-4
            copyBytes(destinationBytes, 0x60, sourceBytes, 0x58, 0x1A);   // Nickname
            copyBytes(destinationBytes, 0x94, sourceBytes, 0x8C, 0x04);   // IV32
            copyBytes(destinationBytes, 0xB8, sourceBytes, 0xA8, 0x1A);   // HT name
            copyBytes(destinationBytes, 0xD2, sourceBytes, 0xC2, 0x03);   // HT gender / language / CurrentHandler
            copyBytes(destinationBytes, 0xD8, sourceBytes, 0xC8, 0x06);   // HT friendship + memory
            copyBytes(destinationBytes, 0xEE, sourceBytes, 0xDE, 0x02);   // Version + BattleVersion
            copyBytes(destinationBytes, 0xF2, sourceBytes, 0xE2, 0x01);   // Language
            copyBytes(destinationBytes, 0xF4, sourceBytes, 0xE4, 0x04);   // FormArgument
            copyBytes(destinationBytes, 0xF8, sourceBytes, 0xE8, 0x01);   // AffixedRibbon
            copyBytes(destinationBytes, 0x110, sourceBytes, 0xF8, 0x1A);  // OT name
            copyBytes(destinationBytes, 0x12A, sourceBytes, 0x112, 0x07); // OT friendship + memory
            copyBytes(destinationBytes, 0x131, sourceBytes, 0x119, 0x06); // egg + met dates
            copyBytes(destinationBytes, 0x138, sourceBytes, 0x120, 0x04); // egg + met locations
            copyBytes(destinationBytes, 0x137, sourceBytes, 0x124, 0x01); // Ball (PK8 balls are valid in PA8 -> keep)
            copyBytes(destinationBytes, 0x13D, sourceBytes, 0x125, 0x01); // MetLevel + OTGender
            copyBytes(destinationBytes, 0x13E, sourceBytes, 0x126, 0x01); // HyperTrain flags
            copyBytes(destinationBytes, 0x14D, sourceBytes, 0x135, 0x08); // HOME Tracker
            return destinationBytes;
        }

        // PB7 is a 260-byte Gen-7-format record (Gen6/7 crypto handled by the ctor/encryptFor). Almost
        // every field is at a different offset than PK8, AND several are re-packed (PB7 ability is a u8,
        // gender+form share byte 0x1D, PB7 has no mint so PK8 StatNature := Nature). Stat-training is NOT
        // cross-converted: neither AVs (PB7 0x24-0x29) nor EVs (0x1E-0x23) are carried, so a pokemon entering
        // LGPE gets AVs=0 and a pokemon leaving gets EVs=0 (per product spec -- the UI asks the user to ack it).
        // PB7-only data (AVs, CP, Height/WeightAbsolute, Spirit/Mood) is dropped; CP + party stats are
        // recomputed on load. Offsets per PKHeX PB7.cs. Ribbons differ in encoding and are dropped.

        // PB7 -> PK8 layout (SwSh/BDSP), returns a 0x148 stored buffer.
        std::vector<std::byte> remapPB7toPK8(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x148, std::byte{0});
            // EC + Sanity + Checksum (checksum recomputed later)
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x08);
            // Species / HeldItem / ID32 / EXP (0x08..0x13 shared)
            copyBytes(destinationBytes, 0x08, sourceBytes, 0x08, 0x0C);
            // Ability (PB7 u8 -> PK8 u16 low byte; high byte stays 0)
            wr8(destinationBytes, 0x14, rd8(sourceBytes, 0x14));
            // AbilityNumber (PB7 0x15 bits0-2 -> PK8 0x16 bits0-2)
            wr8(destinationBytes, 0x16, rd8(sourceBytes, 0x15) & 0x07);
            copyBytes(destinationBytes, 0x18, sourceBytes, 0x16, 0x02); // MarkingValue (PB7 0x16 -> PK8 0x18)
            copyBytes(destinationBytes, 0x1C, sourceBytes, 0x18, 0x04); // PID (PB7 0x18 -> PK8 0x1C)
            wr8(destinationBytes, 0x20, rd8(sourceBytes, 0x1C));        // Nature (PB7 0x1C -> PK8 0x20)
            wr8(destinationBytes, 0x21, rd8(sourceBytes, 0x1C));        // StatNature := Nature (LGPE has no mint)
            {
                uint8_t packedByte = rd8(sourceBytes, 0x1D);
                uint8_t fateful = packedByte & 0x01, gender = (packedByte >> 1) & 0x03, form = packedByte >> 3;
                wr8(destinationBytes, 0x22, fateful | (gender << 2)); // PK8 0x22: Fateful(bit0) + Gender(bits2-3)
                wr8(destinationBytes, 0x24, form);
            } // PK8 0x24: Form
            // EVs (0x1E-0x23) + AVs (0x24-0x29) intentionally NOT carried -> PK8 EVs stay 0.
            wr8(destinationBytes, 0x32, rd8(sourceBytes, 0x2B));         // PokerusState (PB7 0x2B -> PK8 0x32)
            // Height + Weight scalars (PB7 0x3A/0x3B -> PK8 0x50/0x51)
            copyBytes(destinationBytes, 0x50, sourceBytes, 0x3A, 0x02);
            copyBytes(destinationBytes, 0xE4, sourceBytes, 0x3C, 0x04);  // FormArgument (PB7 0x3C -> PK8 0xE4)
            copyBytes(destinationBytes, 0x58, sourceBytes, 0x40, 0x1A);  // Nickname
            copyBytes(destinationBytes, 0x72, sourceBytes, 0x5A, 0x08);  // Moves
            copyBytes(destinationBytes, 0x7A, sourceBytes, 0x62, 0x04);  // Move PP
            copyBytes(destinationBytes, 0x7E, sourceBytes, 0x66, 0x04);  // Move PP-Ups
            copyBytes(destinationBytes, 0x82, sourceBytes, 0x6A, 0x08);  // Relearn
            copyBytes(destinationBytes, 0x8C, sourceBytes, 0x74, 0x04);  // IV32
            copyBytes(destinationBytes, 0xA8, sourceBytes, 0x78, 0x1A);  // HT name
            wr8(destinationBytes, 0xC2, rd8(sourceBytes, 0x92));         // HT gender
            wr8(destinationBytes, 0xC4, rd8(sourceBytes, 0x93));         // CurrentHandler
            wr8(destinationBytes, 0xC8, rd8(sourceBytes, 0xA2));         // HT friendship
            copyBytes(destinationBytes, 0xC9, sourceBytes, 0xA4, 0x03);  // HT memory (intensity/memory/feeling)
            copyBytes(destinationBytes, 0xCC, sourceBytes, 0xA8, 0x02);  // HT text var
            copyBytes(destinationBytes, 0xF8, sourceBytes, 0xB0, 0x1A);  // OT name
            wr8(destinationBytes, 0x112, rd8(sourceBytes, 0xCA));        // OT friendship
            // egg date (0xD1-0xD3) + met date (0xD4-0xD6) -> 0x119..0x11E
            copyBytes(destinationBytes, 0x119, sourceBytes, 0xD1, 0x06);
            copyBytes(destinationBytes, 0x120, sourceBytes, 0xD8, 0x04); // egg location + met location
            wr8(destinationBytes, 0x124, rd8(sourceBytes, 0xDC));        // Ball (LGPE balls are <=26 -> valid in PK8)
            wr8(destinationBytes, 0x125, rd8(sourceBytes, 0xDD));        // MetLevel(bits0-6) + OTGender(bit7)
            wr8(destinationBytes, 0x126, rd8(sourceBytes, 0xDE));        // HyperTrain flags
            wr8(destinationBytes, 0xDE, rd8(sourceBytes, 0xDF));         // Version (PB7 0xDF -> PK8 0xDE)
            wr8(destinationBytes, 0xE2, rd8(sourceBytes, 0xE3));         // Language (PB7 0xE3 -> PK8 0xE2)
            return destinationBytes;
        }

        // PK8 layout -> PB7 (Let's Go), returns a 260-byte record. Mirror of remapPB7toPK8. AVs + EVs are
        // left 0 (stat-training reset), CP + party stats are recomputed by the caller, PB7-only extras stay 0.
        std::vector<std::byte> remapPK8toPB7(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x104, std::byte{0}); // PB7 = 260 bytes
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x08);             // EC + Sanity + Checksum
            copyBytes(destinationBytes, 0x08, sourceBytes, 0x08, 0x0C);             // Species / HeldItem / ID32 / EXP
            // Ability (PK8 u16 low byte -> PB7 u8; LGPE ability ids < 256)
            wr8(destinationBytes, 0x14, rd8(sourceBytes, 0x14));
            wr8(destinationBytes, 0x15, rd8(sourceBytes, 0x16) & 0x07); // AbilityNumber (PK8 0x16 -> PB7 0x15)
            copyBytes(destinationBytes, 0x16, sourceBytes, 0x18, 0x02); // MarkingValue (PK8 0x18 -> PB7 0x16)
            copyBytes(destinationBytes, 0x18, sourceBytes, 0x1C, 0x04);             // PID
            wr8(destinationBytes, 0x1C, rd8(sourceBytes, 0x20));                    // Nature
            {
                uint8_t genderValue = rd8(sourceBytes, 0x22);
                uint8_t fateful = genderValue & 0x01, gender = (genderValue >> 2) & 0x03, form = rd8(sourceBytes, 0x24);
                wr8(destinationBytes, 0x1D, fateful | (gender << 1) | (form << 3));
            } // PB7 0x1D: Fateful|Gender(bits1-2)|Form(bits3+)
            // EVs (0x1E-0x23) + AVs (0x24-0x29) deliberately left 0 -> entering LGPE resets stat training.
            wr8(destinationBytes, 0x2B, rd8(sourceBytes, 0x32));         // PokerusState
            copyBytes(destinationBytes, 0x3A, sourceBytes, 0x50, 0x02);  // Height + Weight scalars
            copyBytes(destinationBytes, 0x3C, sourceBytes, 0xE4, 0x04);  // FormArgument
            copyBytes(destinationBytes, 0x40, sourceBytes, 0x58, 0x1A);  // Nickname
            copyBytes(destinationBytes, 0x5A, sourceBytes, 0x72, 0x08);  // Moves
            copyBytes(destinationBytes, 0x62, sourceBytes, 0x7A, 0x04);  // Move PP
            copyBytes(destinationBytes, 0x66, sourceBytes, 0x7E, 0x04);  // Move PP-Ups
            copyBytes(destinationBytes, 0x6A, sourceBytes, 0x82, 0x08);  // Relearn
            copyBytes(destinationBytes, 0x74, sourceBytes, 0x8C, 0x04);  // IV32
            copyBytes(destinationBytes, 0x78, sourceBytes, 0xA8, 0x1A);  // HT name
            wr8(destinationBytes, 0x92, rd8(sourceBytes, 0xC2));         // HT gender
            wr8(destinationBytes, 0x93, rd8(sourceBytes, 0xC4));         // CurrentHandler
            wr8(destinationBytes, 0xA2, rd8(sourceBytes, 0xC8));         // HT friendship
            copyBytes(destinationBytes, 0xA4, sourceBytes, 0xC9, 0x03);  // HT memory
            copyBytes(destinationBytes, 0xA8, sourceBytes, 0xCC, 0x02);  // HT text var
            copyBytes(destinationBytes, 0xB0, sourceBytes, 0xF8, 0x1A);  // OT name
            wr8(destinationBytes, 0xCA, rd8(sourceBytes, 0x112));        // OT friendship
            copyBytes(destinationBytes, 0xD1, sourceBytes, 0x119, 0x06); // egg + met dates
            copyBytes(destinationBytes, 0xD8, sourceBytes, 0x120, 0x04); // egg + met locations
            wr8(destinationBytes, 0xDC, rd8(sourceBytes, 0x124));        // Ball
            wr8(destinationBytes, 0xDD, rd8(sourceBytes, 0x125));        // MetLevel + OTGender
            wr8(destinationBytes, 0xDE, rd8(sourceBytes, 0x126));        // HyperTrain flags
            wr8(destinationBytes, 0xDF, rd8(sourceBytes, 0xDE));         // Version
            wr8(destinationBytes, 0xE3, rd8(sourceBytes, 0xE2));         // Language
            // LGPE has no held-item mechanic (PKHeX likewise drops held items entering LGPE) -> drop it.
            // Illegal / nonexistent moves are cleared by the general moveset sanitizer in convert().
            wr8(destinationBytes, 0x0A, 0);
            wr8(destinationBytes, 0x0B, 0);
            // Absolute height/weight (0x2C/0xE4 floats): LGPE displays these; PK8/PK9 store only the
            // scalar and compute the absolute on the fly. Recompute from the carried scalars + the
            // species/form base size, per PKHeX PB7.GetHeightAbsolute / GetWeightAbsolute.
            {
                const uint16_t species = static_cast<uint16_t>(rd8(destinationBytes, 0x08)) |
                                         (static_cast<uint16_t>(rd8(destinationBytes, 0x09)) << 8);
                const uint8_t form =
                    static_cast<uint8_t>(rd8(destinationBytes, 0x1D) >> 3); // PB7 packs form in 0x1D bits 3+
                const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(species, form);
                const float heightRatio =
                    (rd8(destinationBytes, 0x3A) / 255.0f) * 0.79999995f + 0.6f; // height ratio (+40% / -20%)
                const float weightRatio =
                    (rd8(destinationBytes, 0x3B) / 255.0f) * 0.40000004f + 0.8f; // weight ratio (+/- 20%)
                wrf32(destinationBytes, 0x2C, heightRatio * static_cast<float>(pi.height));            // HeightAbsolute
                // WeightAbsolute
                wrf32(destinationBytes, 0xE4, heightRatio * weightRatio * static_cast<float>(pi.weight));
            }
            return destinationBytes;
        }

        // The ONE official way out of Red/Blue/Yellow and Gold/Silver/Crystal. The Virtual Console
        // games hand a copy to Poke Transporter, which builds a Gen 7 record and drops it into Bank;
        // there has never been a route to any other generation, which is why PKSE runs this step
        // first and then lets the ordinary PK8 hub carry the result onward.
        //
        // Most of this is a RULE rather than a copy. A PK1 has no nature, no ability, no PID, no
        // ball, no met data and no origin game, so Transporter invents all of them to fixed recipes
        // (PKHeX PK1.ConvertToPK7 / PK2.ConvertToPK7):
        //
        //   nature   = EXP % 25, taken from the ORIGINAL EXP before it is rounded down below
        //   EXP      = the minimum for the level it arrives at -- partial EXP is lost
        //   ability  = the HIDDEN ability, bar the handful of species Bank refuses it to
        //   IVs      = rerolled, three guaranteed 31s (five for Mew and Celebi)
        //   ball     = Poke Ball; met level = the current level; met location 30013 / 30017
        //   EVs      = 0. Gen 1/2 Stat Experience does not become EVs; it is dropped.
        //   nickname = the species name, unless the stored one differs from it
        //
        // What it does NOT invent is shininess: the Gen 1/2 DV pattern has no counterpart in a PK7,
        // so the FACT is preserved by forcing the rerolled PID shiny or not shiny to match.
        //
        // THREE THINGS PKSE CANNOT DO THAT TRANSPORTER COULD, all for want of data rather than code:
        //  1. The ORIGIN GAME is a guess. A PK1 records no version and neither does a Gen 1 save, so
        //     Red/Blue/Green and Gold/Silver are indistinguishable; the group's representative title
        //     is stamped and the UI says so before the transfer runs. Gen 2 is the one exception --
        //     a record carrying Crystal's caught data names Crystal outright.
        //  2. The substituted in-game-trade OT is only half localised. The species name an
        //     un-nicknamed Pokemon arrives with is taken in the RECORD's language, but the trade OT
        //     is トレーナー for a Japanese record and the English "Trainer" for every other language,
        //     where Bank would write that language's own word.
        //     A name that IS carried (a real nickname, an ordinary OT) goes through PKSE's own Gen
        //     1/2 character table rather than Bank's transporter one, which differs for Japanese:
        //     PKHeX's StringConverter12Transporter shifts katakana to hiragana when a name mixes
        //     the two, and PKSE writes what its table decodes.
        //  3. The 3DS locale block (Country / Region / ConsoleRegion, 0xE0-0xE2) stays 0. PKSE reads
        //     no console region off the receiving save, and inventing one would be worse than blank.
        //
        // A GEN 2 EGG ARRIVES HATCHED. Nothing here sets the egg bit, which is PKHeX's answer too --
        // its converter calls ForceHatchPKM() on any egg before the format change. The met data is
        // then the ordinary transfer's, so the result is a legitimate Pokemon rather than an egg
        // carrying a Gen 7 met location it could never have had.

        // A small deterministic 32-bit stream (SplitMix64). Transporter drew the PID, the encryption
        // constant and the IV placement from a real RNG. PKSE seeds this from the SOURCE RECORD'S OWN
        // BYTES instead, so the same Pokemon always transfers to the same Pokemon: a conversion that
        // answered differently every time could not be tested, and a user who withdrew a pokemon, undid
        // it and withdrew again would get a different Pokemon each time.
        class TransferRandom
        {
        public:
            explicit TransferRandom(uint64_t seed) noexcept : state(seed) {}
            uint32_t next() noexcept
            {
                state += 0x9E3779B97F4A7C15ull;
                uint64_t mixed = state;
                mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
                mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
                return static_cast<uint32_t>((mixed ^ (mixed >> 31)) >> 32);
            }
            /// Uniform enough for a PID and an IV spread; `bound` is never near 2^32 here.
            uint32_t below(uint32_t bound) noexcept { return bound == 0 ? 0 : next() % bound; }

        private:
            uint64_t state;
        };

        uint64_t recordSeed(const Pokemon::Pokemon &source)
        {
            uint64_t hash = 0xCBF29CE484222325ull; // FNV-1a 64
            const std::span<const std::byte> bytes = source.getData();
            for (size_t index = 0; index < bytes.size(); ++index)
            {
                hash ^= static_cast<uint8_t>(bytes[index]);
                hash *= 0x100000001B3ull;
            }
            return hash;
        }

        /// Writes a Gen 6/7 name field: `unitCount` UTF-16LE code units, zero-filled past the text.
        void writeUtf16Field(std::vector<std::byte> &destinationBytes, size_t offset, size_t unitCount,
                             const std::u16string &text)
        {
            Utils::setString(reinterpret_cast<uint8_t *>(destinationBytes.data()) + offset, unitCount * 2, text,
                             unitCount - 1);
        }

        /// Species Bank will not hand its hidden ability to. Gen 2 adds Misdreavus, Unown and Celebi
        /// to Gen 1's list (PKHeX TransporterLogic.IsHiddenDisallowedVC1 / VC2).
        bool hiddenAbilityDisallowedOnTransfer(uint16_t species, bool fromGen2)
        {
            switch (species)
            {
            case 92:  // Gastly
            case 93:  // Haunter
            case 94:  // Gengar
            case 109: // Koffing
            case 110: // Weezing
            case 151: // Mew
                return true;
            case 200: // Misdreavus
            case 201: // Unown
            case 251: // Celebi
                return fromGen2;
            default:
                return false;
            }
        }

        // Builds the PK7 a Transporter run would produce. Returns a 260-byte PARTY record; the caller
        // fills its stat tail with recalculateStats(). `originVersion` is the Gen 1/2 title to stamp
        // (see virtualConsoleOriginVersion, and note 1 above).
        std::vector<std::byte> transportVirtualConsoleToPK7(const Pokemon::Pokemon &source, uint8_t originVersion,
                                                            const ReceivingTrainer *receivingTrainer)
        {
            const bool fromGen2 = source.getGameGroup() == GameVersion::GSC;
            const uint16_t species = source.speciesID();
            const uint8_t form = source.formID();
            const Pokemon::PersonalRecord &gen7Personal = Pokemon::getPersonalInfo7USUM(species, form);

            std::vector<std::byte> destinationBytes(Encryption::SIZE_PARTY7_SM, std::byte{0});
            TransferRandom random(recordSeed(source));

            // Mew and Celebi are the event-only pair: five guaranteed 31s and a fateful-encounter
            // flag instead of a carried nickname.
            const bool isEventPair = (species == 151) || (fromGen2 && species == 251);

            // EXP is knocked back to the floor of the level the Pokemon arrives at, so a pokemon partway
            // to its next level loses that progress. The nature is read from the EXP it had BEFORE
            // that -- read it after and every transferred Pokemon would share a handful of natures.
            const uint32_t sourceExperience = source.exp();
            const uint8_t level = Pokemon::getLevelFromExp(sourceExperience, gen7Personal.growthRate);

            wr32(destinationBytes, 0x00, random.next()); // EncryptionConstant
            wr16(destinationBytes, 0x08, species);                                                    // Species
            wr16(destinationBytes, 0x0C, source.tid16()); // TID16 (Gen 1/2 have no SID)
            wr32(destinationBytes, 0x10, Pokemon::getExpForLevel(level, gen7Personal.growthRate));    // EXP
            wr8(destinationBytes, 0x1C, static_cast<uint8_t>(sourceExperience % 25));                 // Nature

            // Bank hands almost everything its HIDDEN ability -- the well-known Virtual Console
            // quirk, and the reason a transferred Gengar is a Cursed Body Gengar. A species with no
            // hidden ability in the table falls back to slot 1 rather than writing ability 0.
            const bool useFirstAbility =
                hiddenAbilityDisallowedOnTransfer(species, fromGen2) || gen7Personal.abilityHidden == 0;
            wr8(destinationBytes, 0x14,
                static_cast<uint8_t>(useFirstAbility ? gen7Personal.ability1 : gen7Personal.abilityHidden));
            wr8(destinationBytes, 0x15, useFirstAbility ? 1 : 4); // AbilityNumber: 1 = slot 1, 4 = hidden

            // FatefulEncounter(bit0) + Gender(bits1-2) + Form(bits3+). Gen 1 has no gender field at
            // all -- gender() derives it from the Attack DV against the species ratio, which is what
            // the games themselves do on transfer -- and no forms; Gen 2's only form is Unown's
            // letter, likewise derived from the DVs.
            wr8(destinationBytes, 0x1D,
                static_cast<uint8_t>((isEventPair ? 1 : 0) | ((source.gender() & 3) << 1) | (form << 3)));

            // IVs are rerolled, not carried: a Gen 1/2 DV is 4 bits and also drives the shininess
            // pattern, so there is no honest widening of it. Three stats come out flawless (five for
            // the event pair) and which three is shuffled, exactly as Transporter did it.
            {
                int individualValues[6];
                for (int index = 0; index < 6; ++index)
                    individualValues[index] = static_cast<int>(random.below(32));
                const int flawlessCount = isEventPair ? 5 : 3;
                for (int index = 0; index < flawlessCount; ++index)
                    individualValues[index] = 31;
                for (int index = 5; index > 0; --index)
                {
                    const int swapWith = static_cast<int>(random.below(static_cast<uint32_t>(index) + 1));
                    const int held = individualValues[index];
                    individualValues[index] = individualValues[swapWith];
                    individualValues[swapWith] = held;
                }
                uint32_t packedIVs = 0;
                for (int index = 0; index < 6; ++index)
                    packedIVs |= static_cast<uint32_t>(individualValues[index] & 0x1F) << (index * 5);
                // Bits 0-29 are the six IVs in HP/ATK/DEF/SPE/SPA/SPD order. Bit 30 (isEgg) stays
                // clear -- see the egg note above -- and bit 31 (isNicknamed) is set further down,
                // once it is known whether the stored nickname is being carried.
                wr32(destinationBytes, 0x74, packedIVs);
            }

            // Shininess is the one Gen 1/2 trait with no field to carry it: it is a DV pattern there
            // and a PID/ID xor here. Reroll the PID, then force it to agree with what the source was.
            {
                const uint16_t trainerId = source.tid16();
                uint32_t pid = random.next();
                auto pidIsShiny = [trainerId](uint32_t value)
                {
                    return static_cast<uint16_t>((value >> 16) ^ (value & 0xFFFF) ^ trainerId) < 16;
                };
                const bool wantShiny = source.isShiny(source.id32(), "");
                if (wantShiny && !pidIsShiny(pid))
                {
                    const uint32_t low = pid & 0xFFFFu; // square-shiny: the high half mirrors the low
                    pid = ((low ^ trainerId) << 16) | low;
                }
                else if (!wantShiny && pidIsShiny(pid))
                {
                    pid ^= 0x10000000u;
                }
                wr32(destinationBytes, 0x18, pid);
            }

            // Moves ride across untouched, PP-Ups included, and the PP is healed to the Gen 7 maximum
            // rather than carried -- Gen 1's PP byte packs the PP-Ups into its top bits and Bank's own
            // conversion of it was buggy, which PKHeX declines to reproduce.
            for (int slot = 0; slot < 4; ++slot)
            {
                uint16_t moveId = source.move(slot);
                // Dizzy Punch is the single move Transporter refuses to carry out of Gen 2.
                if (fromGen2 && moveId == 146)
                    moveId = 0;
                const uint8_t powerPointUps = moveId == 0 ? 0 : source.movePPUps(slot);
                wr16(destinationBytes, 0x5A + slot * 2, moveId);
                wr8(destinationBytes, 0x66 + slot, powerPointUps);
                wr8(destinationBytes, 0x62 + slot,
                    moveId == 0 ? 0 : Names::getMoveMaxPP(moveId, powerPointUps, GameVersion::SM));
            }

            // The record's own language, which both name fields below are written in. A Game Boy
            // save states it by its LAYOUT rather than a field -- a Japanese one has narrower name
            // fields -- and the entity reports what that layout said.
            const uint8_t language = source.language();

            // Nicknames. An un-nicknamed Pokemon arrives with its species name, and the event pair
            // arrives with theirs whatever they were called -- PKHeX takes the fateful branch INSTEAD
            // of the nickname branch, not as well as it.
            //
            // The species name is taken in the RECORD's language, not the display one: a Japanese
            // Pikachu that was never nicknamed is ピカチュウ, and calling it "Pikachu" would both
            // read wrong and make it look deliberately nicknamed to anything comparing the two.
            const bool keepNickname = !isEventPair && source.isNicknamed();
            writeUtf16Field(destinationBytes, 0x40, 13,
                            keepNickname ? source.nickname()
                                         : Utils::utf8ToUtf16(Names::getSpeciesNameLocalized(
                                               source.speciesID(),
                                               Names::languageIndexFor(static_cast<Enums::LanguageID>(language)))));
            if (keepNickname)
                // IsNicknamed rides IV32 bit 31
                wr32(destinationBytes, 0x74, rd32(destinationBytes, 0x74) | 0x80000000u);

            // OT. A Pokemon received in an in-game trade stores a MARKER byte rather than a name, and
            // Bank substitutes the localised "Trainer" for it -- carrying the marker across would put
            // a lone '*' in the OT field.
            bool inGameTradeOT = false;
            if (fromGen2)
                inGameTradeOT = static_cast<const Pokemon::Pokemon2GSC &>(source).isInGameTradeOT();
            else
                inGameTradeOT = static_cast<const Pokemon::Pokemon1RBY &>(source).isInGameTradeOT();
            std::u16string trainerName = source.otName();
            if (inGameTradeOT)
                trainerName = (language == 1) ? std::u16string(u"トレーナー") : std::u16string(u"Trainer");
            writeUtf16Field(destinationBytes, 0xB0, 13, trainerName);

            // Gold and Silver record no trainer gender; only Crystal does, in its caught data. Gen 1
            // is male-only, so 0 is a fact there rather than a default.
            const uint8_t trainerGender = fromGen2 ? source.otGender() : 0;

            wr8(destinationBytes, 0xCA, gen7Personal.baseFriendship);  // OT friendship, reset to base
            wr8(destinationBytes, 0xA2, gen7Personal.baseFriendship);  // HT friendship, likewise
            wr16(destinationBytes, 0xDA, fromGen2 ? 30017 : 30013);    // Met location: "Johto" / "Kanto"
            wr8(destinationBytes, 0xDC, 4);                            // Ball: Poke Ball
            // MetLevel + OTGender
            wr8(destinationBytes, 0xDD, static_cast<uint8_t>((level & 0x7F) | ((trainerGender & 1) << 7)));
            wr8(destinationBytes, 0xDF, originVersion);                // Origin game
            wr8(destinationBytes, 0xE3, language);                     // Language

            // Met date. The 3DS stamped the day the transfer ran, so PKSE stamps the day it runs --
            // the same thing the Gen 3 remap does, and for the same reason: leaving it 0 reads as
            // "met 00/00/2000" in every destination.
            {
                const std::time_t nowT = std::time(nullptr);
                if (const std::tm *localTime = std::localtime(&nowT))
                {
                    int year = localTime->tm_year + 1900;
                    // the met-year byte is (year - 2000) and must never go negative
                    if (year < 2000) year = 2000;
                    wr8(destinationBytes, 0xD4, static_cast<uint8_t>(year - 2000));
                    wr8(destinationBytes, 0xD5, static_cast<uint8_t>(localTime->tm_mon + 1));
                    wr8(destinationBytes, 0xD6, static_cast<uint8_t>(localTime->tm_mday));
                }
            }

            // The handling trainer is whoever is receiving the Pokemon: it arrives through Bank,
            // which is a trade, so the receiving player takes charge of it and the Gen 6/7 trade
            // memory says where. When the caller cannot name that trainer -- any code path with no
            // save behind it -- the pokemon arrives with NO handler rather than an invented
            // one. An empty HT name means "never traded" in a format that has the field, which is a
            // readable state; a made-up name is not.
            if (receivingTrainer != nullptr && !receivingTrainer->name.empty())
            {
                writeUtf16Field(destinationBytes, 0x78, 13, receivingTrainer->name);
                wr8(destinationBytes, 0x92, receivingTrainer->gender & 1); // HT gender
                wr8(destinationBytes, 0x93, 1);                           // CurrentHandler: the HT is in charge
                wr8(destinationBytes, 0xA4, 1);                           // HT memory intensity
                wr8(destinationBytes, 0xA5, 4);                           // "Link trade to [general location]"
                wr8(destinationBytes, 0xA6, static_cast<uint8_t>(random.below(10))); // feeling, 0-9 for a Bank trade
                wr16(destinationBytes, 0xA8, 0);                          // memory variable: "somewhere" (Bank)
            }
            return destinationBytes;
        }

        // PK7 is the 3DS record: 232 stored / 260 party, four 0x38 blocks, Gen 6/7 crypto (the ctor
        // and encryptFor handle that). It is the shape Let's Go's PB7 was cut down from, so most of
        // this reads like the PB7 remap above -- but PK7 is the FULL record and carries everything
        // PB7 dropped: the held item, EVs, contest stats, both trainers' memories, and the ribbon
        // memory counts.
        //
        // Four regions are Gen-7-only and are DROPPED rather than guessed at:
        //   - Super Training (0x2C-0x2F, 0x72) and the Poke Pelago resort state (0x2A). Gen 8 has
        //     neither mechanic.
        //   - Geolocation (0x94-0x9D) plus Country/Region/ConsoleRegion (0xE0-0xE2). Gen 8 removed
        //     the whole 3DS locale block; there is nowhere to put it.
        //   - Affection (OT 0xCB, HT 0xA3). Gen 8 dropped affection into the LGPE-style friendship.
        //   - Ribbons (0x30-0x37). Gen 8 REORDERED the ribbon bits and added marks, so the two
        //     blocks are not a byte copy and a wrong one would hand out ribbons nobody earned. The
        //     PB7 remap drops them for the same reason.
        //
        // Height/weight scalars (PK8 0x50/0x51) stay 0: Gen 8 introduced them and a PK7 has no such
        // field, so there is nothing to carry. That matches what the Gen 3 remap already does.
        // Offsets: PKHeX PK7.cs / G8PKM.cs.

        // PK7 -> PK8 layout (SwSh/BDSP), returns a 0x148 stored buffer.
        std::vector<std::byte> remapPK7toPK8(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x148, std::byte{0});
            // EC + Sanity + Checksum (checksum recomputed later)
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x08);
            // Species / HeldItem / ID32 / EXP (0x08..0x13 shared)
            copyBytes(destinationBytes, 0x08, sourceBytes, 0x08, 0x0C);
            wr8(destinationBytes, 0x14, rd8(sourceBytes, 0x14));        // Ability (PK7 u8 -> PK8 u16 low byte)
            wr8(destinationBytes, 0x16, rd8(sourceBytes, 0x15) & 0x07); // AbilityNumber (PK7 0x15 bits0-2 -> PK8 0x16)
            copyBytes(destinationBytes, 0x18, sourceBytes, 0x16, 0x02); // MarkingValue (both u16, two bits per mark)
            copyBytes(destinationBytes, 0x1C, sourceBytes, 0x18, 0x04); // PID
            wr8(destinationBytes, 0x20, rd8(sourceBytes, 0x1C));        // Nature
            wr8(destinationBytes, 0x21, rd8(sourceBytes, 0x1C));        // StatNature := Nature (mints arrive in Gen 8)
            {
                const uint8_t packedByte = rd8(sourceBytes, 0x1D);
                const uint8_t fateful = packedByte & 0x01, gender = (packedByte >> 1) & 0x03, form = packedByte >> 3;
                // PK8 0x22: Fateful(bit0) + Gender(bits2-3)
                wr8(destinationBytes, 0x22, static_cast<uint8_t>(fateful | (gender << 2)));
                wr8(destinationBytes, 0x24, form);                                          // PK8 0x24: Form
            }
            copyBytes(destinationBytes, 0x26, sourceBytes, 0x1E, 0x06); // EVs (HP,ATK,DEF,SPE,SPA,SPD -- same order)
            copyBytes(destinationBytes, 0x2C, sourceBytes, 0x24, 0x06); // Contest stats (Cool..Sheen)
            wr8(destinationBytes, 0x32, rd8(sourceBytes, 0x2B));        // PokerusState
            wr8(destinationBytes, 0x3C, rd8(sourceBytes, 0x38));        // RibbonCountMemoryContest
            wr8(destinationBytes, 0x3D, rd8(sourceBytes, 0x39));        // RibbonCountMemoryBattle
            copyBytes(destinationBytes, 0xE4, sourceBytes, 0x3C, 0x04); // FormArgument
            copyBytes(destinationBytes, 0x58, sourceBytes, 0x40, 0x1A); // Nickname
            copyBytes(destinationBytes, 0x72, sourceBytes, 0x5A, 0x08); // Moves
            copyBytes(destinationBytes, 0x7A, sourceBytes, 0x62, 0x04); // Move PP
            copyBytes(destinationBytes, 0x7E, sourceBytes, 0x66, 0x04); // Move PP-Ups
            copyBytes(destinationBytes, 0x82, sourceBytes, 0x6A, 0x08); // Relearn moves
            copyBytes(destinationBytes, 0x8C, sourceBytes, 0x74, 0x04); // IV32 (IVs + isEgg bit30 + isNicknamed bit31)
            copyBytes(destinationBytes, 0xA8, sourceBytes, 0x78, 0x1A); // HT name
            wr8(destinationBytes, 0xC2, rd8(sourceBytes, 0x92));        // HT gender
            wr8(destinationBytes, 0xC4, rd8(sourceBytes, 0x93));        // CurrentHandler
            wr8(destinationBytes, 0xC8, rd8(sourceBytes, 0xA2));        // HT friendship
            copyBytes(destinationBytes, 0xC9, sourceBytes, 0xA4, 0x03); // HT memory (intensity/memory/feeling)
            copyBytes(destinationBytes, 0xCC, sourceBytes, 0xA8, 0x02); // HT memory variable
            wr8(destinationBytes, 0xDC, rd8(sourceBytes, 0xAE));        // Fullness
            wr8(destinationBytes, 0xDD, rd8(sourceBytes, 0xAF));        // Enjoyment
            copyBytes(destinationBytes, 0xF8, sourceBytes, 0xB0, 0x1A); // OT name
            wr8(destinationBytes, 0x112, rd8(sourceBytes, 0xCA));       // OT friendship
            wr8(destinationBytes, 0x113, rd8(sourceBytes, 0xCC));       // OT memory intensity
            wr8(destinationBytes, 0x114, rd8(sourceBytes, 0xCD));       // OT memory
            copyBytes(destinationBytes, 0x116, sourceBytes, 0xCE, 0x02); // OT memory variable
            wr8(destinationBytes, 0x118, rd8(sourceBytes, 0xD0));       // OT memory feeling
            copyBytes(destinationBytes, 0x119, sourceBytes, 0xD1, 0x06); // egg date (0xD1-0xD3) + met date (0xD4-0xD6)
            copyBytes(destinationBytes, 0x120, sourceBytes, 0xD8, 0x04); // egg location + met location
            wr8(destinationBytes, 0x124, rd8(sourceBytes, 0xDC));       // Ball
            wr8(destinationBytes, 0x125, rd8(sourceBytes, 0xDD));       // MetLevel(bits0-6) + OTGender(bit7)
            wr8(destinationBytes, 0x126, rd8(sourceBytes, 0xDE));       // HyperTrain flags
            wr8(destinationBytes, 0xDE, rd8(sourceBytes, 0xDF));        // Version (PK7 0xDF -> PK8 0xDE)
            wr8(destinationBytes, 0xE2, rd8(sourceBytes, 0xE3));        // Language (PK7 0xE3 -> PK8 0xE2)
            return destinationBytes;
        }

        // PK8 layout -> PK7 (Sun/Moon, Ultra Sun/Ultra Moon). Mirror of remapPK7toPK8; returns a
        // 260-byte PARTY record, whose stat tail the caller fills with recalculateStats(). Gen 8's own
        // additions have no home here and are dropped: Technical Record flags, Dynamax level and
        // Gigantamax, Sociability, the height/weight scalars, the HT language byte, AffixedRibbon,
        // marks, and the mint (PK7 has Nature but no StatNature, so a minted pokemon reverts to its real
        // nature). Nothing PKSE can do about that -- the fields do not exist in the 3DS record.
        std::vector<std::byte> remapPK8toPK7(const std::vector<std::byte> &sourceBytes)
        {
            // One buffer serves both Gen 7 destinations. Each game group owns its own size constant
            // by design, so pin the fact that they agree rather than assuming it -- if Ultra Sun's
            // ever moved, this would quietly hand a Pokemon7USUM a buffer of the wrong length.
            static_assert(Encryption::SIZE_PARTY7_SM == Encryption::SIZE_PARTY7_USUM,
                          "Sun/Moon and Ultra Sun/Moon party records must be the same length");
            std::vector<std::byte> destinationBytes(Encryption::SIZE_PARTY7_SM, std::byte{0});
            copyBytes(destinationBytes, 0x00, sourceBytes, 0x00, 0x08); // EC + Sanity + Checksum
            copyBytes(destinationBytes, 0x08, sourceBytes, 0x08, 0x0C); // Species / HeldItem / ID32 / EXP
            wr8(destinationBytes, 0x14, rd8(sourceBytes, 0x14));        // Ability (PK8 u16 low byte -> PK7 u8)
            wr8(destinationBytes, 0x15, rd8(sourceBytes, 0x16) & 0x07); // AbilityNumber
            copyBytes(destinationBytes, 0x16, sourceBytes, 0x18, 0x02); // MarkingValue
            copyBytes(destinationBytes, 0x18, sourceBytes, 0x1C, 0x04); // PID
            wr8(destinationBytes, 0x1C, rd8(sourceBytes, 0x20));        // Nature (the mint at 0x21 is dropped)
            {
                const uint8_t genderByte = rd8(sourceBytes, 0x22);
                const uint8_t fateful = genderByte & 0x01, gender = (genderByte >> 2) & 0x03,
                              form = rd8(sourceBytes, 0x24);
                wr8(destinationBytes, 0x1D, static_cast<uint8_t>(fateful | (gender << 1) | (form << 3)));
            }
            copyBytes(destinationBytes, 0x1E, sourceBytes, 0x26, 0x06); // EVs
            copyBytes(destinationBytes, 0x24, sourceBytes, 0x2C, 0x06); // Contest stats
            wr8(destinationBytes, 0x2B, rd8(sourceBytes, 0x32));        // PokerusState
            wr8(destinationBytes, 0x38, rd8(sourceBytes, 0x3C));        // RibbonCountMemoryContest
            wr8(destinationBytes, 0x39, rd8(sourceBytes, 0x3D));        // RibbonCountMemoryBattle
            copyBytes(destinationBytes, 0x3C, sourceBytes, 0xE4, 0x04); // FormArgument
            copyBytes(destinationBytes, 0x40, sourceBytes, 0x58, 0x1A); // Nickname
            copyBytes(destinationBytes, 0x5A, sourceBytes, 0x72, 0x08); // Moves
            copyBytes(destinationBytes, 0x62, sourceBytes, 0x7A, 0x04); // Move PP
            copyBytes(destinationBytes, 0x66, sourceBytes, 0x7E, 0x04); // Move PP-Ups
            copyBytes(destinationBytes, 0x6A, sourceBytes, 0x82, 0x08); // Relearn moves
            copyBytes(destinationBytes, 0x74, sourceBytes, 0x8C, 0x04); // IV32
            copyBytes(destinationBytes, 0x78, sourceBytes, 0xA8, 0x1A); // HT name
            wr8(destinationBytes, 0x92, rd8(sourceBytes, 0xC2));        // HT gender
            wr8(destinationBytes, 0x93, rd8(sourceBytes, 0xC4));        // CurrentHandler
            wr8(destinationBytes, 0xA2, rd8(sourceBytes, 0xC8));        // HT friendship
            copyBytes(destinationBytes, 0xA4, sourceBytes, 0xC9, 0x03); // HT memory
            copyBytes(destinationBytes, 0xA8, sourceBytes, 0xCC, 0x02); // HT memory variable
            wr8(destinationBytes, 0xAE, rd8(sourceBytes, 0xDC));        // Fullness
            wr8(destinationBytes, 0xAF, rd8(sourceBytes, 0xDD));        // Enjoyment
            copyBytes(destinationBytes, 0xB0, sourceBytes, 0xF8, 0x1A); // OT name
            wr8(destinationBytes, 0xCA, rd8(sourceBytes, 0x112));       // OT friendship
            wr8(destinationBytes, 0xCC, rd8(sourceBytes, 0x113));       // OT memory intensity
            wr8(destinationBytes, 0xCD, rd8(sourceBytes, 0x114));       // OT memory
            copyBytes(destinationBytes, 0xCE, sourceBytes, 0x116, 0x02); // OT memory variable
            wr8(destinationBytes, 0xD0, rd8(sourceBytes, 0x118));       // OT memory feeling
            copyBytes(destinationBytes, 0xD1, sourceBytes, 0x119, 0x06); // egg + met dates
            copyBytes(destinationBytes, 0xD8, sourceBytes, 0x120, 0x04); // egg + met locations
            wr8(destinationBytes, 0xDC, rd8(sourceBytes, 0x124));       // Ball
            wr8(destinationBytes, 0xDD, rd8(sourceBytes, 0x125));       // MetLevel + OTGender
            wr8(destinationBytes, 0xDE, rd8(sourceBytes, 0x126));       // HyperTrain flags
            wr8(destinationBytes, 0xDF, rd8(sourceBytes, 0xDE));        // Version
            wr8(destinationBytes, 0xE3, rd8(sourceBytes, 0xE2));        // Language
            return destinationBytes;
        }

        // Gen 3 is the most divergent format: no EC (EC := PID), nature/gender/shiny all PID-derived,
        // ability = a slot BIT (IV32 bit31) resolved through the personal table, INTERNAL species ids,
        // Gen 3 item ids, Gen 3 text (not UTF-16), IVs packed with the ability bit at 31, and no relearn
        // moves / HT / memories / marks. We map identity + stats + moves + origin and drop the rest;
        // held item + moveset are remapped/sanitized to the true destination in convert().

        // PK3 (canonical decrypted, 80/100 B) -> PK8 layout (0x148 stored).
        std::vector<std::byte> remapPK3toPK8(const std::vector<std::byte> &sourceBytes)
        {
            std::vector<std::byte> destinationBytes(0x148, std::byte{0});
            const uint32_t pidValue = rd32(sourceBytes, 0x00);
            const uint16_t national = Pokemon::gen3InternalToNational(rd16(sourceBytes, 0x20));
            const uint32_t iv32 = rd32(sourceBytes, 0x48);
            const uint16_t origins = rd16(sourceBytes, 0x46);
            const uint8_t abilBit = (iv32 >> 31) & 1;

            // Gen 3 stores no form except Unown, whose letter lives in the PID.
            const uint8_t form = (national == 201) ? Pokemon::unownFormFromPID(pidValue) : 0;
            const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(national, form);

            wr32(destinationBytes, 0x00, pidValue);                                  // EC := PID
            wr16(destinationBytes, 0x08, national);                             // Species (National)
            // Held item (Gen 3 -> modern; 0 if none)
            wr16(destinationBytes, 0x0A, Names::itemG3ToModern(rd16(sourceBytes, 0x22)));
            copyBytes(destinationBytes, 0x0C, sourceBytes, 0x04, 4);                      // ID32 (TID16 + SID16)
            copyBytes(destinationBytes, 0x10, sourceBytes, 0x24, 4);                      // EXP
            wr16(destinationBytes, 0x14, abilBit ? pi.ability2 : pi.ability1);  // Ability id (from the slot bit)
            wr8(destinationBytes, 0x16, abilBit ? 2 : 1);                       // AbilityNumber (1=slot1 / 2=slot2)
            wr32(destinationBytes, 0x1C, pidValue);                                  // PID
            wr8(destinationBytes, 0x20, static_cast<uint8_t>(pidValue % 25));        // Nature
            wr8(destinationBytes, 0x21, static_cast<uint8_t>(pidValue % 25));        // StatNature (no mints pre-Gen 8)

            uint8_t gender;
            const uint8_t genderRatioValue = pi.genderRatio;
            if (genderRatioValue == 255)
                gender = 2;
            else if (genderRatioValue == 254)
                gender = 1;
            else if (genderRatioValue == 0)
                gender = 0;
            else
                gender = ((pidValue & 0xFF) < genderRatioValue) ? 1 : 0;
            const uint8_t fateful = (rd32(sourceBytes, 0x4C) >> 31) & 1; // Gen 3 ribbon bit31 = FatefulEncounter
            wr8(destinationBytes, 0x22, static_cast<uint8_t>(fateful | (gender << 2)));
            wr8(destinationBytes, 0x24, form);

            copyBytes(destinationBytes, 0x26, sourceBytes, 0x38, 6); // EVs (HP,ATK,DEF,SPE,SPA,SPD -- same order)
            copyBytes(destinationBytes, 0x2C, sourceBytes, 0x3E, 6); // Contest stats
            wr8(destinationBytes, 0x32, rd8(sourceBytes, 0x44));     // Pokerus

            copyBytes(destinationBytes, 0x72, sourceBytes, 0x2C, 8); // Moves 1-4
            copyBytes(destinationBytes, 0x7A, sourceBytes, 0x34, 4); // Move PP
            {
                const uint8_t pokerusValue = rd8(sourceBytes, 0x28);
                for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                    wr8(destinationBytes, 0x7E + moveIndex, (pokerusValue >> (moveIndex * 2)) & 3);
            }
            wr32(destinationBytes, 0x8C, iv32 & 0x7FFFFFFFu); // IVs + isEgg(bit30); clear Gen 3 ability bit(31)

            // The PK3's OWN language byte picks the table, the same way the entity layer reads it.
            // A Japanese record decoded through the international table loses its name here just as
            // silently as it would going the other way.
            const uint8_t sourceLanguage = rd8(sourceBytes, 0x12);
            // Nickname (Gen 3 -> UTF-16)
            g3NameToUtf16(destinationBytes, 0x58, sourceBytes, 0x08, 10, 26, sourceLanguage);
            g3NameToUtf16(destinationBytes, 0xF8, sourceBytes, 0x14, 7, 26, sourceLanguage); // OT name
            wr8(destinationBytes, 0x112, rd8(sourceBytes, 0x29)); // OT friendship (Gen 3 single friendship)
            wr8(destinationBytes, 0x124, static_cast<uint8_t>((origins >> 11) & 0x0F));                          // Ball
            // MetLevel + OTgender
            wr8(destinationBytes, 0x125, static_cast<uint8_t>((origins & 0x7F) | (((origins >> 15) & 1) << 7)));
            wr16(destinationBytes, 0x122, rd8(sourceBytes, 0x45)); // Met location (Gen 3 id -- carried)
            // Gen 3 records no met DATE, so leaving it 0 reads as "met 00/00/2000" in the destination.
            // Emulate HOME: PKHeX's PK3.ConvertToPK4() stamps MetDate = EncounterDate.GetDateNDS()
            // (the transfer date). Do the same with today's date; it rides the PK8-hub met-date region
            // (0x11C-0x11E, year stored as year-2000) out to every destination format. Skip an egg -- an
            // unhatched egg has no met date until it hatches (isEgg is iv32 bit 30, carried above).
            if (!((iv32 >> 30) & 1))
            {
                const std::time_t nowT = std::time(nullptr);
                if (const std::tm *lt = std::localtime(&nowT))
                {
                    int year = lt->tm_year + 1900;
                    if (year < 2000) year = 2000; // met-year byte is (year - 2000); never negative
                    wr8(destinationBytes, 0x11C, static_cast<uint8_t>(year - 2000));    // Met_Year
                    wr8(destinationBytes, 0x11D, static_cast<uint8_t>(lt->tm_mon + 1)); // Met_Month (1-12)
                    wr8(destinationBytes, 0x11E, static_cast<uint8_t>(lt->tm_mday));    // Met_Day (1-31)
                }
            }
            // Origin game (FR=4/LG=5, same PK8 enum)
            wr8(destinationBytes, 0xDE, static_cast<uint8_t>((origins >> 7) & 0x0F));
            wr8(destinationBytes, 0xE2, rd8(sourceBytes, 0x12));                                // Language
            return destinationBytes;
        }

        /// Where a FOREIGN pokemon lands when it is written into a Gen 3 game.
        struct Gen3Landing
        {
            uint8_t metLocation;
            uint8_t metLevel;
            uint8_t originVersion; // the game that can actually produce this pokemon
            /// The level the pokemon should ARRIVE at. Equal to the level it came in with unless
            /// the rebuild is on and the encounter it landed on starts above that -- see the
            /// note on raising a level in gen3LandingFor.
            uint8_t currentLevel;
        };

        /// A met location + level that the destination game could actually have produced.
        ///
        /// NOT THE SOURCE'S. A Gen 3 met location is a BYTE in its own id space and every later
        /// generation's is a u16 in a different one, so carrying the number across is meaningless, and
        /// truncating it is how a Gen 1 pokemon lands at "Abandoned Ship" (30013 & 0xFF = 61) -- a HOENN
        /// location, in a Kanto game, on a pokemon that was never in either. The two id spaces share
        /// nothing; PKHeX's own constants make the point, since 30013 is the Gen 1 -> Gen 7 transfer
        /// marker AND a Gen 5 Crown Beast marker.
        ///
        /// Derived from a REAL ENCOUNTER, which is what PKHeX does wherever it has to synthesize one
        /// (IEntityRejuvenator: `MetLocation = enc.Location`). PKSM instead stamps a fixed sentinel --
        /// 0xFD, "(gift egg)" -- with a comment saying it is a guess; PKSE has the encounter tables
        /// Layer 3 already checks against, so it can answer properly.
        ///
        /// THIS DOES NOT MAKE THE POKEMON LEGAL AND IS NOT TRYING TO. No transfer into Gen 3 has ever
        /// existed, so anything that did not originate there is illegal whatever it carries, and the PID
        /// is rerolled besides (it will not match a real Gen 3 RNG frame). The goal is a location that is
        /// POSSIBLE in the destination instead of one that is provably not -- and where no encounter
        /// exists, to leave the legality checker free to say so.
        Gen3Landing gen3LandingFor(uint16_t national, uint8_t form, uint8_t destOriginVersion,
                                   uint8_t sourceMetLevel, uint8_t currentLevel)
        {
            // Fallback: the destination's own first town. Ruby/Sapphire/Emerald have no encounter
            // table in PKSE, and not every Gen 3 species is catchable in the game being written to.
            // A real place in the right region beats one in the wrong one, and the checker still
            // reports the truth -- that no encounter produces this pokemon here.
            // WITH THE TOGGLE OFF, THE PLACEHOLDER IS THE FIRST ENTRY IN THE GAME'S OWN LIST.
            // A user who wants to set the met data by hand is best served by a value that is
            // obviously unset rather than one that looks plausible and gets missed -- so it is
            // location 0, the top of the met-location picker, and the checker says so loudly.
            // With the toggle ON the fallback is the destination's own first town, because by then
            // PKSE is rebuilding the pokemon and a real place in the right region is the floor.
            const bool intoKanto = (destOriginVersion == static_cast<uint8_t>(GameVersion::FR) ||
                                    destOriginVersion == static_cast<uint8_t>(GameVersion::LG));
            const uint8_t placeholderLocation =
                g_autoLegalizeTransfers ? static_cast<uint8_t>(intoKanto ? 88 : 0) : 0;
            //                                               Pallet Town  Littleroot Town
            Gen3Landing landing{placeholderLocation, sourceMetLevel, destOriginVersion, currentLevel};

            const auto destVersion = static_cast<GameVersion>(destOriginVersion);
            const Legality::EncounterTable *table = Legality::getEncounterTable(destVersion);
            if (table == nullptr || table->speciesIndex == nullptr)
                return landing;
            // THE SIBLING VERSION IS PART OF THE ANSWER, because half of Kanto is version-exclusive.
            // Sandshrew, Vulpix, Bellsprout, Slowpoke, Staryu, Magmar and Pinsir (with their
            // evolutions) exist ONLY in LeafGreen, and the mirror set only in FireRed -- 13 of the
            // 68 "FireRed does not have this" reports on a real bank were exactly that. In the
            // games you get one by TRADING, and a traded pokemon keeps the other game as its
            // origin, so stamping the version that can produce it is what a legitimate one looks
            // like. The destination's own version is tried FIRST, so nothing becomes a trade that
            // did not have to be.
            const uint8_t destBit = Legality::encounterVersionBit(destVersion);
            // THE OTHER GEN 3 GROUP COUNTS TOO, for exactly the reason the sibling version does.
            // Hoenn and Kanto trade over a link cable, so a Kanto save holding a Hoenn pokemon is
            // ordinary -- and for some species it is the ONLY story. FireRed has no Hoothoot at all
            // and Emerald's Safari Zone has one at level 35, so stamping FireRed on a Hoothoot
            // leaves a record no Gen 3 game can explain, while stamping Emerald leaves one that is
            // simply true. Only the four Gen 3 groups can be reached this way: the origin field is
            // four bits and its games occupy 1-5.
            //
            // ORDERED SO NOTHING BECOMES A TRADE THAT DID NOT HAVE TO BE: the destination itself,
            // then the game it shares a save format with, then the other group's. A same-pair trade
            // is a weaker claim than a cross-region one, so it is tried first.
            //
            // PASSES, NOT RECURSION. Calling this function again for another version is the obvious
            // spelling and it is unbounded: FireRed's sibling is LeafGreen, whose sibling is
            // FireRed. It stack-overflowed on the first pokemon neither version can produce.
            static constexpr uint8_t GEN3_VERSIONS[] = {
                static_cast<uint8_t>(GameVersion::RU), static_cast<uint8_t>(GameVersion::SA),
                static_cast<uint8_t>(GameVersion::EM), static_cast<uint8_t>(GameVersion::FR),
                static_cast<uint8_t>(GameVersion::LG)};
            uint8_t attemptVersion[sizeof(GEN3_VERSIONS)] = {destOriginVersion};
            int attemptCount = 1;
            if (g_autoLegalizeTransfers)
            {
                const GameVersion destinationGroup = Enums::getGameGroup(destVersion);
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (const uint8_t candidateVersion : GEN3_VERSIONS)
                    {
                        if (candidateVersion == destOriginVersion)
                            continue;
                        const bool sharesSaveFormat =
                            Enums::getGameGroup(static_cast<GameVersion>(candidateVersion)) == destinationGroup;
                        if (sharesSaveFormat != (pass == 0))
                            continue;
                        attemptVersion[attemptCount++] = candidateVersion;
                    }
                }
            }
            (void)destBit;

            // THE PRE-EVOLUTION CHAIN IS PART OF THE ANSWER, exactly as it is for the checker
            // (EncounterMatch::buildChain). A Wartortle is never caught in FireRed -- it is a
            // Squirtle that evolved, so the met data belongs to the SQUIRTLE. Looking up the
            // species alone found nothing for every evolved pokemon in a transfer and dropped all
            // of them on the fallback town; on a real bank that was 120 of 252.
            uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN + 1] = {};
            uint8_t chainForm[Pokemon::EVO_MAX_CHAIN + 1] = {};
            chainSpecies[0] = national;
            chainForm[0] = form;
            // Only the rebuild walks the chain, tries the sibling version, or claims an egg. Off,
            // PKSE does the plain same-species lookup and nothing more.
            const int chainLength =
                g_autoLegalizeTransfers
                    ? 1 + Pokemon::getPreEvolutionChain(destVersion, national, form, chainSpecies + 1,
                                                        chainForm + 1, Pokemon::EVO_MAX_CHAIN)
                    : 1;

            // Deterministic pick, because a conversion that answered differently each run could
            // not be tested and would hand a user a different pokemon every time they undid and
            // redid it. Preference order:
            //
            //   1. a row whose levels the pokemon can satisfy
            //   2. a WILD slot over a one-off static/gift/trade
            //   3. the SMALLEST LEVEL SHORTFALL -- the encounter it comes closest to qualifying for
            //   4. the lowest location id, purely so the answer is stable
            //
            // The chain is walked NEAREST FIRST, so a species that can be caught directly wins
            // over its own pre-evolution.
            //
            // RULE 3 EXISTS BECAUSE RULE 4 IS MEANINGLESS AS A CHOICE. It is a fine tiebreak for
            // determinism and a terrible one for deciding, and where nothing fits it was doing all
            // the deciding: a level 15 Machop has three FireRed rows, Victory Road 32-34 (id 132),
            // Rock Tunnel 16-17 (id 138) and Mt. Ember 31-39 (id 175). All are wild and none fits,
            // so Rock Tunnel -- one level short of legal -- lost to Victory Road by six id numbers,
            // and the checker then truthfully but uselessly reported "nearest is 32-34". Ranking by
            // distance instead names the place the pokemon nearly belongs, and the same message
            // becomes "nearest is 16-17", which tells the user exactly what to fix.
            //
            // IT SITS BELOW THE WILD PREFERENCE, NOT ABOVE IT, and that ordering is measured rather
            // than assumed. Above it, distance wins 82 more cases across the FireRed table by
            // stamping an IN-GAME TRADE on a low-level pokemon -- those rows carry wide-open bands
            // (Electrode 3-100, Tangela 5-100), so a level 2 Nidoran would be claimed as the Route
            // 11 trade, beating Route 3 (6-6) by a single level. A trade is a far stronger and more
            // falsifiable claim than a patch of grass -- fixed OT, fixed id, fixed nickname -- and
            // one level of accuracy does not buy it. Below the wild preference the swap never
            // happens, and 25 species land strictly closer than they did.
            //
            // A ROW THAT FITS IS UNAFFECTED, which is what keeps this contained: everything that
            // fits has a shortfall of 0, so rule 3 can only ever order the rows where NOTHING fits.
            bool haveRow = false, haveLevelFit = false, haveWild = false;
            uint8_t bestLocation = 0;
            uint8_t bestLevelShortfall = 0;
            uint8_t bestLevelMin = 0;
            for (int attempt = 0; attempt < attemptCount && !haveRow; ++attempt)
            {
            const uint8_t tryVersion = attemptVersion[attempt];
            // THE TABLE IS PER ATTEMPT, NOT PER DESTINATION, now that a candidate can belong to the
            // other Gen 3 group: Ruby/Sapphire/Emerald and FireRed/LeafGreen have separate tables,
            // and asking the destination's for an Emerald row finds nothing at all.
            const Legality::EncounterTable *attemptTable =
                Legality::getEncounterTable(static_cast<GameVersion>(tryVersion));
            if (attemptTable == nullptr || attemptTable->speciesIndex == nullptr)
                continue;
            const uint8_t versionBit =
                Legality::encounterVersionBit(static_cast<GameVersion>(tryVersion));
            for (int link = 0; link < chainLength; ++link)
            {
                const uint16_t species = chainSpecies[link];
                if (species == 0 || species + 1 >= attemptTable->speciesIndexLength)
                    continue;
                for (uint16_t rowIndex = attemptTable->speciesIndex[species];
                     rowIndex < attemptTable->speciesIndex[species + 1]; ++rowIndex)
                {
                    const Legality::EncounterRow &row = attemptTable->rows[rowIndex];
                    if (versionBit != 0 && (row.versions & versionBit) == 0)
                        continue;
                    // a region sentinel, not a place; Gen 3 has none but be explicit
                    if (row.location > 0xFF) continue;
                    // AN EGG ROW DESCRIBES THE EGG, NOT THE MEETING. Gen 3 has no egg-location
                    // field, so a hatched pokemon carries where it HATCHED (146, Four Island) and
                    // the row's own location is the egg's. Taking it stamped Togepi as met at
                    // "(gift egg)" (253), which the checker rightly refuses -- the egg branch below
                    // is where these belong.
                    if ((row.flags & Legality::ENCOUNTER_FLAG_EGG) != 0 || row.eggLocation != 0)
                        continue;
                    // NEVER AN EVENT. A Mystery Gift distribution, a Pokemon Colosseum gift, or a
                    // static only an event ticket opens (Mew on Faraway Island) is a story this
                    // Pokemon never had and cannot now have -- claiming one to quiet the checker is
                    // exactly what a rebuild must not do. The LOCATION is tested as well as the row:
                    // Colosseum's gifts are neither distributions nor fateful, but Gen 3 meets them
                    // at the event location too, and a level 20 Pikachu from Red landed on the
                    // Japanese bonus disc's. As candidates they also HID real encounters, because the
                    // search stops at the first game with any row at all: Ruby's only Bulbasaur is the
                    // level 70 10th-anniversary distribution, so a level 6 Bulbasaur was raised to 70
                    // there and never reached FireRed's Pallet Town gift, which explains it as it is.
                    if (row.kind == Legality::ENCOUNTER_KIND_EVENT ||
                        (row.flags & Legality::ENCOUNTER_FLAG_FATEFUL) != 0 ||
                        row.location == Legality::ENCOUNTER_LOCATION_EVENT3)
                        continue;
                    // THE FORM HAS TO MATCH, and Unown is why. Its 28 letters are 28 SEPARATE rows,
                    // each pinned to one Tanoby chamber -- Monean holds only A and ?, Liptoo only
                    // B/C/G/N/T. Matching on species alone picked the lowest chamber for every
                    // letter, so 26 of 28 landed where their own letter never appears.
                    if (row.form != Legality::ENCOUNTER_FORM_ANY && row.form != chainForm[link])
                        continue;
                    const bool levelFits = (row.levelMin <= sourceMetLevel);
                    const bool isWild = (row.kind == Legality::ENCOUNTER_KIND_WILD);
                    // How far SHORT of this encounter's floor the pokemon is; 0 when it fits. A row
                    // that does not fit is one the pokemon is too LOW for by definition, so this is
                    // never negative -- and for those rows, ranking by it is the same as ranking by
                    // levelMin. Spelled as the distance anyway, because that stays the right
                    // question if levelFits ever becomes two-sided.
                    const uint8_t levelShortfall =
                        levelFits ? 0 : static_cast<uint8_t>(row.levelMin - sourceMetLevel);
                    if (haveRow)
                    {
                        if (haveLevelFit && !levelFits)
                            continue;
                        if (levelFits == haveLevelFit)
                        {
                            if (haveWild && !isWild)
                                continue;
                            if (isWild == haveWild)
                            {
                                if (levelShortfall > bestLevelShortfall)
                                    continue;
                                if (levelShortfall == bestLevelShortfall && row.location >= bestLocation)
                                    continue;
                            }
                        }
                    }
                    bestLocation = static_cast<uint8_t>(row.location);
                    bestLevelMin = row.levelMin;
                    landing.metLocation = bestLocation;
                    landing.originVersion = tryVersion;
                    // Keep the original met level where the encounter allows it, so the pokemon's
                    // own history survives as far as it can; clamp into range otherwise. When no
                    // row fits at all the level is left alone rather than raised above the current
                    // level, which would trade one finding for another. (With the toggle on, the
                    // rebuild below lifts it to the chosen row's floor -- never above the level.)
                    landing.metLevel = levelFits
                                           ? static_cast<uint8_t>(sourceMetLevel > row.levelMax
                                                                      ? row.levelMax
                                                                      : sourceMetLevel)
                                           : sourceMetLevel;
                    haveRow = true;
                    haveLevelFit = levelFits;
                    haveWild = isWild;
                    bestLevelShortfall = levelShortfall;
                }
                // A link that produced a usable encounter ends the walk: a Squirtle gift explains a
                // Wartortle, but so would a Squirtle's own pre-evolution if one existed, and the
                // nearer link is always the better story.
                if (haveRow && haveLevelFit)
                    break;
            }
            }

            // THE REBUILD MAY RAISE A LEVEL, AND ONLY THE REBUILD. Where no encounter starts low enough,
            // leaving the met level alone leaves the pokemon flagged -- 30 of a 252-pokemon bank were exactly
            // that, a level 15 Machop in a game whose Rock Tunnel starts at 16. With the toggle ON, PKSE is
            // re-creating the pokemon as a native of the destination, so it is re-created at a level that
            // encounter can produce.
            //
            // IT ONLY EVER GOES UP. Lowering one would destroy training the user did, and a pokemon ABOVE an
            // encounter's band is not a problem in the first place -- it grew. With the toggle off nothing
            // here runs at all, and the honest finding stands.
            //
            // THE MET LEVEL IS LIFTED EVEN WHEN THE LEVEL IS NOT. A pokemon can have grown past an
            // encounter's floor after being MET below it -- a level 63 Ultra Sun Charizard met at level 1
            // lands on FireRed's Pallet Town gift, which starts at 5 -- and keeping the source's met level
            // leaves it flagged at a row it otherwise fits. Rebuilding every pokemon in a real collection
            // into both Gen 3 groups, that was 97 of 2204. The met level is invented along with the location
            // it belongs to, so it goes to the floor, and the level the user trained to stays where it was.
            if (haveRow && !haveLevelFit && g_autoLegalizeTransfers)
            {
                if (bestLevelMin > landing.currentLevel)
                    landing.currentLevel = bestLevelMin;
                landing.metLevel = bestLevelMin;
            }

            // NOTHING IN THIS GAME PRODUCES IT -- so say it HATCHED. FireRed/LeafGreen never have
            // the Johto starters, the babies, Mareep or Houndour, and no met data can make a caught
            // one legal. An egg can: Gen 3 records no egg location (the field does not exist), so a
            // hatched pokemon is recognised by its fixed hatch location and level alone, which is a
            // story the game itself cannot contradict.
            //
            // Gated on the game's own BREEDABLE set rather than applied blanket: Celebi and Mew are
            // not breedable, so claiming an egg for them would replace one impossible story with
            // another. They keep the fallback town and the checker keeps telling the truth.
            // The BASE of the chain is what hatches: a Togetic was a Togepi egg, so breedability is
            // asked of the bottom link rather than of the pokemon standing in front of you.
            const uint16_t eggSpecies = chainSpecies[chainLength - 1];
            // WHAT HATCHES IS THE BOTTOM OF THE CHAIN, so every evolution above it still has to be
            // paid for. An egg cannot get round a level requirement the way a wild slot can: Gold
            // and Silver put wild Noctowl on Route 2 at level 7, but a FireRed egg is a Hoothoot
            // and a Hoothoot is a Noctowl only from level 20 -- so a level 12 Noctowl has no egg
            // story anywhere, and claiming one would swap "no encounter" for a finding that is just
            // as true and harder to read. The checker asks the same question from the other side
            // (EncounterMatch::checkEgg), which is what keeps the two halves agreeing.
            const uint8_t evolutionFloor =
                Pokemon::getEvolutionLevelFloor(destVersion, national, form, chainLength - 1);
            if (!haveRow && g_autoLegalizeTransfers && table->egg.hasBreeding != 0 &&
                table->egg.hatchLocation != 0 &&
                table->breedable != nullptr && eggSpecies != 0 &&
                ((table->breedable[eggSpecies >> 3] >> (eggSpecies & 7)) & 1u) != 0 &&
                (evolutionFloor == 0 || currentLevel >= evolutionFloor) &&
                // Gen 3 stamps a hatched egg at met level 0, so there is no floor left to clear --
                // but the id has to fit the byte a PK3 keeps its met location in.
                table->egg.hatchLocation <= 0xFF)
            {
                // THE DAY CARE'S OWN MAP, NOT THE LOWEST PERMITTED ID. Over a hundred Kanto places
                // are legal hatch locations and the lowest of them is the Ferry, which is a true
                // answer and a silly one; hatchLocation is the id the game itself writes.
                landing.metLocation = static_cast<uint8_t>(table->egg.hatchLocation);
                landing.metLevel = table->egg.hatchLevel;
                landing.originVersion = destOriginVersion; // an egg hatches where you are walking
                // A Gen 3 egg hatches at level 5, so a pokemon arriving below that did not hatch.
                // The rebuild raises it for the same reason it raises one into an encounter's band:
                // it is re-creating the pokemon as a native, and a level 4 hatchling is not one.
                if (landing.currentLevel < table->egg.hatchCurrentLevel)
                    landing.currentLevel = table->egg.hatchCurrentLevel;
            }
            return landing;
        }

        // PK8 layout -> PK3 (Gen 3). Returns a 100-byte PARTY record (box writes take the first 80 B).
        // `destOriginVersion` is the exact destination game's origin byte (FR = 4, LG = 5). It has to be
        // passed in: a game GROUP collapses the pair into one value by design, so FRLG alone cannot say
        // which of the two the save actually is.
        std::vector<std::byte> remapPK8toPK3(const std::vector<std::byte> &sourceBytes, uint8_t destOriginVersion,
                                             GameVersion destGroup)
        {
            std::vector<std::byte> destinationBytes(0x64, std::byte{0});
            const uint32_t pidValue = rd32(sourceBytes, 0x1C);
            const uint16_t national = rd16(sourceBytes, 0x08);
            const uint32_t pk8iv = rd32(sourceBytes, 0x8C);
            const bool isEgg = (pk8iv >> 30) & 1;

            // Gen 3 derives nature/gender/shiny/ability ALL from the PID, but the source stores nature
            // and gender EXPLICITLY -- copying the PID verbatim silently changes them (an LGPE Calm pokemon
            // read as Jolly in FR/LG). The standard down-convert answer is to reroll the PID so its
            // Gen-3-derived traits match the source's: nature, gender, shiny status and ability slot are
            // preserved. IVs are NOT touched (separate field at 0x48). This DOES change the PID (the pokemon's
            // identity) and yields a PID/IV pair that won't match a real Gen 3 RNG frame -- the UI warns
            // the user first. Falls back to the original PID if no match is found within the budget.
            uint32_t outPid = pidValue;
            {
                const uint8_t natWant = rd8(sourceBytes, 0x20) % 25;                              // source nature
                const uint8_t genWant = static_cast<uint8_t>((rd8(sourceBytes, 0x22) >> 2) & 3); // 0=M,1=F,2=genderless
                const uint8_t genderRatioValue =
                    getPersonalInfo(national, rd8(sourceBytes, 0x24)).genderRatio; // by species+form
                const uint8_t abilBit = (rd8(sourceBytes, 0x16) == 2) ? 1u : 0u;   // AbilityNumber 2 -> slot 2
                // UNOWN'S LETTER IS THE PID IN GEN 3, so a reroll that ignores it RENAMES the
                // pokemon -- and worse, the letter decides which Tanoby chamber it can have been
                // caught in, so a relettered Unown also lands somewhere its own letter never
                // appears and the encounter checker refuses it. Preserved the way the entity's own
                // rerollPID does it: CONSTRUCTED, not filtered. Every candidate gets a bit pattern
                // that already yields the wanted letter, so the search costs what it did before
                // rather than 28x more.
                //
                // The letter also decides PID bit 0, and with it Gen 3's ability bit -- so for
                // Unown the ability test is dropped rather than fought. That loses nothing: Unown
                // has exactly one ability, so there is no second slot to land in.
                const bool isUnown = (national == 201);
                uint32_t letterPatterns[10] = {};
                int letterPatternCount = 0;
                if (isUnown)
                {
                    const uint32_t wantLetter = rd8(sourceBytes, 0x24) % 28u;
                    for (uint32_t value = wantLetter; value <= 0xFFu; value += 28u)
                        letterPatterns[letterPatternCount++] = value;
                }
                const uint32_t tid32 = rd32(sourceBytes, 0x0C);
                const uint16_t trainerShinyValue = static_cast<uint16_t>((tid32 & 0xFFFF) ^ (tid32 >> 16));
                const bool shWant = (static_cast<uint16_t>(((pidValue & 0xFFFF) ^ (pidValue >> 16)) ^
                                                           trainerShinyValue) < 16); // source threshold
                uint32_t cand = pidValue;
                for (int index = 0; index < 1000000; ++index)
                {
                    // Stamp the letter before ANY test: every constraint below reads the whole PID,
                    // so this has to happen first or they judge a PID that will not be stored.
                    if (letterPatternCount > 0)
                        cand = withUnownFormValue(
                            cand, letterPatterns[(cand >> 2) % static_cast<uint32_t>(letterPatternCount)]);
                    const uint8_t genderValue = (genderRatioValue == 255) ? 2 : (genderRatioValue == 254) ? 1
                                                    : (genderRatioValue == 0)     ? 0
                                                                    : (((cand & 0xFF) < genderRatioValue) ? 1 : 0);
                    const uint16_t pokemonShinyValue = static_cast<uint16_t>((cand & 0xFFFF) ^ (cand >> 16));
                    const bool isShiny =
                        (static_cast<uint16_t>(pokemonShinyValue ^ trainerShinyValue) < 8); // Gen 3 shiny threshold
                    if ((cand % 25) == natWant && genderValue == genWant && isShiny == shWant &&
                        (isUnown || (cand & 1u) == abilBit))
                    {
                        outPid = cand;
                        break;
                    }
                    cand = cand * 0x41C64E6Du + 0x00006073u; // Gen 3 LCG walk
                }
            }
            wr32(destinationBytes, 0x00, outPid); // PID rerolled to preserve nature/gender/shiny/ability
            copyBytes(destinationBytes, 0x04, sourceBytes, 0x0C, 4);                                // OTID32

            // LANGUAGE FIRST, because both names below are written through the table it picks.
            //
            // It is CLAMPED, not carried. Gen 3 shipped in six languages and reserved the id Korean
            // later took without ever using it, so a Korean or Chinese Pokemon -- or a Latin American
            // Spanish one out of Legends: Z-A -- would otherwise arrive claiming a language no Gen 3
            // cartridge has, and be read back through a font that cannot spell it. English is the
            // substitute for the reason PKHeX picks it (Language.cs `SafeLanguage`): it is the one
            // language every generation shipped and every table can write.
            const uint8_t destinationLanguage =
                Enums::safeLanguageForGroup(destGroup, rd8(sourceBytes, 0xE2));
            wr8(destinationBytes, 0x12, destinationLanguage);                                       // Language
            wr8(destinationBytes, 0x13, static_cast<uint8_t>(0x02 | (isEgg ? 0x04 : 0))); // Flags: HasSpecies (+ IsEgg)
            wr16(destinationBytes, 0x20, Pokemon::gen3NationalToInternal(national));      // Species (INTERNAL Gen 3 id)
            // Held item (modern -> Gen 3; 0 if none)
            wr16(destinationBytes, 0x22, Names::itemModernToG3(rd16(sourceBytes, 0x0A)));
            copyBytes(destinationBytes, 0x24, sourceBytes, 0x10, 4);                                // EXP

            // NICKNAME. A PK3 has no is-nicknamed flag -- the field always holds a name, and the game
            // decides whether it is a nickname by comparing it against the species name in the record's own
            // language. So an un-nicknamed Pokemon has to arrive holding exactly that string, in ITS
            // language: a Japanese Ditto's nickname is メタモン, not DITTO, however the console reading it
            // is set. PKSM's PK4::convertToG3 resolves it the same way.
            //
            // A REAL nickname is carried, all or nothing. A nickname that only PARTLY maps falls back to the
            // species name rather than storing its mappable prefix, because half a name is not the name the
            // player gave it.
            const bool sourceIsNicknamed = ((rd32(sourceBytes, 0x8C) >> 31) & 1) != 0;
            const bool nicknameCarried =
                sourceIsNicknamed &&
                utf16ToG3Name(destinationBytes, 0x08, sourceBytes, 0x58, 26, GEN3_NICKNAME_CHARS, destinationLanguage);
            if (!nicknameCarried)
            {
                utf8UpperToG3Name(
                    destinationBytes, 0x08,
                    Names::getSpeciesNameLocalized(
                        national, Names::languageIndexFor(static_cast<Enums::LanguageID>(destinationLanguage))),
                    GEN3_NICKNAME_CHARS, destinationLanguage);
            }

            // OT NAME. The one field here that cannot be reconstructed when it does not fit: a
            // nickname has the species name to fall back on, but a trainer's name is theirs and
            // there is no substitute for it. So it is carried when the destination's alphabet can
            // spell it -- which covers a Korean or Chinese player whose trainer name is Latin, the
            // common case -- and left BLANK when it cannot, rather than filled with a stand-in.
            //
            // Blank is the honest answer and it is a visible one: the legality checker reports an
            // empty OT, and the transfer confirm dialog says so before the move runs. A placeholder
            // would not be visible -- "Trainer" is a real Gen 3 in-game-trade OT that PKSE already
            // writes to mean exactly that, and the receiving player's own name would silently turn
            // a traded Pokemon into a self-caught one while its trainer id still said otherwise.
            const bool originalTrainerNameCarried =
                utf16ToG3Name(destinationBytes, 0x14, sourceBytes, 0xF8, 26, GEN3_OT_NAME_CHARS, destinationLanguage);
            if (!originalTrainerNameCarried && g_debugLogging)
            {
                Utils::logEventToFile("CONVERT action=GEN3 species=" + std::to_string(national) +
                                      " language=" + std::to_string(destinationLanguage) +
                                      " result=OT_NAME_DROPPED");
            }

            copyBytes(destinationBytes, 0x2C, sourceBytes, 0x72, 8); // Moves 1-4
            copyBytes(destinationBytes, 0x34, sourceBytes, 0x7A, 4); // Move PP
            {
                uint8_t pokerusValue = 0;
                for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                    pokerusValue |= static_cast<uint8_t>((rd8(sourceBytes, 0x7E + moveIndex) & 3) << (moveIndex * 2));
                wr8(destinationBytes, 0x28, pokerusValue);
            }
            copyBytes(destinationBytes, 0x38, sourceBytes, 0x26, 6); // EVs
            copyBytes(destinationBytes, 0x3E, sourceBytes, 0x2C, 6); // Contest stats
            wr8(destinationBytes, 0x29, rd8(sourceBytes, 0x112));    // Friendship (from OT friendship)
            wr8(destinationBytes, 0x44, rd8(sourceBytes, 0x32));     // Pokerus

            // Origins word: MetLevel(0-6) + Version(7-10) + Ball(11-14) + OTGender(15).
            const uint8_t sourceMetLevel = rd8(sourceBytes, 0x125) & 0x7F;
            const uint8_t otGender = (rd8(sourceBytes, 0x125) >> 7) & 1;
            uint8_t ball = rd8(sourceBytes, 0x124);
            // Gen 3 balls 1-12; default Poke Ball
            if (ball == 0 || ball > 12) ball = 4;
            // Origin version. A source that ALREADY holds a Gen 3 value (1-5) came from Gen 3 originally, so
            // it passes through untouched -- that is the origin-preserving case. Everything else is a modern
            // game whose id (SW = 44, VL = 51, ZA = 52 ...) does not fit Gen 3's 4-bit field, so the pokemon
            // is stamped with the game it is being written INTO. This is the normal path, not a rare edge:
            // every cross-generation transfer into Gen 3 takes it. Substituting a fixed member of the pair
            // stamps FireRed on a LeafGreen save every time.
            const uint8_t sourceVersion = rd8(sourceBytes, 0xDE);
            const bool originatedInGen3 = (sourceVersion >= 1 && sourceVersion <= 5);

            // MET LOCATION ASKS THE SAME QUESTION AS THE VERSION ABOVE, and has to answer it the same way --
            // answering it differently is what puts "Abandoned Ship" on a transferred Gen 1 pokemon.
            //
            //   Origin-preserving: a Gen 3 pokemon coming home. Its location is already a Gen 3 id, so
            //   it round-trips exactly (PK3 -> PK8 widened it to u16, this narrows it back, and every
            //   Gen 3 id is <= 255 so nothing is lost). Returning to your origin game is also the one
            //   backward transfer that is genuinely legal, so this path must not disturb anything.
            //
            //   Foreign: the number means nothing in Gen 3's space, so it is DERIVED from a real
            //   encounter in the destination instead of carried -- see gen3LandingFor.
            uint8_t metLocation;
            uint8_t metLevel;
            uint8_t version;
            if (originatedInGen3)
            {
                metLocation = static_cast<uint8_t>(rd16(sourceBytes, 0x122));
                metLevel = sourceMetLevel;
                version = sourceVersion;
            }
            else
            {
                // The form the PK3 will ACTUALLY report, not the source's field. For Unown that is
                // derived from the PID chosen above, so the chamber and the letter cannot disagree
                // even if the reroll ran out of budget and kept a letter it did not want. Every
                // other Gen 3 species is form 0 -- the format has nowhere else to put one.
                const uint8_t gen3Form = (national == 201) ? Pokemon::unownFormFromPID(outPid) : 0;
                // The level the PK3 will report, derived the way the record itself derives it: a
                // Gen 3 party record has no level byte, only EXP, and the EXP was copied across
                // verbatim a few lines up. The egg branch needs it, because an evolution level is
                // a claim about the CURRENT level rather than the met one.
                const uint8_t gen3Level = Pokemon::getLevelFromExp(rd32(destinationBytes, 0x24),
                                                                   Pokemon::getGrowthRate(national));
                const Gen3Landing landing =
                    gen3LandingFor(national, gen3Form, destOriginVersion, sourceMetLevel, gen3Level);
                metLocation = landing.metLocation;
                metLevel = landing.metLevel;
                version = landing.originVersion;
                // A PK3 has no level byte -- level IS the EXP -- so raising one means rewriting the
                // EXP the copy above carried across. Only the rebuild ever gets here with a level
                // that changed; with the toggle off landing.currentLevel is what came in.
                if (landing.currentLevel != gen3Level)
                {
                    wr32(destinationBytes, 0x24,
                         Pokemon::getExpForLevel(landing.currentLevel, Pokemon::getGrowthRate(national)));
                }
            }
            wr16(destinationBytes, 0x46,
                 static_cast<uint16_t>((metLevel & 0x7F) | ((version & 0x0F) << 7) | ((ball & 0x0F) << 11) |
                                       ((otGender & 1) << 15)));
            wr8(destinationBytes, 0x45, metLocation);

            // IVs: keep bits 0-29 (IVs) + bit30 (isEgg); set bit31 = ability slot from the PK8 ability number.
            const uint8_t abilNum = rd8(sourceBytes, 0x16); // 1=slot1, 2=slot2, 4=hidden (-> slot1 in Gen 3)
            wr32(destinationBytes, 0x48, (pk8iv & 0x7FFFFFFFu) | ((abilNum == 2) ? 0x80000000u : 0u));
            return destinationBytes;
        }

        // The ball a record should carry once it has landed in `destGroup`.
        //
        // HOME does not give a Pokemon one ball it takes everywhere. It keeps ONE PER GAME SIDE, and
        // each side decides what to do with a neighbouring side's when that side is created. Only
        // Sword/Shield converts: PKHeX's GameDataPK8 adopts a ball through `ball > Beast ? 4 : ball`,
        // while GameDataPB8, GameDataPK9 and GameDataPA9 each do a plain `Ball = side.Ball` and take
        // it unchanged. Generalised over every destination that is "stop at the destination format's
        // own ceiling, substituting the Poke Ball", which is what this does -- and it leaves BDSP,
        // S/V, Z-A and Legends: Arceus alone, because their ceiling is the top of the enum.
        uint8_t ballForDestination(uint8_t ball, GameVersion destGroup)
        {
            const uint8_t highestBall = Enums::maxBallForGroup(destGroup);
            return ball > highestBall ? static_cast<uint8_t>(Enums::Ball::Poke) : ball;
        }
    }

    bool canConvert(const Pokemon::Pokemon &source, GameVersion destGroup, Result &result)
    {
        result = gate(source, destGroup);
        return result == Result::Ok || result == Result::SameGroup;
    }

    std::unique_ptr<Pokemon::Pokemon> convert(const Pokemon::Pokemon &source, GameVersion destGroup, Result &result,
                                              uint8_t destOriginVersion, const ReceivingTrainer *receivingTrainer)
    {
        result = gate(source, destGroup);
        // SameGroup / NotInDex / Blocked / Unsupported
        if (result != Result::Ok) return nullptr;

        // 0 means the caller could not name the exact destination game; fall back to the group's
        // representative rather than writing a 0 origin.
        if (destOriginVersion == 0)
            destOriginVersion = Enums::getGroupRepVersion(destGroup);

        const GameVersion from = source.getGameGroup();

        // Copy the source's decrypted bytes; every field at a shared offset (identity, IVs/EVs, moves,
        // nickname/OT/HT, ribbons/marks, dates, ...) carries verbatim. Then fix only the regions that
        // differ: siblings null a couple of divergent fields; cross-generation runs the full PK8<->PK9 remap.
        std::vector<std::byte> buffer(source.getData().data(), source.getData().data() + source.getDataSize());

        bool seededPartyTail = false;   // a hub remap built this pokemon a fresh Gen 8/9 battle-stat tail
        bool viaHub = false;            // not a sibling pairing -> re-laid-out through the PK8 hub
        bool builtFreshRecord = false;  // the record was CONSTRUCTED, not remapped -> its stat tail is empty

        // Gen 1 and Gen 2 leave through Poke Transporter FIRST, whatever the destination is. That
        // step lands them in Sun/Moon's PK7 -- the only place they have ever been able to go -- and
        // from there the pokemon is an ordinary Gen 7 record: a Gen 7 destination is finished, and
        // anything later rides the same PK8 hub a real Sun/Moon Pokemon would. Doing it this way
        // rather than mapping PK1 straight onto a modern layout is what makes the invented fields
        // (nature, ability, IVs, met data) the ones Transporter would have invented.
        GameVersion layoutFrom = from;
        if (isVirtualConsole(from))
        {
            buffer = transportVirtualConsoleToPK7(source, static_cast<uint8_t>(virtualConsoleOriginVersion(source)),
                                                  receivingTrainer);
            layoutFrom = GameVersion::SM;
            // The PK7 was built from nothing, so its battle-stat tail is still zeros -- including the
            // level byte that PK7's level() reads straight out of it. Without this the pokemon arrives at
            // level 0 with no stats, which is the same trap the hub remaps hit.
            builtFreshRecord = true;
        }

        // Sibling fast paths keep the shared layout in place (preserve everything, null a few divergent
        // fields). Every other pairing normalizes the source to the PK8 layout, then denormalizes to the
        // destination -- so PB7 / PA8 / PK9 all interoperate through one hub.
        if (isG8(layoutFrom) && isG8(destGroup))
        { // SwSh <-> BDSP: drop the Technical-Record flags (BDSP has no TRs).
            for (size_t moveIndex = 0x127; moveIndex <= 0x134 && moveIndex < buffer.size(); ++moveIndex)
                buffer[moveIndex] = std::byte{0};
            // The one sibling pairing that does not simply preserve the ball: BDSP reaches Origin
            // and Sword/Shield stops at Beast, so a Hisui ball has to go on the way into SwSh.
            if (0x124 < buffer.size())
            {
                wr8(buffer, 0x124, ballForDestination(rd8(buffer, 0x124), destGroup));
            }
        }
        else if (isG7(layoutFrom) && isG7(destGroup))
        {
            // Sun/Moon <-> Ultra Sun/Ultra Moon: the same PK7 byte for byte. Nothing to null --
            // the two differ only in how far their dex, move and item spaces run, and the gate
            // above already refused a species Sun/Moon has no entry for. The ball needs no pass
            // either: both stop at Beast, so nothing a PK7 can hold is out of range in the other.
        }
        else if (isG9(layoutFrom) && isG9(destGroup))
        { // S/V <-> Z-A: drop the fields that diverge between PK9 and PA9. The ball is left alone --
          // both reach Origin, so no value one of them can hold is out of range in the other.
            for (size_t bufIndex = 0x94; bufIndex <= 0x9F && bufIndex < buffer.size(); ++bufIndex)
                buffer[bufIndex] = std::byte{0}; // Tera (PK9) / Plus-flags (PA9)
            for (size_t bufIndex = 0x4B; bufIndex <= 0x57 && bufIndex < buffer.size(); ++bufIndex)
                buffer[bufIndex] = std::byte{0}; // DLC-TM (PK9) / LevelBoost (PA9)
            // Alpha (PA9) / alignment (PK9)
            if (0x23 < buffer.size()) buffer[0x23] = std::byte{0};
        }
        else
        {
            viaHub = true;
            // 1. Normalize the source into the PK8 layout.
            if (isG9(layoutFrom))
                transformG9toG8(buffer); // PK9 -> PK8
            // PA8 -> PK8
            else if (layoutFrom == GameVersion::PLA) buffer = remapPA8toPK8(buffer);
            // PB7 -> PK8
            else if (layoutFrom == GameVersion::GG) buffer = remapPB7toPK8(buffer);
            // PK3 -> PK8
            else if (isGen3Group(layoutFrom)) buffer = remapPK3toPK8(buffer);
            // PK7 -> PK8 (a real Sun/Moon pokemon, or a Transporter result)
            else if (isG7(layoutFrom)) buffer = remapPK7toPK8(buffer);

            // 1b. Those three hub remaps emit a STORED-size (0x148) buffer -- no battle-stat tail. Handing
            // that to a Gen 8/9 entity makes level()/statXXX() index PAST the allocation, so the pokemon shows a
            // random level and garbage stats that change on every placement (and lands in the destination
            // slot with whatever bytes were already there). Grow to the party size and seed the level from
            // EXP: recalculateStats() reads level() first and bails on 0, so the tail needs a real level
            // before it can compute against it. (PA8/PB7/PK3 destinations are unaffected -- their remaps
            // already emit a full party record and their level() derives from EXP rather than a tail byte.)
            if ((isG8(destGroup) || isG9(destGroup)) && buffer.size() < PARTY_SIZE_G89)
            {
                buffer.resize(PARTY_SIZE_G89, std::byte{0});
                const uint16_t speciesId = rd16(buffer, 0x08);
                wr8(buffer, 0x148, Pokemon::getLevelFromExp(rd32(buffer, 0x10), Pokemon::getGrowthRate(speciesId)));
                seededPartyTail = true;
            }

            // 1c. Ball. The hub buffer is a PK8 LAYOUT but is not a PK8, and may still be carrying a
            // ball above Beast -- Legends: Arceus and BDSP both reach Origin, and the source's ball
            // rides in unchanged. Apply the DESTINATION's rule here, where the ball is always at
            // 0x124, and step 2's verbatim copies then land the right value at whatever offset the
            // destination keeps it at. Doing it during step 1 instead made every destination inherit
            // whichever source's rule happened to be written there.
            if (0x124 < buffer.size())
            {
                wr8(buffer, 0x124, ballForDestination(rd8(buffer, 0x124), destGroup));
            }

            // 2. Denormalize the PK8 layout into the destination.
            if (isG9(destGroup))
                transformG8toG9(buffer, source.speciesID(), source.form()); // PK8 -> PK9
            // PK8 -> PA8
            else if (destGroup == GameVersion::PLA) buffer = remapPK8toPA8(buffer);
            // PK8 -> PB7
            else if (destGroup == GameVersion::GG) buffer = remapPK8toPB7(buffer);
            // PK8 -> PK3
            else if (isGen3Group(destGroup)) buffer = remapPK8toPK3(buffer, destOriginVersion, destGroup);
            // PK8 -> PK7
            else if (isG7(destGroup)) buffer = remapPK8toPK7(buffer);
        }

        // Re-key into the destination format, rebuild the entity, refresh its checksum.
        const uint32_t encryptionConstant =
            Utils::readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(buffer.data()));
        std::byte *encryptedRecord =
            encryptFor(destGroup, std::span<const std::byte>(buffer.data(), buffer.size()), encryptionConstant);
        if (!encryptedRecord)
        {
            result = Result::Unsupported;
            return nullptr;
        }
        auto out = makePokemon(destGroup, std::span<const std::byte>(encryptedRecord, buffer.size()));
        delete[] encryptedRecord;
        if (!out)
        {
            result = Result::Unsupported;
            return nullptr;
        }

        // THE LANGUAGE SET GREW OVER TIME, and the byte is copied verbatim by every remap above, so a
        // record can land claiming a language its destination never shipped a game in. Legends: Z-A
        // introduced Latin American Spanish and nothing before it has that id; Gen 7 introduced both
        // Chinese scripts; Gen 4 introduced Korean. Clamp once, here, where the destination group is
        // known and every format has an entity that knows where its own language byte lives.
        //
        // The Gen 3 down-convert has already done this for itself and this is a no-op for it -- it
        // has to clamp BEFORE it writes a name, because the language is what picks the character
        // table the name is written through.
        out->setLanguage(Enums::safeLanguageForGroup(destGroup, out->language()));

        // Sanitize the moveset to what the destination game's species can legally learn: any move (or
        // relearn move) it can't -- illegal for the species, OR not present in that game at all -- is
        // cleared to empty. Required for LGPE, which turns a pokemon with an impossible move into a Bad Egg;
        // for the other games it just keeps the transfer to a legal moveset. Learnability is the per-game
        // pool from the learnset table. Applies to every cross-game conversion.
        {
            const uint16_t speciesId = out->speciesID();
            const uint8_t formValue = out->form();
            // A Poke Transporter transfer is the exception, and deliberately so: its moveset is
            // legal BECAUSE the Pokemon learned it in Gen 1/2, and no Gen 7+ learnset records that
            // -- a Yellow Pikachu really does know Surf, and running it through isLearnable() would
            // delete the very thing that makes it interesting. Bank does not regenerate movesets
            // either. The one filter that still applies is presence: a move the destination game
            // does not HAVE cannot be written into it whatever its history.
            //
            // ...EXCEPT WHEN THE DESTINATION IS GEN 3, because there is no history to protect. No
            // game has ever let a Gen 1/2 pokemon reach Gen 3, so PKSE is not carrying one there --
            // it is fabricating a native Gen 3 encounter for it (see gen3LandingFor), and a move
            // that encounter could never have taught is part of what makes the result illegal. The
            // rule is about OFFICIALLY SUPPORTED transfers: where one exists PKSE does what the
            // games do and leaves the moveset alone; where none does, the pokemon is being rebuilt
            // and has to be rebuilt legally.
            //
            // Gen 3's own two groups trade with each other freely, so that IS supported: an Emerald
            // tutor move has to survive a move into a FireRed save even though FireRed's own pool is
            // narrower, exactly as the cartridges behave.
            const bool sameGenerationTrade = isGen3Group(from) && isGen3Group(destGroup);
            const bool rebuildingIntoGen3 =
                isVirtualConsole(from) && isGen3Group(destGroup) && g_autoLegalizeTransfers;
            const bool transporterMoveset =
                (isVirtualConsole(from) && !rebuildingIntoGen3) || sameGenerationTrade;
            auto moveSurvives = [&](uint16_t moveId)
            {
                return transporterMoveset ? Names::isMovePresent(moveId, destGroup)
                                          : Pokemon::isLearnable(speciesId, formValue, destGroup, moveId);
            };
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                if (out->move(moveIndex) != 0 && !moveSurvives(out->move(moveIndex)))
                {
                    out->setMove(moveIndex, 0);
                    out->setMovePP(moveIndex, 0);
                    out->setMovePPUps(moveIndex, 0);
                }
                if (!isGen3Group(destGroup) && out->relearnMove(moveIndex) != 0 &&
                    // Gen 3 has no relearn moves (remap leaves them 0)
                    !moveSurvives(out->relearnMove(moveIndex))) out->setRelearnMove(moveIndex, 0);
            }

            // Compact the surviving moves upward. Clearing slot 2 of 4 otherwise leaves a HOLE,
            // and a gap mid-moveset is not a state the games produce: they pack moves from slot 1 and
            // treat the first empty slot as the end of the list. A pokemon that arrived as
            // [Tackle, --, Ember, --] could therefore read as knowing only Tackle. Each move carries its
            // own PP and PP-Ups with it, or the counts would end up attached to the wrong move.
            for (int destinationSlot = 0, source = 0; source < 4; ++source)
            {
                if (out->move(source) == 0)
                    continue;
                if (destinationSlot != source)
                {
                    out->setMove(destinationSlot, out->move(source));
                    out->setMovePP(destinationSlot, out->movePP(source));
                    out->setMovePPUps(destinationSlot, out->movePPUps(source));
                    out->setMove(source, 0);
                    out->setMovePP(source, 0);
                    out->setMovePPUps(source, 0);
                }
                ++destinationSlot;
            }

            // Clamp each surviving move's PP to what the DESTINATION game allows. The PP bytes
            // were copied across verbatim, but base PP is per-generation data: Scarlet/Violet's
            // Tackle has 35 PP and Legends: Arceus' has 30, so a transfer that kept the 35 lands
            // a pokemon showing 35/30. Current PP above max is not a state the games produce.
            // Only ever lowers -- a pokemon that arrives with PP to spare keeps it.
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                if (out->move(moveIndex) == 0)
                    continue;
                const uint8_t maxPowerPoints =
                    Names::getMoveMaxPP(out->move(moveIndex), out->movePPUps(moveIndex), destGroup);
                if (out->movePP(moveIndex) > maxPowerPoints)
                    out->setMovePP(moveIndex, maxPowerPoints);
            }
        }

        // Sanitize the held item the same way: drop anything the destination can't actually hold.
        // Let's Go and Legends: Arceus have NO held-item mechanic at all (PKHeX Legal.HeldItems_GG and
        // HeldItems_LA are both empty), and every other game has its own item space where an id that
        // doesn't exist is meaningless. `out` is already in the destination's format, so its held-item id
        // is in that game's own space -- Gen 3 ids for FRLG, modern ids everywhere else.
        if (out->heldItem() != 0 && !Names::isHeldItemPresent(out->heldItem(), destGroup))
            out->setHeldItem(0);

        // Fill the destination's (un-checksummed) battle-stat tail whenever this conversion created a fresh
        // one: Let's Go and Gen 3 always build theirs from scratch (they store Level + stats + Combat Power
        // there, so a zeroed tail shows Level/CP 0), a Gen 8/9 pokemon arriving through a hub remap had its tail
        // seeded above, and a PA8 built by remapPK8toPA8 starts with a zeroed tail too. A same-generation *sibling*
        // pairing carries the source's tail verbatim and needs nothing.
        const bool freshTail =
            seededPartyTail || builtFreshRecord || (viaHub && (destGroup == GameVersion::PLA || isG7(destGroup)));
        if (destGroup == GameVersion::GG || isGen3Group(destGroup) || freshTail)
            out->recalculateStats();

        // Heal to full if current HP reads 0. Current HP is NOT in the party stat tail -- it sits INSIDE the
        // stored (checksummed) region, so a hub remap that only maps the fields it knows about leaves it
        // zero and the converted pokemon arrives FAINTED in the destination game. Observed on hardware: a
        // FireRed Rattata in Shining Pearl with every stat correct but 0/15 HP. Offsets per PKHeX:
        // PK8/PB8/PK9/PA9 = 0x8A, PA8 = 0x92; PB7 and PK3 write their own in recalculateStats().
        // Guarded on "reads 0" so a sibling/transform path keeps the source's real (possibly damaged) HP.
        {
            size_t hpOfs = 0;
            if (isG8(destGroup) || isG9(destGroup))
                hpOfs = 0x8A;
            else if (destGroup == GameVersion::PLA)
                hpOfs = 0x92;
            if (hpOfs != 0 && hpOfs + 1 < out->getDataSize())
            {
                std::span<std::byte> destinationBytes = out->getData();
                const uint16_t currentHP =
                    static_cast<uint16_t>(static_cast<uint8_t>(destinationBytes[hpOfs])) |
                    (static_cast<uint16_t>(static_cast<uint8_t>(destinationBytes[hpOfs + 1])) << 8);
                if (currentHP == 0)
                {
                    const uint16_t full = out->statHPMax();
                    destinationBytes[hpOfs] = static_cast<std::byte>(full & 0xFF);
                    destinationBytes[hpOfs + 1] = static_cast<std::byte>((full >> 8) & 0xFF);
                }
            }
        }

        // AffixedRibbon. A source format without the field (PK3, PB7) never writes it, so the
        // destination inherits the 0 its buffer was zero-initialised with -- and 0 is a REAL index
        // naming the Kalos Champion ribbon, not "none" (that is 0xFF). Every pokemon arriving from FireRed
        // or Let's Go therefore displayed a ribbon it does not own. The same 0 also rides across a
        // Gen 8 <-> Gen 9 hop unchanged, so a pokemon banked before the creator was fixed keeps it.
        normalizeAffixedRibbon(*out);

        out->refreshChecksum(); // stored bytes changed -> recompute the entity checksum
        result = Result::Ok;
        return out;
    }

    bool normalizeAffixedRibbon(Pokemon::Pokemon &pokemon)
    {
        const size_t affix = affixedRibbonOffset(pokemon.getGameGroup());
        // format has no such field
        if (affix == 0) return false;
        std::span<std::byte> bytes = pokemon.getData();
        if (bytes.size() <= affix || bytes.size() < 0x46)
            return false;
        // already names something / None
        if (static_cast<uint8_t>(bytes[affix]) != 0) return false;
        // genuinely owns Kalos Champion
        if ((static_cast<uint8_t>(bytes[0x34]) & 0x01) != 0) return false;
        bytes[affix] = std::byte{AFFIXED_RIBBON_NONE};
        pokemon.refreshChecksum(); // the field is inside the checksummed region
        return true;
    }

    size_t affixedRibbonOffset(Enums::GameVersion group) noexcept
    {
        switch (group)
        {
        case GameVersion::SV:
        case GameVersion::ZA:
            return 0xD4; // PK9 / PA9
        case GameVersion::SWSH:
        case GameVersion::BDSP:
            return 0xE8; // G8PKM (PK8 / PB8)
        case GameVersion::PLA:
            return 0xF8; // PA8
        default:
            // FRLG (PK3), Let's Go (PB7) and the Gen 7 3DS games (PK7) have no such field -- Gen 8
            // introduced it along with the reordered ribbon block.
            return 0;
        }
    }

    bool canStoreOriginalTrainerName(const Pokemon::Pokemon &source, GameVersion destGroup)
    {
        // Every format but Gen 3 stores UTF-16 and can hold any name at all, so there is nothing
        // here for them to fail at -- this asks about a character table, and Gen 3 is the last
        // generation to have one.
        if (!isGen3Group(destGroup))
            return true;
        const std::u16string originalTrainerName = source.otName();
        // nothing to lose; an OT that is already blank is the checker's business
        if (originalTrainerName.empty()) return true;
        // The clamp has to happen here too, and for the same reason the conversion does it: the
        // language chooses the table, so asking the SOURCE's language would test a Korean record
        // against a table it will never actually be written through.
        const uint8_t destinationLanguage =
            Enums::safeLanguageForGroup(destGroup, source.language());
        int characterCount = 0;
        for (const char16_t character : originalTrainerName)
        {
            // past the field's width: truncated, not lost
            if (characterCount++ >= GEN3_OT_NAME_CHARS) break;
            if (Utils::charToGen3(character, destinationLanguage) == Utils::GEN3_TERMINATOR)
                return false;
        }
        return true;
    }

    bool virtualConsoleTransferInvolved(const Pokemon::Pokemon &source, GameVersion destGroup)
    {
        return isVirtualConsole(source.getGameGroup()) && !isVirtualConsole(destGroup);
    }

    GameVersion virtualConsoleOriginVersion(const Pokemon::Pokemon &source)
    {
        const GameVersion group = source.getGameGroup();
        // Crystal is the one Gen 1/2 title a record can name for itself: it is the only one that
        // writes caught data, so a PK2 carrying any is a Crystal Pokemon. PKHeX makes the same call.
        if (group == GameVersion::GSC && source.hasMetData())
            return GameVersion::C;
        if (isVirtualConsole(group))
            return static_cast<GameVersion>(Enums::getGroupRepVersion(group));
        return GameVersion::Invalid;
    }

    bool virtualConsoleOriginIsGuess(const Pokemon::Pokemon &source)
    {
        const GameVersion group = source.getGameGroup();
        if (!isVirtualConsole(group))
            return false;
        return !(group == GameVersion::GSC && source.hasMetData()); // Crystal names itself; nothing else does
    }

    const char *resultMessage(Result result)
    {
        switch (result)
        {
        case Result::Ok:
            return "Converted";
        case Result::SameGroup:
            return "Same game";
        case Result::NotInDex:
            return "Not obtainable in this game";
        // Names the way out: this one is lifted by a setting, unlike NotInDex which is absolute.
        case Result::Blocked:
            return "HOME can't transfer this species here (Allow Illegal Values overrides)";
        case Result::Unsupported:
            return "Transfer to/from this game isn't supported yet";
        }
        return "";
    }
}
