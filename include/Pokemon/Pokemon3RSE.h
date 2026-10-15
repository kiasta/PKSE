/**
 * THE RECORD IS BYTE-FOR-BYTE THE SAME AS FIRERED/LEAFGREEN'S, and this class is a deliberate copy
 * of Pokemon3FRLG rather than a shared class with a group flag -- the same call the codebase makes
 * for PK4 (three near-identical classes for DP/Pt/HGSS) and for PK9 (S/V and Z-A). What differs is
 * the one thing an entity class exists to answer: getGameGroup(), which reports the SAVE FORMAT a
 * record belongs to, and RSE and FRLG are two of those. That answer decides which learn pool a move
 * is checked against and which bank tag the record stores under, so a Ruby Pokemon reporting FRLG
 * would be checked against FireRed's TM list.
 *
 * Which of the five GBA games a pokemon actually came from is originGame() (SA=1, RU=2, EM=3, FR=4,
 * LG=5), read straight out of the record's Origins field -- that is a different question from the
 * format it is stored in, and the two must not be conflated.
 *
 * Wraps a DECRYPTED, canonically-ordered PK3 buffer (see Encryption3RSE): 32-byte header +
 * four 12-byte substructures G/A/E/M at fixed offsets 0x20/0x2C/0x38/0x44 + party stats. Gen 3
 * stores no nature / gender / ability / level -- those are derived from the PID (+ the personal
 * table). Species @0x20 is the Gen 3 INTERNAL id; speciesID() returns the National id.
 */
#ifndef POKEMON_POKEMON3_RSE_H
#define POKEMON_POKEMON3_RSE_H

#include <cstdint>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Enums/LanguageID.h" // the egg placeholder's language, and the default save language
#include "Pokemon/SpeciesConverter3.h" // the real Gen 3 internal <-> National tables (PKHeX-derived)
#include "Encryption/Encryption3RSE.h"

namespace Pokemon
{

    // Gen 3 internal <-> National dex species id. Thin aliases over SpeciesConverter3 -- the Hoenn
    // block is NOT a uniform shift, and assuming it was stored 109 of the 135 Hoenn species as a
    // different Pokemon. Names kept so existing call sites read unchanged.

    class Pokemon3RSE final : public Pokemon
    {
    public:
        explicit Pokemon3RSE(std::span<const std::byte> raw)
        {
            buffer = Encryption::decryptArray3RSE(raw); // decrypt + un-shuffle to canonical G/A/E/M
            dataSize = raw.size();
            data = std::span<std::byte>(buffer, dataSize);
        }
        ~Pokemon3RSE() override = default;
        Pokemon3RSE(const Pokemon3RSE &) = delete;
        Pokemon3RSE &operator=(const Pokemon3RSE &) = delete;
        Pokemon3RSE(Pokemon3RSE &&) noexcept = default;
        Pokemon3RSE &operator=(Pokemon3RSE &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            std::byte *encryptedRecord = Encryption::encryptArray3RSE(std::span<const std::byte>(data.data(), dataSize));
            auto copiedPokemon = std::make_unique<Pokemon3RSE>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::RSE; }

        uint8_t rd8(size_t offset) const noexcept { return static_cast<uint8_t>(data[offset]); }
        uint16_t rd16(size_t offset) const noexcept
        {
            return static_cast<uint16_t>(rd8(offset)) | (static_cast<uint16_t>(rd8(offset + 1)) << 8);
        }
        uint32_t rd32(size_t offset) const noexcept
        {
            return static_cast<uint32_t>(rd16(offset)) | (static_cast<uint32_t>(rd16(offset + 2)) << 16);
        }
        void wr8(size_t offset, uint8_t value) noexcept { data[offset] = static_cast<std::byte>(value); }
        void wr16(size_t offset, uint16_t value) noexcept
        {
            wr8(offset, static_cast<uint8_t>(value));
            wr8(offset + 1, static_cast<uint8_t>(value >> 8));
        }
        void wr32(size_t offset, uint32_t value) noexcept
        {
            wr16(offset, static_cast<uint16_t>(value));
            wr16(offset + 2, static_cast<uint16_t>(value >> 16));
        }

