/**
 * Layout: 8-byte header, four 56-byte blocks (0x08-0xE7), then a 28-byte party tail.
 *   stored 232 (0xE8)   party 260 (0x104)
 *
 * This is where the MODERN format begins, and three things become true here that stay true
 * through Gen 9 -- so most of this class reads like the Gen 8/9 ones rather than like PK4/PK5:
 *
 *  - The crypt is seeded on the ENCRYPTION CONSTANT and is the same for the body and the party
 *    tail (Encryption7SM). The checksum is NOT the key, so the familiar order applies:
 *    recalculateStats() -> refreshChecksum() -> encrypt.
 *  - Text is real UTF-16.
 *  - Nature, ability slot, and gender are all stored fields.
 *
 * FOUR THINGS ARE NEW versus Gen 5 and are what most of the added surface here is for:
 *
 *  1. ENCRYPTION CONSTANT (0x00) IS A SEPARATE FIELD FROM THE PID (0x18). Form correlations key
 *     on the EC, not the PID.
 *  2. ABILITY NUMBER (0x15) IS A REAL FIELD -- 1 / 2 / 4. No PID inference, no Gen 5 flag byte.
 *  3. THE HANDLING TRAINER EXISTS (0x78 name, 0x92 gender, 0x93 CurrentHandler, 0xA2 friendship).
 *     This is where PKSE's OT/HT re-stamp logic starts to apply.
 *  4. RELEARN MOVES (0x6A-0x71).
 *
 * HYPER TRAINING EXISTS HERE and reuses byte 0xDE, which is the ground tile in Gen 6.
 *
 * PK7 IS PK6 WITH NINE BYTES REINTERPRETED (PKHeX PK7.cs vs PK6.cs):
 *   0x15  AbilityNumber is MASKED to the low 3 bits (PK6 uses the whole byte)
 *   0x16  MarkingValue is a u16 here; PK6 has TrainingBagHits/TrainingBag at 0x16/0x17
 *   0x2A  ResortEventStatus; PK6 has MarkingValue
 *   0x36  one more ribbon byte (RIB6); 0x37 unused
 *   0xDE  HyperTrainFlags; PK6 has GroundTile          <- the one that matters
 *   0xED  DirtType;     PK6 has FormArgumentRemain
 *   0xEE  DirtLocation; PK6 has FormArgumentElapsed
 *   0xEF  cleared on transfer; PK6 has TrainingBagEffect
 *   plus the dex/move/item/ability maxima and the personal table
 *
 * GameVersion::GG (Let's Go) is ALSO generation 7 but a different format entirely -- PB7, 260
 * bytes, its own class -- and the two must never be conflated.
 */
#ifndef POKEMON_POKEMON7_SM_H
#define POKEMON_POKEMON7_SM_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/PersonalInfo7SM.h"
#include "Encryption/Encryption7SM.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

namespace Pokemon
{

    class Pokemon7SM final : public Pokemon
    {
    public:
        /// Takes ENCRYPTED bytes, 232 or 260.
        explicit Pokemon7SM(std::span<const std::byte> raw)
        {
            if (Encryption::isSize7SM(raw.size()))
            {
                dataSize = raw.size();
                buffer = Encryption::decryptArray7SM(raw);
            }
            else
            {
                dataSize = Encryption::SIZE_PARTY7_SM;
                buffer = new std::byte[dataSize];
                std::memset(buffer, 0, dataSize);
            }
            data = std::span<std::byte>(buffer, dataSize);
        }

        Pokemon7SM()
        {
            dataSize = Encryption::SIZE_PARTY7_SM;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            data = std::span<std::byte>(buffer, dataSize);
        }

        ~Pokemon7SM() override = default;
        Pokemon7SM(const Pokemon7SM &) = delete;
        Pokemon7SM &operator=(const Pokemon7SM &) = delete;
        Pokemon7SM(Pokemon7SM &&) noexcept = default;
        Pokemon7SM &operator=(Pokemon7SM &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            std::byte *encryptedRecord = Encryption::encryptArray7SM(std::span<const std::byte>(data.data(), dataSize), encryptionConstant());
            auto copiedPokemon = std::make_unique<Pokemon7SM>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::SM; }

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

        bool isPartySize() const noexcept { return dataSize >= Encryption::SIZE_PARTY7_SM; }

