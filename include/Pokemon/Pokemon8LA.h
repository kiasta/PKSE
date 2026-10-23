#ifndef POKEMON_POKEMON8_LA_H
#define POKEMON_POKEMON8_LA_H

#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Encryption/Encryption8LA.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

using namespace Encryption;
using namespace Pokemon;
using namespace Utils;

namespace Pokemon
{
    using namespace Encryption;

    class Pokemon8LA final : public Pokemon
    {
    public:
        explicit Pokemon8LA(std::span<const std::byte> raw)
        {
            // LA box slots are stored-size (0x168); party slots are party-size (0x178). Always keep a
            // full party-size buffer so the party-stat getters (level/HP at 0x168+) stay in bounds for
            // a stored box pokemon — the trailing party region just reads as zero for box mons.
            std::byte *dec = decryptArray8LA(raw);
            const size_t copyN = std::min(raw.size(), static_cast<size_t>(SIZE_PARTY8_LA));
            buffer = new std::byte[SIZE_PARTY8_LA](); // zero-initialized
            for (size_t index = 0; index < copyN; ++index)
                buffer[index] = dec[index];
            delete[] dec;
            dataSize = SIZE_PARTY8_LA;
            data = std::span<std::byte>(buffer, dataSize);
            // A box slot is stored-size and carries no party-stat block, so the battle-stat cache at
            // 0x16A+ loaded as zero. Recompute it from species/IVs/EVs/level (level() derives from EXP,
            // so it's valid even for a box pokemon) -- otherwise statXXX(), which reads that cache, shows 0
            // for a boxed pokemon (a created pokemon's stats vanished after a game round-trip). Display-only:
            // the tail is beyond the stored size and the checksum, so it isn't written back to a box.
            if (raw.size() < static_cast<size_t>(SIZE_PARTY8_LA))
            {
                const uint16_t keepHP = statHPCurrent();
                recalculateStats();
                writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(buffer + 0x92), keepHP);
            }
        }

        ~Pokemon8LA() override = default;

        Pokemon8LA(const Pokemon8LA &) = delete;
        Pokemon8LA &operator=(const Pokemon8LA &) = delete;

        Pokemon8LA(Pokemon8LA &&) noexcept = default;
        Pokemon8LA &operator=(Pokemon8LA &&) noexcept = default;

