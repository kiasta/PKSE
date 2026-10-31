#ifndef POKEMON_POKEMON8_SWSH_H
#define POKEMON_POKEMON8_SWSH_H

#include <cstdint>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Encryption/Encryption8SWSH.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

using namespace Encryption;
using namespace Pokemon;
using namespace Utils;

namespace Pokemon
{
    using namespace Encryption;

    class Pokemon8SWSH final : public Pokemon
    {
    public:
        explicit Pokemon8SWSH(std::span<const std::byte> raw)
        {
            buffer = decryptArray8SWSH(raw);
            dataSize = raw.size();
            data = std::span<std::byte>(buffer, dataSize);
        }

        ~Pokemon8SWSH() override = default;

        Pokemon8SWSH(const Pokemon8SWSH &) = delete;
        Pokemon8SWSH &operator=(const Pokemon8SWSH &) = delete;

        Pokemon8SWSH(Pokemon8SWSH &&) noexcept = default;
        Pokemon8SWSH &operator=(Pokemon8SWSH &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data()));
            std::byte *encryptedRecord = encryptArray8SWSH(std::span<const std::byte>(data.data(), dataSize), encryptionConstant);
            auto copiedPokemon = std::make_unique<Pokemon8SWSH>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        /** Storage-format game group (this subclass), NOT the origin Version byte. */
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::SWSH; }

        uint16_t speciesID() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x08));
        }

        const char *species() const noexcept override;

        uint16_t heldItem() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0A));
        }

        uint32_t id32() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0C));
        }

        uint32_t exp() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x10));
        }

        uint16_t ability() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14));
        }

        uint8_t nature() const noexcept override
        {
            return static_cast<uint8_t>(data[0x20]);
        }

        /// The nature a mint made effective, which stat maths reads; nature() stays the original.
        uint8_t statNature() const noexcept override
        {
            return static_cast<uint8_t>(data[0x21]);
        }

        uint32_t encryptionConstant() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x00));
        }

        uint32_t pid() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x1C));
        }

        std::u16string nickname() const override
        {
            const uint8_t *nicknameStart = reinterpret_cast<const uint8_t *>(data.data() + 0x58);
            return getString(nicknameStart, 26);
        }

        uint16_t move(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x72 + slot * 2));
        }

        void setMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x72 + slot * 2), moveID);
            refreshChecksum();
        }

        uint8_t movePP(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x7A + slot]);
        }

        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x7A + slot] = static_cast<std::byte>(powerPoints);
            refreshChecksum();
        }

        uint8_t movePPUps(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x7E + slot]);
        }

        void setMovePPUps(int slot, uint8_t ppUps) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x7E + slot] = static_cast<std::byte>(ppUps);
            refreshChecksum();
        }

        uint16_t relearnMove(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x82 + slot * 2));
        }

        void setRelearnMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x82 + slot * 2), moveID);
            refreshChecksum();
        }

        uint8_t friendship() const noexcept override
        {
            return currentHandler() == 0 ? otFriendship() : htFriendship();
        }

        bool isEgg() const noexcept override
        {
            return (iv32() & 0x40000000) != 0;
        }

        /// Low nibble is days remaining (0 = cured), high nibble the strain.
        uint8_t pokerus() const noexcept
        {
            return static_cast<uint8_t>(data[0x32]);
        }

        bool isPokerusInfected() const noexcept
        {
            return (pokerus() & 0xF) > 0;
        }

        bool isPokerusCured() const noexcept
        {
            return (pokerus() & 0xF0) > 0 && (pokerus() & 0xF) == 0;
        }
        void setPokerus(uint8_t value) noexcept override
        {
            data[0x32] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        bool hasPokerus() const noexcept override { return true; }

        /** Hyper Training: one bit per stat -- HP, ATK, DEF, SPA, SPD, SPE -- marking the stat as
         *  played at a maximal IV while the stored IV is left untouched. PK8 keeps it at 0x126, inherited from PKHeX's
         * G8PKM rather than declared in PK8.cs.
         *
         *  recalculateStats() below reads effectiveIV() rather than ivXXX() because of this; see
         *  the base class for what ignoring it cost. */
        bool hasHyperTraining() const noexcept override { return true; }
        uint8_t hyperTrainFlags() const noexcept override { return static_cast<uint8_t>(data[0x126]); }
        void setHyperTrainFlags(uint8_t value) noexcept override
        {
            data[0x126] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t originGame() const noexcept override { return static_cast<uint8_t>(data[0xDE]); }
        void setOriginGame(uint8_t version) noexcept override
        {
            data[0xDE] = static_cast<std::byte>(version);
            refreshChecksum();
        }

        uint16_t tid16() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0C));
        }
        void setTID16(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0C), value);
            refreshChecksum();
        }

        uint16_t sid16() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0E));
        }
        void setSID16(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0E), value);
            refreshChecksum();
        }

        void setId32(uint32_t value) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0C), value);
            refreshChecksum();
        }

        uint8_t otGender() const noexcept override { return (static_cast<uint8_t>(data[0x125]) >> 7) & 0x01; }
        void setOTGender(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x125]) & 0x7F) | ((value & 0x01) << 7);
            data[0x125] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        uint8_t otFriendship() const noexcept override { return static_cast<uint8_t>(data[0x112]); }
        void setOTFriendship(uint8_t value) noexcept override
        {
            data[0x112] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t language() const noexcept override { return static_cast<uint8_t>(data[0xE2]); }
        void setLanguage(uint8_t value) noexcept override
        {
            data[0xE2] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t ball() const noexcept override { return static_cast<uint8_t>(data[0x124]); }
        void setBall(uint8_t value) noexcept override
        {
            data[0x124] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint16_t metLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x122));
        }
        void setMetLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x122), value);
            refreshChecksum();
        }

        uint8_t metLevel() const noexcept override { return static_cast<uint8_t>(data[0x125]) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x125]) & 0x80) | (value & 0x7F);
            data[0x125] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        uint16_t eggLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x120));
        }
        void setEggLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x120), value);
            refreshChecksum();
        }

        /** The year is years since 2000. */
        uint8_t metYear() const noexcept override { return static_cast<uint8_t>(data[0x11C]); }
        void setMetYear(uint8_t value) noexcept override
        {
            data[0x11C] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metMonth() const noexcept override { return static_cast<uint8_t>(data[0x11D]); }
        void setMetMonth(uint8_t value) noexcept override
        {
            data[0x11D] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metDay() const noexcept override { return static_cast<uint8_t>(data[0x11E]); }
        void setMetDay(uint8_t value) noexcept override
        {
            data[0x11E] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** The year is years since 2000. */
        uint8_t eggYear() const noexcept override { return static_cast<uint8_t>(data[0x119]); }
        void setEggYear(uint8_t value) noexcept override
        {
            data[0x119] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggMonth() const noexcept override { return static_cast<uint8_t>(data[0x11A]); }
        void setEggMonth(uint8_t value) noexcept override
        {
            data[0x11A] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggDay() const noexcept override { return static_cast<uint8_t>(data[0x11B]); }
        void setEggDay(uint8_t value) noexcept override
        {
            data[0x11B] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        void setNickname(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0x58), 26, value, 12);
            refreshChecksum();
        }

        std::u16string otName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0xF8), 26);
        }
        void setOTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0xF8), 26, value, 12);
            refreshChecksum();
        }

        /// Gen 6 introduced the handler; this format carries one.
        bool hasHandler() const noexcept override { return true; }

        /// HOME arrived with Gen 8; this format carries a tracker.
        bool hasHomeTracker() const noexcept override { return true; }
        uint64_t homeTracker() const noexcept override
        {
            return readUInt64LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x135));
        }
        void setHomeTracker(uint64_t value) noexcept override
        {
            writeUInt64LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x135), value);
            refreshChecksum();
        }

        std::u16string htName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0xA8), 26);
        }
        void setHTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0xA8), 26, value, 12);
            refreshChecksum();
        }

        uint8_t htGender() const noexcept override { return static_cast<uint8_t>(data[0xC2]); }
        void setHTGender(uint8_t value) noexcept override
        {
            data[0xC2] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t htFriendship() const noexcept override { return static_cast<uint8_t>(data[0xC8]); }
        void setHTFriendship(uint8_t value) noexcept override
        {
            data[0xC8] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** 0 = OT is the active handler, 1 = HT. */
        uint8_t currentHandler() const noexcept override { return static_cast<uint8_t>(data[0xC4]); }
        void setCurrentHandler(uint8_t value) noexcept override
        {
            data[0xC4] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Handling Trainer language. Location: 0xC3. */
        bool hasHandlerLanguage() const noexcept override { return true; }
        uint8_t htLanguage() const noexcept override { return static_cast<uint8_t>(data[0xC3]); }
        void setHTLanguage(uint8_t value) noexcept override
        {
            data[0xC3] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Handling Trainer memory: intensity 0xC9, memory 0xCA, feeling 0xCB, variable 0xCC (u16). */
        bool hasHandlerMemories() const noexcept override { return true; }
        uint8_t htMemory() const noexcept override { return static_cast<uint8_t>(data[0xCA]); }
        void setHTMemory(uint8_t value) noexcept override
        {
            data[0xCA] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint16_t htMemoryVariable() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xCC));
        }
        void setHTMemoryVariable(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xCC), value);
            refreshChecksum();
        }
        uint8_t htMemoryIntensity() const noexcept override { return static_cast<uint8_t>(data[0xC9]); }
        void setHTMemoryIntensity(uint8_t value) noexcept override
        {
            data[0xC9] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t htMemoryFeeling() const noexcept override { return static_cast<uint8_t>(data[0xCB]); }
        void setHTMemoryFeeling(uint8_t value) noexcept override
        {
            data[0xCB] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        void setSpecies(uint16_t species) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x08), species);
            recalculateStats();
            refreshChecksum();
        }

        void setForm(uint8_t formValue) noexcept override
        {
            data[0x24] = static_cast<std::byte>(formValue);
            recalculateStats();
            refreshChecksum();
        }

        void setHeldItem(uint16_t item) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0A), item);
            refreshChecksum();
        }

        void setAbility(uint16_t abilityValue) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x14), abilityValue);
            refreshChecksum();
        }

        uint8_t abilityNumber() const noexcept override { return static_cast<uint8_t>(data[0x16]) & 0x07; }
        void setAbilityNumber(uint8_t number) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x16]) & ~0x07) | (number & 0x07);
            data[0x16] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        void setNature(uint8_t natureValue) noexcept override
        {
            data[0x20] = static_cast<std::byte>(natureValue);
            refreshChecksum();
        }

        void setStatNature(uint8_t natureValue) noexcept override
        {
            data[0x21] = static_cast<std::byte>(natureValue);
            recalculateStats();
            refreshChecksum();
        }

        void setPID(uint32_t pidValue) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), pidValue);
            refreshChecksum();
        }

        void setEncryptionConstant(uint32_t encryptionConstant) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x00), encryptionConstant);
            refreshChecksum();
        }

        void setFriendship(uint8_t value) noexcept override
        {
            if (currentHandler() == 0)
                setOTFriendship(value);
            else
                setHTFriendship(value);
            refreshChecksum();
        }

        void setEgg(bool isEgg) noexcept override
        {
            uint32_t ivValue = iv32();
            if (isEgg)
                ivValue |= 0x40000000u;
            else
                ivValue &= ~0x40000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8C), ivValue);
            refreshChecksum();
        }

        bool isNicknamed() const noexcept override { return (iv32() & 0x80000000u) != 0; }
        void setIsNicknamed(bool nicknamed) noexcept override
        {
            uint32_t ivValue = iv32();
            if (nicknamed)
                ivValue |= 0x80000000u;
            else
                ivValue &= ~0x80000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8C), ivValue);
            refreshChecksum();
        }

        void setGender(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x22]) & 0xF3) | ((value & 0x03) << 2);
            data[0x22] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        /** Fateful-encounter flag -- bit 0 of the gender byte at 0x22 (setGender leaves bit 0 alone). */
        bool isFatefulEncounter() const noexcept override { return (static_cast<uint8_t>(data[0x22]) & 0x01) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x22]) & ~0x01) | (value ? 0x01 : 0x00);
            data[0x22] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        /**
         * Sets the Pokemon's level (level() is the getter).
         * Clamps to [1,100], writes the level's minimum total EXP (0x10), updates the
         * cached party-stat level byte, then recalculates stats and refreshes the checksum.
         * Defined in the .cpp alongside recalculateStats().
         */
        void setLevel(uint8_t level) noexcept override;

        void setExp(uint32_t value) noexcept override;

        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override
        {
            // No guard for an id of 0: PKHeX has none, and that is a real (if rare) trainer id.
            // Callers skip empty slots, whose all-zero PID would otherwise read as shiny.
            uint32_t xorComponent = (pid() ^ trainerID32);
            uint32_t xorResult = (xorComponent ^ (xorComponent >> 16)) & 0xFFFF;
            return xorResult < 16;
        }

        uint8_t gender() const noexcept override;

        const char *genderSymbol() const noexcept override
        {
            uint8_t genderValue = gender();
            if (genderValue == 0) return "♂";
            if (genderValue == 1) return "♀";
            return "";
        }

        uint8_t formID() const noexcept override
        {
            return static_cast<uint8_t>(data[0x24]);
        }

        uint8_t form() const noexcept override
        {
            return static_cast<uint8_t>(data[0x24]);
        }

        uint8_t evHP() const noexcept override { return static_cast<uint8_t>(data[0x26]); }
        uint8_t evATK() const noexcept override { return static_cast<uint8_t>(data[0x27]); }
        uint8_t evDEF() const noexcept override { return static_cast<uint8_t>(data[0x28]); }
        uint8_t evSPE() const noexcept override { return static_cast<uint8_t>(data[0x29]); }
        uint8_t evSPA() const noexcept override { return static_cast<uint8_t>(data[0x2A]); }
        uint8_t evSPD() const noexcept override { return static_cast<uint8_t>(data[0x2B]); }

        void setEV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6)
            {
                data[0x26 + statIndex] = static_cast<std::byte>(value);
                recalculateStats();
                refreshChecksum();
            }
        }

        uint32_t iv32() const noexcept
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x8C));
        }

        uint8_t ivHP() const noexcept override { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }

        void setIV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6 && value <= 31)
            {
                uint32_t ivValue = iv32();
                int shift = statIndex * 5;
                uint32_t mask = ~(0x1F << shift);
                ivValue = (ivValue & mask) | ((value & 0x1F) << shift);
                writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8C), ivValue);
                recalculateStats();
                refreshChecksum();
            }
        }

        uint16_t checksum() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x06));
        }

        /// Sums 16-bit words from 0x08 to the STORED size; the party tail is not covered.
        uint16_t calculateChecksum() const noexcept override
        {
            uint16_t checksum = 0;

            const size_t checksumEnd = std::min(dataSize, SIZE_STORED8_SWSH);
            for (size_t index = 0x08; index < checksumEnd; index += 2)
            {
                checksum += readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + index));
            }

            return checksum;
        }

        void refreshChecksum() noexcept override
        {
            uint16_t newChecksum = calculateChecksum();
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x06), newChecksum);
        }

        bool checksumValid() const noexcept override
        {
            return checksum() == calculateChecksum();
        }

        uint8_t baseHP() const noexcept override;
        uint8_t baseATK() const noexcept override;
        uint8_t baseDEF() const noexcept override;
        uint8_t baseSPE() const noexcept override;
        uint8_t baseSPA() const noexcept override;
        uint8_t baseSPD() const noexcept override;

        uint8_t level() const noexcept override { return static_cast<uint8_t>(data[0x148]); }
        uint16_t statHPMax() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14A));
        }
        uint16_t statATK() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14C));
        }
        uint16_t statDEF() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14E));
        }
        uint16_t statSPE() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x150));
        }
        uint16_t statSPA() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x152));
        }
        uint16_t statSPD() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x154));
        }
        uint16_t statHPCurrent() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x8A));
        }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            const uint16_t max = statHPMax();
            if (max != 0 && value > max)
                value = max;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8A), value);
            refreshChecksum();
        }

        /// 110, 100 or 90 -- the percentage the nature applies. HP is never modified, so this index
        /// is 0=ATK, 1=DEF, 2=SPE, 3=SPA, 4=SPD, one short of the EV/IV order.
        int getNatureModifier(int statIndex) const noexcept;

        void recalculateStats() noexcept override;

        void regeneratePID(uint32_t trainerID32) noexcept override;

        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;
    };
}

#endif
