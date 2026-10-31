/**
 * Layout: 8-byte header, four 32-byte blocks (0x08-0x87), then an 84-byte party tail.
 *   stored 136 (0x88)   party 220 (0xDC)   <- Gen 4's party is 236; the STORED size is the same
 *
 * The crypt is the Gen 4/5 one (Encryption5B2W2): body keyed on the CHECKSUM, party tail on the
 * PID, so refreshChecksum() must run BEFORE encrypting. The IV word at 0x38 carries the egg and
 * nicknamed flags exactly as in Gen 4 -- read-modify-write.
 *
 * FOUR THINGS DIFFER FROM PK4, and three of them are places a copied class quietly stays wrong:
 *
 *  1. NATURE IS STORED, at 0x41. Gen 3 and Gen 4 derive it from `PID % 25`; Gen 5 does not. So
 *     setNature() is a plain write for a Pokemon from Gen 5. One that came from Gens 3-4 still
 *     has to keep `PID % 25` equal to it, so for those the PID is rebuilt as well.
 *  2. HIDDEN ABILITY IS A FLAG, at 0x42 bit 0 (bit 1 is N's Sparkle). Gen 5 introduced Dream World
 *     abilities, so abilityNumber() cannot be inferred from the PID the way Gen 4's is.
 *  3. ONE BALL FIELD (0x83). Gen 4's DP/Pt-vs-HGSS two-field split does not exist here.
 *  4. TEXT IS REAL UTF-16. Gen 4 stores indices into a code page; Gen 5 stores the code units
 *     themselves. Routing Gen 5 names through Gen4Text mangles every one of them into CJK
 *     punctuation while still looking like text, so the two must not share a converter.
 *
 * Byte 0x87 is Pokestar Fame here, where Gen 4 has WalkingMood. Neither is edited; both are
 * preserved.
 */
#ifndef POKEMON_POKEMON5_B2W2_H
#define POKEMON_POKEMON5_B2W2_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/PersonalInfo5B2W2.h"
#include "Encryption/Encryption5B2W2.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

namespace Pokemon
{

    class Pokemon5B2W2 final : public Pokemon
    {
    public:
        /// Takes ENCRYPTED bytes, 136 or 236. A length that is neither is normalised to a blank
        /// party-size record rather than rejected -- the base class has no way to report a failed
        /// construction -- so the object is always safe to read; isStructurallyValid() reports it.
        explicit Pokemon5B2W2(std::span<const std::byte> raw)
        {
            // isSize() accepts only THIS group's two lengths, so there is nothing to
            // exclude. The shared module needed an exclusion because its size test also
            // accepted the other generation's party length.
            if (Encryption::isSize5B2W2(raw.size()))
            {
                dataSize = raw.size();
                buffer = Encryption::decryptArray5B2W2(raw);
            }
            else
            {
                dataSize = Encryption::SIZE_PARTY5_B2W2;
                buffer = new std::byte[dataSize];
                std::memset(buffer, 0, dataSize);
            }
            data = std::span<std::byte>(buffer, dataSize);
        }

        /// A blank party-size record, for the creator and the conversion path.
        Pokemon5B2W2()
        {
            dataSize = Encryption::SIZE_PARTY5_B2W2;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            data = std::span<std::byte>(buffer, dataSize);
        }

        ~Pokemon5B2W2() override = default;
        Pokemon5B2W2(const Pokemon5B2W2 &) = delete;
        Pokemon5B2W2 &operator=(const Pokemon5B2W2 &) = delete;
        Pokemon5B2W2(Pokemon5B2W2 &&) noexcept = default;
        Pokemon5B2W2 &operator=(Pokemon5B2W2 &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            std::byte *encryptedRecord = Encryption::encryptArray5B2W2(std::span<const std::byte>(data.data(), dataSize));
            auto copiedPokemon = std::make_unique<Pokemon5B2W2>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        /// The STORAGE FORMAT group, not the origin game. See the header note.
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::B2W2; }