        /** Deep-copy: re-encrypt the decrypted buffer and rebuild via the encrypted-span ctor.
         *  dataSize is always party-size (0x178) for PA8 — the ctor re-normalizes on rebuild. */
        std::unique_ptr<Pokemon> clone() const override
        {
            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data()));
            std::byte *encryptedRecord = encryptArray8LA(std::span<const std::byte>(data.data(), dataSize), encryptionConstant);
            auto copiedPokemon = std::make_unique<Pokemon8LA>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        /** Storage-format game group (this subclass), NOT the origin Version byte. */
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::PLA; }

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
            const uint8_t *nicknameStart = reinterpret_cast<const uint8_t *>(data.data() + 0x60);
            return getString(nicknameStart, 26);
        }

        uint16_t move(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x54 + slot * 2));
        }

        void setMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x54 + slot * 2), moveID);
            refreshChecksum();
        }

        /**
         * Gets the current PP of a move slot.
         * Location: 0x5C + slot (1 byte each, slots 0-3).
         */
        uint8_t movePP(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x5C + slot]);
        }

        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x5C + slot] = static_cast<std::byte>(powerPoints);
            refreshChecksum();
        }

        /**
         * Gets the number of PP Ups applied to a move slot.
         * Location: 0x86 + slot (1 byte each, slots 0-3).
         */
        uint8_t movePPUps(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x86 + slot]);
        }

        void setMovePPUps(int slot, uint8_t ppUps) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x86 + slot] = static_cast<std::byte>(ppUps);
            refreshChecksum();
        }

        /**
         * Gets a relearn move ID.
         * Location: 0x8A + slot*2 (2 bytes each, slots 0-3).
         */
        uint16_t relearnMove(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x8A + slot * 2));
        }

        void setRelearnMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8A + slot * 2), moveID);
            refreshChecksum();
        }

        uint8_t friendship() const noexcept override
        {
            // PA8 has no single friendship byte: it's the OT's while the OT still handles the pokemon
            // (CurrentHandler == 0 -> 0x12A), otherwise the handling trainer's (0xD8).
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

        /** Writes the Pokerus byte (0x32). Legends: Arceus (PA8) carries it like PK8/PK9. */
        void setPokerus(uint8_t value) noexcept override
        {
            data[0x32] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** PLA has the Pokerus field (PA8 PokerusState @ 0x32) -- make the row editable. */
        bool hasPokerus() const noexcept override { return true; }

        /** Hyper Training: one bit per stat -- HP, ATK, DEF, SPA, SPD, SPE -- marking the stat as
         *  played at a maximal IV while the stored IV is left untouched. PA8 keeps it at 0x13E -- NOT the 0x126 its Gen
         * 8 siblings use (PKHeX PA8.cs). The Legends: Arceus record is longer and its tail is laid out differently;
         * assuming the sibling offset here would read the wrong byte and report plausible wrong flags.
         *
         *  recalculateStats() below reads effectiveIV() rather than ivXXX() because of this; see
         *  the base class for what ignoring it cost. */
        bool hasHyperTraining() const noexcept override { return true; }
        uint8_t hyperTrainFlags() const noexcept override { return static_cast<uint8_t>(data[0x13E]); }
        void setHyperTrainFlags(uint8_t value) noexcept override
        {
            data[0x13E] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t originGame() const noexcept override { return static_cast<uint8_t>(data[0xEE]); }
        void setOriginGame(uint8_t version) noexcept override
        {
            data[0xEE] = static_cast<std::byte>(version);
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

        /** OT gender (0=Male, 1=Female). Location: 0x13D bit 7. */
        uint8_t otGender() const noexcept override { return (static_cast<uint8_t>(data[0x13D]) >> 7) & 0x01; }
        void setOTGender(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x13D]) & 0x7F) | ((value & 0x01) << 7);
            data[0x13D] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        /** OT (base) friendship. Location: 0x12A. */
        uint8_t otFriendship() const noexcept override { return static_cast<uint8_t>(data[0x12A]); }
        void setOTFriendship(uint8_t value) noexcept override
        {
            data[0x12A] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        uint8_t language() const noexcept override { return static_cast<uint8_t>(data[0xF2]); }
        void setLanguage(uint8_t value) noexcept override
        {
            data[0xF2] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Poke Ball id. Location: 0x137. */
        uint8_t ball() const noexcept override { return static_cast<uint8_t>(data[0x137]); }
        void setBall(uint8_t value) noexcept override
        {
            data[0x137] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Met location. Location: 0x13A. */
        uint16_t metLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x13A));
        }
        void setMetLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x13A), value);
            refreshChecksum();
        }

        /** Met level. Location: 0x13D low 7 bits. */
        uint8_t metLevel() const noexcept override { return static_cast<uint8_t>(data[0x13D]) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x13D]) & 0x80) | (value & 0x7F);
            data[0x13D] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        /** Egg location. Location: 0x138. */
        uint16_t eggLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x138));
        }
        void setEggLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x138), value);
            refreshChecksum();
        }

        /** Met date (year = years since 2000). Location: 0x134/0x135/0x136. */
        uint8_t metYear() const noexcept override { return static_cast<uint8_t>(data[0x134]); }
        void setMetYear(uint8_t value) noexcept override
        {
            data[0x134] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metMonth() const noexcept override { return static_cast<uint8_t>(data[0x135]); }
        void setMetMonth(uint8_t value) noexcept override
        {
            data[0x135] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metDay() const noexcept override { return static_cast<uint8_t>(data[0x136]); }
        void setMetDay(uint8_t value) noexcept override
        {
            data[0x136] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Egg date (year = years since 2000). Location: 0x131/0x132/0x133. */
        uint8_t eggYear() const noexcept override { return static_cast<uint8_t>(data[0x131]); }
        void setEggYear(uint8_t value) noexcept override
        {
            data[0x131] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggMonth() const noexcept override { return static_cast<uint8_t>(data[0x132]); }
        void setEggMonth(uint8_t value) noexcept override
        {
            data[0x132] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggDay() const noexcept override { return static_cast<uint8_t>(data[0x133]); }
        void setEggDay(uint8_t value) noexcept override
        {
            data[0x133] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Sets the nickname (UTF-16, max 12 chars). Location: 0x60 (26 bytes). */
        void setNickname(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0x60), 26, value, 12);
            refreshChecksum();
        }

        /** Original Trainer name. Location: 0x110 (26 bytes). */
        std::u16string otName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0x110), 26);
        }
        void setOTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0x110), 26, value, 12);
            refreshChecksum();
        }

        /// Gen 6 introduced the handler; this format carries one.
        bool hasHandler() const noexcept override { return true; }

        /// HOME arrived with Gen 8; this format carries a tracker.
        bool hasHomeTracker() const noexcept override { return true; }
        uint64_t homeTracker() const noexcept override
        {
            return readUInt64LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14D));
        }
        void setHomeTracker(uint64_t value) noexcept override
        {
            writeUInt64LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x14D), value);
            refreshChecksum();
        }

        /** Handling (current) Trainer name. Location: 0xB8 (26 bytes). */
        std::u16string htName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0xB8), 26);
        }
        void setHTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0xB8), 26, value, 12);
            refreshChecksum();
        }

        /** Handling Trainer gender (0=Male, 1=Female). Location: 0xD2. */
        uint8_t htGender() const noexcept override { return static_cast<uint8_t>(data[0xD2]); }
        void setHTGender(uint8_t value) noexcept override
        {
            data[0xD2] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Handling Trainer friendship. Location: 0xD8. */
        uint8_t htFriendship() const noexcept override { return static_cast<uint8_t>(data[0xD8]); }
        void setHTFriendship(uint8_t value) noexcept override
        {
            data[0xD8] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Current handler flag (0 = OT active, 1 = HT active). Location: 0xD4. */
        uint8_t currentHandler() const noexcept override { return static_cast<uint8_t>(data[0xD4]); }
        void setCurrentHandler(uint8_t value) noexcept override
        {
            data[0xD4] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Handling Trainer language. Location: 0xD3. */
        bool hasHandlerLanguage() const noexcept override { return true; }
        uint8_t htLanguage() const noexcept override { return static_cast<uint8_t>(data[0xD3]); }
        void setHTLanguage(uint8_t value) noexcept override
        {
            data[0xD3] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Handling Trainer memory: intensity 0xD9, memory 0xDA, feeling 0xDB, variable 0xDC (u16). */
        bool hasHandlerMemories() const noexcept override { return true; }
        uint8_t htMemory() const noexcept override { return static_cast<uint8_t>(data[0xDA]); }
        void setHTMemory(uint8_t value) noexcept override
        {
            data[0xDA] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint16_t htMemoryVariable() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xDC));
        }
        void setHTMemoryVariable(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xDC), value);
            refreshChecksum();
        }
        uint8_t htMemoryIntensity() const noexcept override { return static_cast<uint8_t>(data[0xD9]); }
        void setHTMemoryIntensity(uint8_t value) noexcept override
        {
            data[0xD9] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t htMemoryFeeling() const noexcept override { return static_cast<uint8_t>(data[0xDB]); }
        void setHTMemoryFeeling(uint8_t value) noexcept override
        {
            data[0xDB] = static_cast<std::byte>(value);
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

        /** Sets the current friendship — the OT's (0x12A) or HT's (0xD8) per CurrentHandler. */
        void setFriendship(uint8_t value) noexcept override
        {
            if (currentHandler() == 0)
                setOTFriendship(value);
            else
                setHTFriendship(value);
        }

        /** Sets/clears the egg flag (bit 30 of the packed IV32 at 0x94). */
        void setEgg(bool isEgg) noexcept override
        {
            uint32_t ivValue = iv32();
            if (isEgg)
                ivValue |= 0x40000000u;
            else
                ivValue &= ~0x40000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x94), ivValue);
            refreshChecksum();
        }

        /** Reads/sets the "custom nickname" flag (bit 31 of the packed IV32 at 0x94). */
        bool isNicknamed() const noexcept override { return (iv32() & 0x80000000u) != 0; }
        void setIsNicknamed(bool nicknamed) noexcept override
        {
            uint32_t ivValue = iv32();
            if (nicknamed)
                ivValue |= 0x80000000u;
            else
                ivValue &= ~0x80000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x94), ivValue);
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
            if (trainerID32 == 0)
            {
                return false;
            }
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

        /**
         * Alpha flag -- bit 5 of 0x16, the byte that also holds AbilityNumber in bits 0-2 (PA8).
         * The Pokedex records Alpha sightings in their own slot.
         */
        bool isAlpha() const noexcept override { return (static_cast<uint8_t>(data[0x16]) & 0x20) != 0; }
        bool hasAlphaFlag() const noexcept override { return true; }

        /**
         * A wild Alpha has height, weight and scale all at 255 (PKHeX LegendsArceusVerifier and
         * MiscScaleVerifier), and its absolutes follow the scalars. Defined in the .cpp beside
         * resetSizeAbsolutes().
         */
        void setAlpha(bool alpha) noexcept override;

        /**
         * Absolute height / weight -- IEEE-754 floats at 0xAC / 0xB0 (PA8), in the game's own units.
         * These are the values the Pokedex keeps its per-species size records from, so they are read
         * as stored rather than recomputed from the scalars.
         */
        float heightAbsolute() const noexcept
        {
            float floatValue;
            std::memcpy(&floatValue, &data[0xAC], sizeof floatValue);
            return floatValue;
        }
        float weightAbsolute() const noexcept
        {
            float floatValue;
            std::memcpy(&floatValue, &data[0xB0], sizeof floatValue);
            return floatValue;
        }

        /**
         * Sets the absolute height and weight from the size scalars (0x50 / 0x51) and the species'
         * base size, as the game computes them (PKHeX PA8.ResetHeight / ResetWeight). A record that
         * reaches this format any other way than being caught in it carries no absolutes of its own.
         */
        void resetSizeAbsolutes() noexcept;

        /**
         * GRIT VALUES (0-10 per stat), Legends: Arceus's replacement for EVs.
         *
         * PLA has no effort values: the EV bytes at 0x26-0x2B are inherited struct space the game
         * does not use, and what actually raises a stat is a grit value raised with Grit items.
         * recalculateStats() reads these, NOT evXXX() -- see the formula note in the .cpp. Stored
         * in the order HP, ATK, DEF, SPE, SPA, SPD (PKHeX PA8.GV_*), which is the personal-table
         * order and not the order the stat block is written in.
         */
        uint8_t gritHP() const noexcept { return static_cast<uint8_t>(data[0xA4]); }
        uint8_t gritATK() const noexcept { return static_cast<uint8_t>(data[0xA5]); }
        uint8_t gritDEF() const noexcept { return static_cast<uint8_t>(data[0xA6]); }
        uint8_t gritSPE() const noexcept { return static_cast<uint8_t>(data[0xA7]); }
        uint8_t gritSPA() const noexcept { return static_cast<uint8_t>(data[0xA8]); }
        uint8_t gritSPD() const noexcept { return static_cast<uint8_t>(data[0xA9]); }

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

        /**
         * Gets the packed IV32 value.
         * Location: 0x94 (4 bytes)
         * Contains all 6 IVs plus special flags (IsEgg, IsNicknamed).
         */
        uint32_t iv32() const noexcept
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x94));
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
                writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x94), ivValue);
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

            const size_t checksumEnd = std::min(dataSize, SIZE_STORED8_LA);
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

        /**
         * Party stats are the actual values used in battle.
         * Location: 0x168-0x177 (unencrypted section)
         * These are calculated from base stats, IVs, EVs, nature, and level.
         */
        /**
         * Gets the Pokemon's level.
         *
         * LA box slots are stored-size (0x168) and carry no party-stat block, so the cached
         * level byte at 0x168 reads as 0 for box mons. Derive the level from stored EXP for
         * EVERY pokemon (the source of truth, matching PKHeX PKM.CurrentLevel). getLevelFromExp
         * returns 1 for 0 EXP, so freshly-hatched level-1 box mons display correctly — no
         * fallback to the absent cached byte (which would have shown them as level 0).
         */
        uint8_t level() const noexcept override
        {
            return getLevelFromExp(exp(), getGrowthRate(speciesID()));
        }
        uint16_t statHPMax() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x16A));
        }
        uint16_t statATK() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x16C));
        }
        uint16_t statDEF() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x16E));
        }
        uint16_t statSPE() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x170));
        }
        uint16_t statSPA() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x172));
        }
        uint16_t statSPD() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x174));
        }
        // Current HP -- the one "stat" that is stored rather than derived, because it is the damage
        // the Pokemon is carrying. Unlike the block above it lives at 0x92, inside the checksummed
        // stored region, so a box pokemon carries it too and the setter has to refresh the checksum.
        // Reported RAW, 0 (fainted) included: nothing here may quietly heal a Pokemon.
        uint16_t statHPCurrent() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x92));
        }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            const uint16_t max = statHPMax();
            if (max != 0 && value > max)
                value = max;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x92), value);
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