        uint32_t encryptionConstant() const noexcept override { return rd32(0x00); }
        void setEncryptionConstant(uint32_t value) noexcept override { wr32(0x00, value); }
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
        uint16_t ability() const noexcept override { return rd8(0x14); }
        void setAbility(uint16_t value) noexcept override { wr8(0x14, static_cast<uint8_t>(value)); }
        /// A real stored field from Gen 6 on: 1, 2 or 4 (hidden).
        /// PK7 masks to the low 3 bits; the upper 5 belong to something else, so this is a
        /// read-modify-write where PK6's is a plain byte.
        uint8_t abilityNumber() const noexcept override { return rd8(0x15) & 7; }
        void setAbilityNumber(uint8_t abilityNumber) noexcept override
        {
            wr8(0x15, static_cast<uint8_t>((rd8(0x15) & ~7) | (abilityNumber & 7)));
        }
        /// Separate from the EncryptionConstant at 0x00 -- Gen 6 is where the two split.
        uint32_t pid() const noexcept override { return rd32(0x18); }
        void setPID(uint32_t value) noexcept override { wr32(0x18, value); }
        uint8_t nature() const noexcept override { return rd8(0x1C); }
        /// A plain write, except for a Pokemon from Gens 3-4, whose nature must still equal
        /// `EC % 25` (PKHeX GenderVerifier.VerifyNaturePID): its PID, which is also its encryption
        /// constant, is rebuilt to give the new nature.
        void setNature(uint8_t nature) noexcept override;
        uint8_t statNature() const noexcept override { return nature(); } // mints arrive in Gen 8
        void setStatNature(uint8_t) noexcept override {}

        bool isFatefulEncounter() const noexcept override { return (rd8(0x1D) & 1) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            wr8(0x1D, static_cast<uint8_t>((rd8(0x1D) & ~0x01) | (value ? 1 : 0)));
        }
        uint8_t gender() const noexcept override { return (rd8(0x1D) >> 1) & 0x03; }
        void setGender(uint8_t genderValue) noexcept override
        {
            wr8(0x1D, static_cast<uint8_t>((rd8(0x1D) & ~0x06) | ((genderValue & 3) << 1)));
        }
        uint8_t formID() const noexcept override { return rd8(0x1D) >> 3; }
        uint8_t form() const noexcept override { return formID(); }
        void setForm(uint8_t form) noexcept override
        {
            wr8(0x1D, static_cast<uint8_t>((rd8(0x1D) & 0x07) | (form << 3)));
        }