        uint32_t pid() const noexcept override { return rd32(0x00); }
        uint32_t encryptionConstant() const noexcept override { return rd32(0x00); } // Gen3 has no EC; PID is the seed
        uint16_t speciesID() const noexcept override { return gen3InternalToNational(rd16(0x20)); }
        const char *species() const noexcept override;
        uint8_t formID() const noexcept override { return form(); }
        uint8_t form() const noexcept override;                            // Unown (201) form from PID; else 0
        uint16_t heldItem() const noexcept override { return rd16(0x22); } // Gen3 item id
        uint32_t id32() const noexcept override { return rd32(0x04); }
        uint16_t tid16() const noexcept override { return rd16(0x04); }
        uint16_t sid16() const noexcept override { return rd16(0x06); }
        uint32_t exp() const noexcept override { return rd32(0x24); }
        uint8_t nature() const noexcept override { return static_cast<uint8_t>(pid() % 25); }
        uint8_t statNature() const noexcept override { return nature(); } // no mints in Gen3
        uint8_t level() const noexcept override;                          // derived from EXP
        uint8_t gender() const noexcept override;                         // derived from PID + species ratio
        const char *genderSymbol() const noexcept override
        {
            const uint8_t genderValue = gender();
            return genderValue == 0 ? "♂" : genderValue == 1 ? "♀" : "";
        }

        // Ability: Gen3 stores a selector BIT (IV32 bit31), never an ability id -- the game
        // resolves the bit through its own personal table, so the id comes from the Gen 3
        // table (getPersonalInfo3RSE), NOT the modern one. Consequence for editing: the only
        // abilities a PK3 can express are the species' own two slots, so "allow illegal edits"
        // cannot widen this list the way it can for every later game.
        uint16_t ability() const noexcept override;
        uint8_t abilityNumber() const noexcept override { return (rd32(0x48) >> 31) & 1 ? 2 : 1; }
        void setAbility(uint16_t abilityValue) noexcept override; // id -> slot, then setAbilityNumber
        void setAbilityNumber(uint8_t number) noexcept override;  // 1/2; flips the bit + re-rolls the PID

        // Decode a Gen 3 name field: maxChars bytes at `offset`, stopping at the 0xFF terminator.
        std::u16string readG3Name(size_t offset, int maxChars, uint8_t languageId) const;
        std::u16string nickname() const override;                         // Gen3 char table @0x08 (10 bytes)
        void setNickname(const std::u16string &value) noexcept override;  // encode into the Gen3 table @0x08
        int getMaxNicknameLength() const noexcept override { return 10; } // 10 bytes @0x08, 1 byte per glyph
        bool canStoreNickname(const std::u16string &value) const noexcept override;
        std::u16string otName() const override;                        // Gen3 char table @0x14 (7 bytes)
        void setOTName(const std::u16string &value) noexcept override; // encode into the Gen3 table @0x14