        uint8_t rd8(size_t offset) const noexcept { return offset < dataSize ? static_cast<uint8_t>(data[offset]) : 0; }
        uint16_t rd16(size_t offset) const noexcept
        {
            return (offset + 1 < dataSize) ? Utils::readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + offset)) : 0;
        }
        uint32_t rd32(size_t offset) const noexcept
        {
            return (offset + 3 < dataSize) ? Utils::readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + offset)) : 0;
        }
        // Every write into the checksummed blocks refreshes the checksum, so no setter leaves the
        // entity holding a stale one until the next save. The bank stores the live bytes as they
        // are, and its loader drops any record whose checksum fails.
        void wr8(size_t offset, uint8_t value) noexcept
        {
            if (offset < dataSize)
            {
                data[offset] = static_cast<std::byte>(value);
                refreshChecksumAfterWrite(offset);
            }
        }
        void wr16(size_t offset, uint16_t value) noexcept
        {
            if (offset + 1 < dataSize)
            {
                Utils::writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + offset), value);
                refreshChecksumAfterWrite(offset);
            }
        }
        void wr32(size_t offset, uint32_t value) noexcept
        {
            if (offset + 3 < dataSize)
            {
                Utils::writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + offset), value);
                refreshChecksumAfterWrite(offset);
            }
        }

        bool isPartySize() const noexcept { return dataSize >= Encryption::SIZE_PARTY5_B2W2; }

        uint32_t pid() const noexcept override { return rd32(0x00); }
        void setPID(uint32_t value) noexcept override { wr32(0x00, value); }
        /// Gen 5 has no separate EncryptionConstant; PKHeX aliases it to the PID.
        uint32_t encryptionConstant() const noexcept override { return pid(); }
        void setEncryptionConstant(uint32_t) noexcept override {}
        uint16_t sanity() const noexcept { return rd16(0x04); }
        uint16_t checksum() const noexcept override { return rd16(0x06); }

        uint16_t speciesID() const noexcept override { return rd16(0x08); }
        void setSpecies(uint16_t value) noexcept override { wr16(0x08, value); }
        const char *species() const noexcept override;
        uint16_t heldItem() const noexcept override { return rd16(0x0A); }
        void setHeldItem(uint16_t value) noexcept override { wr16(0x0A, value); }
        uint32_t id32() const noexcept override { return rd32(0x0C); }
        void setId32(uint32_t value) noexcept override { wr32(0x0C, value); }
        uint16_t tid16() const noexcept override { return rd16(0x0C); }
        void setTID16(uint16_t value) noexcept override { wr16(0x0C, value); }
        uint16_t sid16() const noexcept override { return rd16(0x0E); }
        void setSID16(uint16_t value) noexcept override { wr16(0x0E, value); }
        uint32_t exp() const noexcept override { return rd32(0x10); }
        void setExp(uint32_t value) noexcept override;
        uint8_t friendship() const noexcept override { return rd8(0x14); }
        void setFriendship(uint8_t value) noexcept override { wr8(0x14, value); }
        uint8_t otFriendship() const noexcept override { return rd8(0x14); }
        void setOTFriendship(uint8_t value) noexcept override { wr8(0x14, value); }
        uint16_t ability() const noexcept override { return rd8(0x15); }
        void setAbility(uint16_t value) noexcept override { wr8(0x15, static_cast<uint8_t>(value)); }
        /// PKHeX PK5.AbilityNumber => HiddenAbility ? 4 : 1 << PIDAbility. 4 sets the hidden flag;
        /// 1 or 2 clears it and rebuilds the PID to hold the slot bit (bit 16, or bit 0 for a
        /// Pokemon from Gens 3-4).
        void setAbilityNumber(uint8_t abilityNumber) noexcept override;
        uint8_t language() const noexcept override { return rd8(0x17); }
        void setLanguage(uint8_t value) noexcept override { wr8(0x17, value); }

        uint8_t evHP() const noexcept override { return rd8(0x18); }
        uint8_t evATK() const noexcept override { return rd8(0x19); }
        uint8_t evDEF() const noexcept override { return rd8(0x1A); }
        uint8_t evSPE() const noexcept override { return rd8(0x1B); }
        uint8_t evSPA() const noexcept override { return rd8(0x1C); }
        uint8_t evSPD() const noexcept override { return rd8(0x1D); }
        void setEV(int statIndex, uint8_t value) noexcept override
        {
            // Accessor order: HP ATK DEF SPE SPA SPD -- the same order the bytes sit in.
            if (statIndex >= 0 && statIndex < 6)
                wr8(0x18 + statIndex, value);
        }

        uint16_t move(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd16(0x28 + slot * 2) : 0;
        }
        void setMove(int slot, uint16_t moveId) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr16(0x28 + slot * 2, moveId);
        }
        uint8_t movePP(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x30 + slot) : 0;
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x30 + slot, powerPoints);
        }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x34 + slot) : 0;
        }
        void setMovePPUps(int slot, uint8_t value) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x34 + slot, value);
        }

        uint32_t iv32() const noexcept { return rd32(0x38); }
        /// Read-modify-write ONLY. Bits 30 and 31 are the egg and nicknamed flags -- rebuilding
        /// this word from six IV values silently hatches every egg in the box.
        void setIV32(uint32_t value) noexcept { wr32(0x38, value); }
        uint8_t ivHP() const noexcept override { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }
        void setIV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex < 0 || statIndex > 5)
                return;
            static constexpr int IV_BIT_SHIFTS[6] = {0, 5, 10, 15, 20, 25}; // HP ATK DEF SPE SPA SPD
            const uint32_t newValue = value > 31 ? 31u : value;
            const int bitShift = IV_BIT_SHIFTS[statIndex];
            setIV32((iv32() & ~(0x1Fu << bitShift)) | (newValue << bitShift));
        }

        bool isEgg() const noexcept override { return ((iv32() >> 30) & 1) != 0; }
        void setEgg(bool isEgg) noexcept override
        {
            setIV32((iv32() & ~0x40000000u) | (isEgg ? 0x40000000u : 0u));
        }
        bool isNicknamed() const noexcept override { return ((iv32() >> 31) & 1) != 0; }
        void setIsNicknamed(bool value) noexcept override
        {
            setIV32((iv32() & 0x7FFFFFFFu) | (value ? 0x80000000u : 0u));
        }

        bool isFatefulEncounter() const noexcept override { return (rd8(0x40) & 1) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & ~0x01) | (value ? 1 : 0)));
        }
        uint8_t gender() const noexcept override { return (rd8(0x40) >> 1) & 0x03; }
        void setGender(uint8_t genderValue) noexcept override; // holds the PID to the new gender
        uint8_t formID() const noexcept override { return rd8(0x40) >> 3; }
        uint8_t form() const noexcept override { return formID(); }
        void setForm(uint8_t form) noexcept override
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & 0x07) | (form << 3)));
        }

        std::u16string nickname() const override { return readString(0x48, 11); }
        void setNickname(const std::u16string &value) noexcept override { writeString(0x48, 11, value); }
        int getMaxNicknameLength() const noexcept override { return 10; }
        bool canStoreNickname(const std::u16string &value) const noexcept override;
        uint8_t originGame() const noexcept override { return rd8(0x5F); }
        void setOriginGame(uint8_t value) noexcept override { wr8(0x5F, value); }

        std::u16string otName() const override { return readString(0x68, 8); }
        void setOTName(const std::u16string &value) noexcept override { writeString(0x68, 8, value); }
        uint8_t eggYear() const noexcept override { return rd8(0x78); }
        uint8_t eggMonth() const noexcept override { return rd8(0x79); }
        uint8_t eggDay() const noexcept override { return rd8(0x7A); }
        void setEggYear(uint8_t value) noexcept override { wr8(0x78, value); }
        void setEggMonth(uint8_t value) noexcept override { wr8(0x79, value); }
        void setEggDay(uint8_t value) noexcept override { wr8(0x7A, value); }
        uint8_t metYear() const noexcept override { return rd8(0x7B); }
        uint8_t metMonth() const noexcept override { return rd8(0x7C); }
        uint8_t metDay() const noexcept override { return rd8(0x7D); }
        void setMetYear(uint8_t value) noexcept override { wr8(0x7B, value); }
        void setMetMonth(uint8_t value) noexcept override { wr8(0x7C, value); }
        void setMetDay(uint8_t value) noexcept override { wr8(0x7D, value); }
        uint16_t eggLocation() const noexcept override { return rd16(0x7E); }
        void setEggLocation(uint16_t value) noexcept override { wr16(0x7E, value); }
        uint16_t metLocation() const noexcept override { return rd16(0x80); }
        void setMetLocation(uint16_t value) noexcept override { wr16(0x80, value); }

        bool isPokerusInfected() const noexcept override { return (rd8(0x82) & 0xF) != 0; }
        bool isPokerusCured() const noexcept override { return (rd8(0x82) & 0xF) == 0 && (rd8(0x82) >> 4) != 0; }
        bool hasPokerus() const noexcept override { return true; }
        void setPokerus(uint8_t value) noexcept override { wr8(0x82, value); }

        uint8_t ball() const noexcept override { return rd8(0x83); }
        void setBall(uint8_t value) noexcept override { wr8(0x83, value); }

        uint8_t metLevel() const noexcept override { return rd8(0x84) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            wr8(0x84, static_cast<uint8_t>((rd8(0x84) & 0x80) | (value & 0x7F)));
        }
        uint8_t otGender() const noexcept override { return rd8(0x84) >> 7; }
        void setOTGender(uint8_t value) noexcept override
        {
            wr8(0x84, static_cast<uint8_t>((rd8(0x84) & 0x7F) | ((value & 1) << 7)));
        }

        uint8_t level() const noexcept override { return isPartySize() ? rd8(0x8C) : 0; }
        void setLevel(uint8_t levelValue) noexcept override;
        uint16_t statHPCurrent() const noexcept override { return isPartySize() ? rd16(0x8E) : 0; }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            if (isPartySize())
                wr16(0x8E, value);
        }
        uint16_t statHPMax() const noexcept override { return isPartySize() ? rd16(0x90) : 0; }
        uint16_t statATK() const noexcept override { return isPartySize() ? rd16(0x92) : 0; }
        uint16_t statDEF() const noexcept override { return isPartySize() ? rd16(0x94) : 0; }
        uint16_t statSPE() const noexcept override { return isPartySize() ? rd16(0x96) : 0; }
        uint16_t statSPA() const noexcept override { return isPartySize() ? rd16(0x98) : 0; }
        uint16_t statSPD() const noexcept override { return isPartySize() ? rd16(0x9A) : 0; }

        uint8_t nature() const noexcept override { return rd8(0x41); }
        uint8_t statNature() const noexcept override { return nature(); }
        /// A plain write for a Pokemon from Gen 5. One from Gens 3-4 must keep `PID % 25` equal to
        /// its nature (PKHeX GenderVerifier.VerifyNaturePID), so the PID is rebuilt for it too,
        /// holding shininess, gender and the ability slot.
        void setNature(uint8_t nature) noexcept override;
        void setStatNature(uint8_t) noexcept override {} // no mints before Gen 8

        /// Dream World ability flag (0x42 bit 0). Gen 5 is where hidden abilities start.
        bool hasHiddenAbility() const noexcept { return (rd8(0x42) & 1) != 0; }
        void setHiddenAbility(bool enabled) noexcept
        {
            wr8(0x42, static_cast<uint8_t>((rd8(0x42) & ~0x01) | (enabled ? 1 : 0)));
        }
        uint8_t abilityNumber() const noexcept override;
        const char *genderSymbol() const noexcept override;

        bool hasNature() const noexcept override { return true; }
        bool hasAbility() const noexcept override { return true; }
        bool hasHeldItem() const noexcept override { return true; }
        bool hasFriendship() const noexcept override { return true; }
        bool hasBall() const noexcept override { return true; }
        bool hasMetData() const noexcept override { return true; }
        bool hasOriginGame() const noexcept override { return true; }
        bool hasPID() const noexcept override { return true; }
        bool hasStoredGender() const noexcept override { return true; }
        bool hasHyperTraining() const noexcept override { return false; } // Gen 7 onward only

        uint8_t baseHP() const noexcept override { return personal().hp; }
        uint8_t baseATK() const noexcept override { return personal().atk; }
        uint8_t baseDEF() const noexcept override { return personal().def; }
        uint8_t baseSPE() const noexcept override { return personal().spe; }
        uint8_t baseSPA() const noexcept override { return personal().spa; }
        uint8_t baseSPD() const noexcept override { return personal().spd; }

        uint16_t calculateChecksum() const noexcept override;
        void refreshChecksum() noexcept override { wr16(0x06, calculateChecksum()); }
        bool checksumValid() const noexcept override { return checksum() == calculateChecksum(); }
        bool isStructurallyValid() const noexcept override
        {
            return sanity() == 0 && checksumValid() && speciesID() != 0;
        }

        void recalculateStats() noexcept override;
        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override;
        void regeneratePID(uint32_t trainerID32) noexcept override;
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;

    private:
        /// The checksum covers the four blocks: 0x08 up to the stored size.
        static constexpr size_t CHECKSUM_START_OFFSET = 0x08;
        void refreshChecksumAfterWrite(size_t offset) noexcept
        {
            if (offset >= CHECKSUM_START_OFFSET && offset < Encryption::SIZE_STORED5_B2W2)
                refreshChecksum();
        }
        /// Gens 3-4 derive the nature from the PID and keep the ability slot in bit 0; Gen 5
        /// stores the nature and keeps the slot in bit 16 (PKHeX PKM.PIDAbility).
        bool pidHoldsNature() const noexcept;
        int abilityBitIndex() const noexcept { return pidHoldsNature() ? 0 : 16; }
        // A new PID holding what the caller does not change; false when none can. `wantNature` and
        // `wantAbilityBit` of -1 hold the current ones.
        bool rerollPID(int wantShiny, int wantGender, int wantNature, int wantAbilityBit, uint32_t trainerID32) noexcept;
        void writeStoredGender(uint8_t genderValue) noexcept
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & ~0x06) | ((genderValue & 3) << 1)));
        }
        const PersonalRecord &personal() const noexcept
        {
            return getPersonalInfo5B2W2(speciesID(), form());
        }
        int natureModifier(int statIndex) const noexcept;
        std::u16string readString(size_t offset, size_t units) const;
        void writeString(size_t offset, size_t units, const std::u16string &value) noexcept;
    };
}

#endif