        uint8_t evHP() const noexcept override { return rd8(0x1E); }
        uint8_t evATK() const noexcept override { return rd8(0x1F); }
        uint8_t evDEF() const noexcept override { return rd8(0x20); }
        uint8_t evSPE() const noexcept override { return rd8(0x21); }
        uint8_t evSPA() const noexcept override { return rd8(0x22); }
        uint8_t evSPD() const noexcept override { return rd8(0x23); }
        void setEV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6)
                wr8(0x1E + statIndex, value);
        }

        bool isPokerusInfected() const noexcept override { return (rd8(0x2B) & 0xF) != 0; }
        bool isPokerusCured() const noexcept override { return (rd8(0x2B) & 0xF) == 0 && (rd8(0x2B) >> 4) != 0; }
        bool hasPokerus() const noexcept override { return true; }
        void setPokerus(uint8_t value) noexcept override { wr8(0x2B, value); }

        std::u16string nickname() const override { return readString(0x40, 13); }
        void setNickname(const std::u16string &value) noexcept override { writeString(0x40, 13, value); }
        int getMaxNicknameLength() const noexcept override { return 12; }

        uint16_t move(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd16(0x5A + slot * 2) : 0;
        }
        void setMove(int slot, uint16_t moveId) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr16(0x5A + slot * 2, moveId);
        }
        uint8_t movePP(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x62 + slot) : 0;
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x62 + slot, powerPoints);
        }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x66 + slot) : 0;
        }
        void setMovePPUps(int slot, uint8_t value) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x66 + slot, value);
        }
        uint16_t relearnMove(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd16(0x6A + slot * 2) : 0;
        }
        void setRelearnMove(int slot, uint16_t moveId) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr16(0x6A + slot * 2, moveId);
        }

        uint32_t iv32() const noexcept { return rd32(0x74); }
        /// Read-modify-write ONLY -- bits 30/31 are the egg and nicknamed flags.
        void setIV32(uint32_t value) noexcept { wr32(0x74, value); }
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
            static constexpr int IV_BIT_SHIFTS[6] = {0, 5, 10, 15, 20, 25};
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

        /// Gen 6 introduced the handler; this format carries one.
        bool hasHandler() const noexcept override { return true; }

        std::u16string htName() const override { return readString(0x78, 13); }
        void setHTName(const std::u16string &value) noexcept override { writeString(0x78, 13, value); }
        uint8_t htGender() const noexcept override { return rd8(0x92); }
        void setHTGender(uint8_t value) noexcept override { wr8(0x92, value); }
        uint8_t currentHandler() const noexcept override { return rd8(0x93); }
        void setCurrentHandler(uint8_t value) noexcept override { wr8(0x93, value); }
        uint8_t htFriendship() const noexcept override { return rd8(0xA2); }
        void setHTFriendship(uint8_t value) noexcept override { wr8(0xA2, value); }

        /** Handling Trainer affection (0-255). Location: 0xA3. */
        bool hasHandlerAffection() const noexcept override { return true; }
        uint8_t htAffection() const noexcept override { return rd8(0xA3); }
        void setHTAffection(uint8_t value) noexcept override { wr8(0xA3, value); }

        /** Handling Trainer memory: intensity 0xA4, memory 0xA5, feeling 0xA6, variable 0xA8 (u16). */
        bool hasHandlerMemories() const noexcept override { return true; }
        uint8_t htMemory() const noexcept override { return rd8(0xA5); }
        void setHTMemory(uint8_t value) noexcept override { wr8(0xA5, value); }
        uint16_t htMemoryVariable() const noexcept override { return rd16(0xA8); }
        void setHTMemoryVariable(uint16_t value) noexcept override { wr16(0xA8, value); }
        uint8_t htMemoryIntensity() const noexcept override { return rd8(0xA4); }
        void setHTMemoryIntensity(uint8_t value) noexcept override { wr8(0xA4, value); }
        uint8_t htMemoryFeeling() const noexcept override { return rd8(0xA6); }
        void setHTMemoryFeeling(uint8_t value) noexcept override { wr8(0xA6, value); }

        /**
         * Handler geolocation, five slots starting at 0x94. **REGION COMES FIRST IN EACH PAIR** --
         * region at 0x94 + 2n, country at 0x95 + 2n -- which is the opposite of how the pair reads
         * aloud. Swapping them yields two plausible small numbers rather than an error.
         */
        bool hasGeolocation() const noexcept override { return true; }
        uint8_t geolocationCountry(int slotIndex) const noexcept override
        {
            if (slotIndex < 0 || slotIndex >= GEOLOCATION_SLOT_COUNT) return 0;
            return rd8(0x95 + slotIndex * 2);
        }
        void setGeolocationCountry(int slotIndex, uint8_t value) noexcept override
        {
            if (slotIndex < 0 || slotIndex >= GEOLOCATION_SLOT_COUNT) return;
            wr8(0x95 + slotIndex * 2, value);
        }
        uint8_t geolocationRegion(int slotIndex) const noexcept override
        {
            if (slotIndex < 0 || slotIndex >= GEOLOCATION_SLOT_COUNT) return 0;
            return rd8(0x94 + slotIndex * 2);
        }
        void setGeolocationRegion(int slotIndex, uint8_t value) noexcept override
        {
            if (slotIndex < 0 || slotIndex >= GEOLOCATION_SLOT_COUNT) return;
            wr8(0x94 + slotIndex * 2, value);
        }

        std::u16string otName() const override { return readString(0xB0, 13); }
        void setOTName(const std::u16string &value) noexcept override { writeString(0xB0, 13, value); }
        /// The CURRENT handler's friendship: the OT's (0xCA) while the OT holds it, the handling
        /// trainer's (0xA2) after a trade (PKHeX G6PKM.CurrentFriendship).
        uint8_t friendship() const noexcept override { return currentHandler() == 0 ? rd8(0xCA) : rd8(0xA2); }
        void setFriendship(uint8_t value) noexcept override { wr8(currentHandler() == 0 ? 0xCA : 0xA2, value); }
        uint8_t otFriendship() const noexcept override { return rd8(0xCA); }
        void setOTFriendship(uint8_t value) noexcept override { wr8(0xCA, value); }

        uint8_t eggYear() const noexcept override { return rd8(0xD1); }
        uint8_t eggMonth() const noexcept override { return rd8(0xD2); }
        uint8_t eggDay() const noexcept override { return rd8(0xD3); }
        void setEggYear(uint8_t value) noexcept override { wr8(0xD1, value); }
        void setEggMonth(uint8_t value) noexcept override { wr8(0xD2, value); }
        void setEggDay(uint8_t value) noexcept override { wr8(0xD3, value); }
        uint8_t metYear() const noexcept override { return rd8(0xD4); }
        uint8_t metMonth() const noexcept override { return rd8(0xD5); }
        uint8_t metDay() const noexcept override { return rd8(0xD6); }
        void setMetYear(uint8_t value) noexcept override { wr8(0xD4, value); }
        void setMetMonth(uint8_t value) noexcept override { wr8(0xD5, value); }
        void setMetDay(uint8_t value) noexcept override { wr8(0xD6, value); }
        uint16_t eggLocation() const noexcept override { return rd16(0xD8); }
        void setEggLocation(uint16_t value) noexcept override { wr16(0xD8, value); }
        uint16_t metLocation() const noexcept override { return rd16(0xDA); }
        void setMetLocation(uint16_t value) noexcept override { wr16(0xDA, value); }
        uint8_t ball() const noexcept override { return rd8(0xDC); }
        void setBall(uint8_t value) noexcept override { wr8(0xDC, value); }
        uint8_t metLevel() const noexcept override { return rd8(0xDD) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            wr8(0xDD, static_cast<uint8_t>((rd8(0xDD) & 0x80) | (value & 0x7F)));
        }
        uint8_t otGender() const noexcept override { return rd8(0xDD) >> 7; }
        void setOTGender(uint8_t value) noexcept override
        {
            wr8(0xDD, static_cast<uint8_t>((rd8(0xDD) & 0x7F) | ((value & 1) << 7)));
        }
        uint8_t originGame() const noexcept override { return rd8(0xDF); }
        void setOriginGame(uint8_t value) noexcept override { wr8(0xDF, value); }
        uint8_t language() const noexcept override { return rd8(0xE3); }
        void setLanguage(uint8_t value) noexcept override { wr8(0xE3, value); }

        uint8_t level() const noexcept override { return isPartySize() ? rd8(0xEC) : 0; }
        void setLevel(uint8_t levelValue) noexcept override;
        uint16_t statHPCurrent() const noexcept override { return isPartySize() ? rd16(0xF0) : 0; }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            if (isPartySize())
                wr16(0xF0, value);
        }
        uint16_t statHPMax() const noexcept override { return isPartySize() ? rd16(0xF2) : 0; }
        uint16_t statATK() const noexcept override { return isPartySize() ? rd16(0xF4) : 0; }
        uint16_t statDEF() const noexcept override { return isPartySize() ? rd16(0xF6) : 0; }
        uint16_t statSPE() const noexcept override { return isPartySize() ? rd16(0xF8) : 0; }
        uint16_t statSPA() const noexcept override { return isPartySize() ? rd16(0xFA) : 0; }
        uint16_t statSPD() const noexcept override { return isPartySize() ? rd16(0xFC) : 0; }

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
        //
        // BYTE 0xDE IS THE GROUND TILE IN GEN 6 AND THE HYPER-TRAIN FLAGS HERE. recalculateStats()
        // below reads effectiveIV() rather than the raw IV, and effectiveIV() consults these three
        // -- so they must stay together. Change one without the others and every hyper-trained
        // Pokemon quietly computes a few points low on every edit that touches a stat. That is the
        // exact bug that shipped in Scarlet/Violet.
        //
        // Flag bit order is PKHeX's: HP, ATK, DEF, SPA, SPD, SPE -- which is NOT the accessor
        // order (HP, ATK, DEF, SPE, SPA, SPD). The HT_* constants encode this, so effectiveIV()
        // and recalculateStats() line up without an index mapping.
        bool hasHyperTraining() const noexcept override { return true; }
        uint8_t hyperTrainFlags() const noexcept override { return rd8(0xDE); }
        void setHyperTrainFlags(uint8_t value) noexcept override { wr8(0xDE, value); }
        bool canStoreNickname(const std::u16string &) const noexcept override { return true; }

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

    protected:
        /// The checksum covers the four blocks: 0x08 up to the stored size.
        static constexpr size_t CHECKSUM_START_OFFSET = 0x08;
        void refreshChecksumAfterWrite(size_t offset) noexcept
        {
            if (offset >= CHECKSUM_START_OFFSET && offset < Encryption::SIZE_STORED7_SM)
                refreshChecksum();
        }
        bool isFromGenerations3To5() const noexcept;
        /// A new PID for a Pokemon from Gens 3-5, whose PID still decides its gender, its ability
        /// slot and (Gens 3-4) its nature, and is also its encryption constant: those are held and
        /// the encryption constant follows the PID (PKHeX PKM.SetShiny / SetPIDNature). `wantNature`
        /// of -1 holds the current one. False, with nothing changed, when no PID fits.
        bool rerollTransferredPID(int wantShiny, int wantNature, uint32_t trainerID32) noexcept;
        const PersonalRecord &personal() const noexcept
        {
            return getPersonalInfo7SM(speciesID(), form());
        }
        int natureModifier(int statIndex) const noexcept;
        std::u16string readString(size_t offset, size_t units) const;
        void writeString(size_t offset, size_t units, const std::u16string &value) noexcept;
    };
}

#endif