        uint16_t move(int slot) const noexcept override { return (slot < 0 || slot > 3) ? 0 : rd16(0x2C + slot * 2); }
        uint8_t movePP(int slot) const noexcept override { return (slot < 0 || slot > 3) ? 0 : rd8(0x34 + slot); }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot < 0 || slot > 3) ? 0 : (rd8(0x28) >> (slot * 2)) & 0x03;
        }
        void setMove(int slot, uint16_t moveId) noexcept override
        {
            if (slot >= 0 && slot <= 3)
            {
                wr16(0x2C + slot * 2, moveId);
                refreshChecksum();
            }
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot >= 0 && slot <= 3)
            {
                wr8(0x34 + slot, powerPoints);
                refreshChecksum();
            }
        }
        void setMovePPUps(int slot, uint8_t ppUps) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            uint8_t packedByte = (rd8(0x28) & ~(0x03 << (slot * 2))) | ((ppUps & 0x03) << (slot * 2));
            wr8(0x28, packedByte);
            refreshChecksum();
        }

        uint32_t iv32() const noexcept { return rd32(0x48); }
        uint8_t ivHP() const noexcept override { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }
        void setIV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex < 0 || statIndex >= 6 || value > 31)
                return;
            uint32_t ivValue = iv32();
            const int shift = statIndex * 5;
            ivValue = (ivValue & ~(0x1Fu << shift)) | (static_cast<uint32_t>(value & 0x1F) << shift);
            wr32(0x48, ivValue);
            recalculateStats(); // IVs feed the stat formula: keep the party tail in step
            refreshChecksum();
        }

        uint8_t evHP() const noexcept override { return rd8(0x38); }
        uint8_t evATK() const noexcept override { return rd8(0x39); }
        uint8_t evDEF() const noexcept override { return rd8(0x3A); }
        uint8_t evSPE() const noexcept override { return rd8(0x3B); }
        uint8_t evSPA() const noexcept override { return rd8(0x3C); }
        uint8_t evSPD() const noexcept override { return rd8(0x3D); }
        void setEV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6)
            {
                wr8(0x38 + statIndex, value);
                recalculateStats(); // EVs feed the stat formula: keep the party tail in step
                refreshChecksum();
            }
        }

        uint16_t origins() const noexcept { return rd16(0x46); }
        uint8_t originGame() const noexcept override { return (origins() >> 7) & 0x0F; }
        uint8_t metLevel() const noexcept override { return origins() & 0x7F; }
        uint8_t otGender() const noexcept override { return (origins() >> 15) & 0x01; }
        uint16_t metLocation() const noexcept override { return rd8(0x45); }
        uint8_t ball() const noexcept override { return (origins() >> 11) & 0x0F; }
        uint8_t otFriendship() const noexcept override { return rd8(0x29); }
        uint8_t language() const noexcept override { return rd8(0x12); }
        /// A Gen 3 egg's nickname and language byte are a placeholder the games stamp on every egg,
        /// in every region -- see Pokemon::eggTextIsPlaceholder.
        bool eggTextIsPlaceholder() const noexcept override { return isEgg(); }
        /// The table an OT name is read and written through. The record's own language, except on an
        /// egg: that byte always says Japanese, while the OT bytes beside it are still the cartridge's
        /// own charset, so the SAVE's language is what renders them.
        uint8_t otTextLanguage() const noexcept { return eggTextIsPlaceholder() ? saveLanguage : language(); }
        /// Tell this record which save it came from. Only an egg needs it; a bank record has no save
        /// and keeps the default, which is the charset five of Gen 3's six releases share.
        void setSaveLanguage(uint8_t languageId) noexcept { saveLanguage = languageId; }
        uint8_t friendship() const noexcept override { return rd8(0x29); }
        uint8_t pokerus() const noexcept { return rd8(0x44); }
        bool isPokerusInfected() const noexcept override { return (pokerus() & 0x0F) != 0; }
        bool isPokerusCured() const noexcept override { return (pokerus() & 0xF0) != 0 && (pokerus() & 0x0F) == 0; }
        void setPokerus(uint8_t value) noexcept override
        {
            wr8(0x44, value);
            refreshChecksum();
        }
        bool hasPokerus() const noexcept override { return true; }

        bool isEgg() const noexcept override { return ((iv32() >> 30) & 1) != 0; }
        /// A PK3 records no date and no egg location -- PKHeX seals G3PKM.EggLocation at 0.
        /// A Gen 3 egg is recognisable only by its met LEVEL (0) and where it hatched.
        bool hasMetDate() const noexcept override { return false; }
        bool hasEggData() const noexcept override { return false; }
        bool isShiny(uint32_t trainerID32, std::string) const noexcept override
        {
            const uint32_t pidValue = pid();
            const uint16_t trainerShinyValue = static_cast<uint16_t>((trainerID32 & 0xFFFF) ^ (trainerID32 >> 16));
            const uint16_t pokemonShinyValue = static_cast<uint16_t>((pidValue & 0xFFFF) ^ (pidValue >> 16));
            return (trainerShinyValue ^ pokemonShinyValue) < 8;
        }

        uint8_t baseHP() const noexcept override;
        uint8_t baseATK() const noexcept override;
        uint8_t baseDEF() const noexcept override;
        uint8_t baseSPE() const noexcept override;
        uint8_t baseSPA() const noexcept override;
        uint8_t baseSPD() const noexcept override;
        uint16_t statHPMax() const noexcept override;
        uint16_t statATK() const noexcept override;
        uint16_t statDEF() const noexcept override;
        uint16_t statSPE() const noexcept override;
        uint16_t statSPA() const noexcept override;
        uint16_t statSPD() const noexcept override;
        // Current HP: the raw stored value @0x56, reported verbatim. Only the party record (100 B)
        // has the field; a box record is 80 B and Gen 3 heals a Pokemon when it leaves the PC, so
        // full health is the only answer available there.
        uint16_t statHPCurrent() const noexcept override;
        void setStatHPCurrent(uint16_t value) noexcept override;
        static uint16_t carryCurrentHP(uint16_t storedCur, uint16_t storedMax, uint16_t newMax) noexcept;

        uint16_t checksum() const noexcept override { return rd16(0x1C); }
        uint16_t calculateChecksum() const noexcept override
        {
            return Encryption::checksum3RSE(std::span<const std::byte>(data.data(), dataSize));
        }
        void refreshChecksum() noexcept override { wr16(0x1C, calculateChecksum()); }
        bool checksumValid() const noexcept override { return checksum() == calculateChecksum(); }

        void setSpecies(uint16_t national) noexcept override
        {
            wr16(0x20, gen3NationalToInternal(national));
            // set HasSpecies flag
            if (national != 0) wr8(0x13, rd8(0x13) | 0x02);
            recalculateStats();
            refreshChecksum();
        }
        void setForm(uint8_t) noexcept override {} // Gen3 forms (Unown/Deoxys) are PID/other-derived
        void setHeldItem(uint16_t item) noexcept override
        {
            wr16(0x22, item);
            refreshChecksum();
        }
        void setPID(uint32_t pidValue) noexcept override
        {
            wr32(0x00, pidValue);
            recalculateStats();
            refreshChecksum();
        }
        void setEncryptionConstant(uint32_t pidValue) noexcept override { setPID(pidValue); }
        void setFriendship(uint8_t value) noexcept override
        {
            wr8(0x29, value);
            refreshChecksum();
        }
        void setOTFriendship(uint8_t value) noexcept override
        {
            wr8(0x29, value);
            refreshChecksum();
        }
        void setTID16(uint16_t value) noexcept override
        {
            wr16(0x04, value);
            refreshChecksum();
        }
        void setSID16(uint16_t value) noexcept override
        {
            wr16(0x06, value);
            refreshChecksum();
        }
        void setId32(uint32_t value) noexcept override
        {
            wr32(0x04, value);
            refreshChecksum();
        }
        void setLanguage(uint8_t value) noexcept override
        {
            wr8(0x12, value);
            refreshChecksum();
        }
        void setBall(uint8_t value) noexcept override
        {
            wr16(0x46, (origins() & ~(0x0Fu << 11)) | ((value & 0x0Fu) << 11));
            refreshChecksum();
        }
        void setMetLevel(uint8_t value) noexcept override
        {
            wr16(0x46, (origins() & ~0x7Fu) | (value & 0x7Fu));
            refreshChecksum();
        }
        void setMetLocation(uint16_t value) noexcept override
        {
            wr8(0x45, static_cast<uint8_t>(value));
            refreshChecksum();
        }
        void setOriginGame(uint8_t value) noexcept override
        {
            wr16(0x46, (origins() & ~(0x0Fu << 7)) | ((value & 0x0Fu) << 7));
            refreshChecksum();
        }
        void setOTGender(uint8_t value) noexcept override
        {
            wr16(0x46, (origins() & ~(1u << 15)) | ((value & 1u) << 15));
            refreshChecksum();
        }
        void setEgg(bool isEgg) noexcept override
        {
            uint32_t ivValue = iv32();
            if (isEgg)
                ivValue |= (1u << 30);
            else
                ivValue &= ~(1u << 30);
            wr32(0x48, ivValue);
            // THE EGG BIT IS NOT THE WHOLE EGG. The games stamp three things together and PKHeX
            // writes the same three in PK3.IsEgg: the sanity flag at 0x13 bit 2, the Japanese word
            // for egg as the nickname, and language 1 -- on every cartridge, whatever its region.
            // Setting the bit alone left a record no real save holds, which is also what the
            // legality rule compares against (PKHeX checks the nickname against the egg name in the
            // record's OWN language, and an egg says Japanese).
            wr8(0x13, static_cast<uint8_t>(isEgg ? (rd8(0x13) | 0x04) : (rd8(0x13) & ~0x04)));
            if (isEgg)
            {
                setLanguage(static_cast<uint8_t>(Enums::LanguageID::Japanese));
                setNickname(EGG_NICKNAME_JAPANESE);
            }
            refreshChecksum();
        }
        void setLevel(uint8_t level) noexcept override;   // writes EXP for the level, recalcs
        void setNature(uint8_t nature) noexcept override; // re-rolls PID to the nature (keeps gender + shiny)
        void setGender(uint8_t gender) noexcept override; // re-rolls PID to the gender (keeps nature + shiny)
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;
        void regeneratePID(uint32_t trainerID32) noexcept override;
        void recalculateStats() noexcept override;

    private:
        /// What the games write as an egg's nickname, in every region: the Japanese word for egg.
        /// PKHeX spells it the same way (PK3.EggNameJapanese).
        static constexpr const char16_t *EGG_NICKNAME_JAPANESE = u"\u30bf\u30de\u30b4"; // タマゴ
        /// The language of the SAVE this record was read from. See otTextLanguage.
        uint8_t saveLanguage = static_cast<uint8_t>(Enums::LanguageID::English);

        // Compute a battle stat (0=HP..5=SPD) from base/IV/EV/level/nature (Gen3 formula).
        uint16_t computeStat(int index) const noexcept;
        // Re-roll the PID (bounded search) to satisfy the given constraints; each is -1 for "don't care".
        // Used by the nature/gender/shiny/ability setters -- all PID-derived in Gen 3. wantAbilityBit
        // constrains the PID's low bit, which Gen 3 legality requires to match the stored ability bit
        // (PKHeX AbilityVerifier.GetPIDAbilityMatch). No-op if no PID satisfies them.
        void rerollPID(int wantShiny, int wantGender, int wantNature, int wantAbilityBit = -1) noexcept;
        // rerollPID that also holds the pokemon's CURRENT shininess, retrying without that constraint if
        // it can't be met in budget. Editing one PID-derived field must not quietly change the others,
        // but the edit the user asked for still has to land -- so shiny is the constraint that yields.
        void rerollPreservingShiny(int wantGender, int wantNature, int wantAbilityBit = -1) noexcept;
        // The PID low bit a re-roll has to preserve so that editing some OTHER PID-derived field
        // cannot desync the PID from the stored ability bit. -1 (unconstrained) unless the species
        // has two distinct abilities -- Gen 3 only ties the two together when there is a real choice.
        int abilityPidBit() const noexcept;
    };
}

#endif // POKEMON_POKEMON3_RSE_H
