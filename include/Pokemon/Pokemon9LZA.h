/**
 * Pokemon9LZA.h - Generation 9 Pokemon Data Class
 *
 * This file defines the Pokemon9LZA class for Generation 9 Pokemon games:
 * - Pokemon Scarlet/Violet
 * - Pokemon Legends: Z-A
 *
 * Pokemon9LZA Format Details:
 * - Party Size: 344 bytes (0x158)
 * - Stored Size: 328 bytes (0x148)
 * - Encryption: XOR cipher + block shuffling
 * - Data Layout: 4 blocks (Growth, Attacks, EVs, Misc) + party stats
 *
 * Data Structure (Decrypted):
 * 0x00-0x07: Header (Encryption Constant, Checksum)
 * 0x08-0x57: Block A (Growth) - Species, items, EVs
 * 0x58-0xA7: Block B (Attacks) - Moves, IVs, nickname
 * 0xA8-0xF7: Block C (EVs/Contest) - Contest stats, ribbons
 * 0xF8-0x147: Block D (Misc) - OT info, encounter data
 * 0x148-0x157: Party Stats (Level, HP, Attack, Defense, etc.)
 */

#ifndef Pokemon_Pokemon9_LZA_H
#define Pokemon_Pokemon9_LZA_H

#include <cstdint>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/FormInfo.h"   // correctEncryptionConstantForForm
#include "Pokemon/SpeciesConverter9.h"
#include "Encryption/Encryption9LZA.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

using namespace Encryption;
using namespace Pokemon;
using namespace Utils;

namespace Pokemon {
    
    class Pokemon9LZA final : public Pokemon
    {
    public:
        /**
         * Constructs a Pokemon9LZA object from encrypted Pokemon data.
         *
         * Process:
         * 1. Decrypts the data using Gen9_LZA decryption algorithm
         * 2. Stores decrypted data in internal buffer
         * 3. Creates span view for easy access
         *
         * @param raw Encrypted Pokemon data (SIZE_PARTY9_LZA or SIZE_STORED9_LZA bytes)
         */
        explicit Pokemon9LZA(std::span<const std::byte> raw)
        {
            // Decrypt the Gen 9 Pokemon data
            buffer = decryptArray9LZA(raw);
            dataSize = raw.size();
            data = std::span<std::byte>(buffer, dataSize);
        }

        /**
         * Destructor - cleans up decrypted data buffer.
         * The base class PKM destructor handles buffer cleanup.
         */
        ~Pokemon9LZA() override = default;

        // Prevent copying (Pokemon data should not be accidentally copied)
        Pokemon9LZA(const Pokemon9LZA&) = delete;
        Pokemon9LZA& operator=(const Pokemon9LZA&) = delete;

        // Allow moving for efficient transfers
        Pokemon9LZA(Pokemon9LZA&&) noexcept = default;
        Pokemon9LZA& operator=(Pokemon9LZA&&) noexcept = default;

        /** Deep-copy: re-encrypt the decrypted buffer and rebuild via the encrypted-span ctor. */
        std::unique_ptr<Pokemon> clone() const override {
            uint32_t ec = readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data()));
            std::byte* enc = encryptArray9LZA(std::span<const std::byte>(data.data(), dataSize), ec);
            auto c = std::make_unique<Pokemon9LZA>(std::span<const std::byte>(enc, dataSize));
            delete[] enc;
            return c;
        }

        /** Storage-format game group (this subclass), NOT the origin Version byte. */
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::ZA; }

        // ========================================
        // Core Data Properties (Block A - Growth)
        // ========================================

        /**
         * Gets the Pokemon's Species ID.
         * Location: 0x08 (2 bytes)
         * @return Species ID (e.g., 1 = Bulbasaur, 25 = Pikachu)
         */
        uint16_t speciesID() const noexcept override
        {
            // Z-A stores the same Gen 9 INTERNAL species index as S/V (PKHeX PA9 uses the identical
            // SpeciesConverter.GetNational9) -- convert on read; see Pokemon9SV.h / SpeciesConverter9.h.
            return gen9InternalToNational(
                readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x08)));
        }

        /**
         * Gets the Pokemon's species name as a string.
         * Uses lookup table to convert Species ID to name.
         * @return Species name (e.g., "Pikachu", "Charizard")
         */
        const char* species() const noexcept override;

        /**
         * Gets the Form.
         * Location: 0x24 (1 byte)
         * @return Form ID (0 = no form)
         */
        uint8_t formID() const noexcept override
        {
            return static_cast<uint8_t>(data[0x24]);
        }

        /**
         * Gets the Pokemon's form/variation.
         * Location: 0x24 (1 byte)
         * @return Form ID (0 = base form)
         */
        uint8_t form() const noexcept override
        {
            return static_cast<uint8_t>(data[0x24]);
        }

        /**
         * Gets the held item ID.
         * Location: 0x0A (2 bytes)
         * @return Item ID (0 = no item)
         */
        uint16_t heldItem() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x0A));
        }

        /**
         * Gets the original trainer ID (32-bit format).
         * Location: 0x0C (4 bytes)
         * Gen 8 uses ID32 format: SID16 << 16 | TID16
         * @return Trainer ID32 value
         */
        uint32_t id32() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x0C));
        }

        /**
         * Gets the Pokemon's current experience points.
         * Location: 0x10 (4 bytes)
         * @return Experience value (determines level)
         */
        uint32_t exp() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x10));
        }

        /**
         * Gets the Pokemon's ability ID.
         * Location: 0x14 (2 bytes)
         * @return Ability ID
         */
        uint16_t ability() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x14));
        }

        /**
         * Gets the Pokemon's nature.
         * Location: 0x20 (1 byte)
         * Nature affects stat growth (e.g., Adamant boosts Attack, lowers Sp. Attack)
         * @return Nature ID (0-24)
         */
        uint8_t nature() const noexcept override
        {
            return static_cast<uint8_t>(data[0x20]);
        }

        /**
         * Gets the Pokemon's stat nature (affected by mints).
         * Location: 0x21 (1 byte)
         * Gen 8 introduced mints that can change effective nature without changing
         * the original nature. This value determines which nature affects stats.
         * @return Stat Nature ID (0-24)
         */
        uint8_t statNature() const noexcept override
        {
            return static_cast<uint8_t>(data[0x21]);
        }

        // ========================================
        // Encryption and Identification
        // ========================================

        /**
         * Gets the Encryption Constant.
         * Location: 0x00 (4 bytes)
         * Used as the seed for encrypting/decrypting Pokemon data.
         * @return Encryption Constant value
         */
        uint32_t encryptionConstant() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x00));
        }

        /**
         * Gets the Personality ID (PID).
         * Location: 0x1C (4 bytes)
         * Determines gender, shininess, and other properties.
         * @return PID value
         */
        uint32_t pid() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x1C));
        }

        // ========================================
        // Block B (Attacks/Nickname)
        // ========================================

        /**
         * Gets the Pokemon's nickname (custom name set by trainer).
         * Location: 0x58 (26 bytes, UTF-16LE)
         * Maximum 13 characters including null terminator.
         * @return Nickname as UTF-16 string
         */
        std::u16string nickname() const override
        {
            const uint8_t* nicknameStart = reinterpret_cast<const uint8_t*>(data.data() + 0x58);
            return getString(nicknameStart, 26);
        }

        // ========================================
        // Moves (Block B)
        // ========================================

        /**
         * Gets the move ID in a given slot.
         * Location: 0x72 + slot*2 (2 bytes each, slots 0-3). Shares the Gen 8/9 Block B layout.
         * @param slot Move slot (0-3)
         * @return Move ID (0 = empty slot)
         */
        uint16_t move(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3) return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x72 + slot * 2));
        }

        /**
         * Sets the move ID in a given slot and refreshes the checksum.
         * @param slot Move slot (0-3)
         * @param moveID Move ID to store
         */
        void setMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3) return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x72 + slot * 2), moveID);
            refreshChecksum();
        }

        /**
         * Gets the current PP of a move slot.
         * Location: 0x7A + slot (1 byte each, slots 0-3).
         */
        uint8_t movePP(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3) return 0;
            return static_cast<uint8_t>(data[0x7A + slot]);
        }

        /** Sets the current PP of a move slot and refreshes the checksum. */
        void setMovePP(int slot, uint8_t pp) noexcept override
        {
            if (slot < 0 || slot > 3) return;
            data[0x7A + slot] = static_cast<std::byte>(pp);
            refreshChecksum();
        }

        /**
         * Gets the number of PP Ups applied to a move slot.
         * Location: 0x7E + slot (1 byte each, slots 0-3).
         */
        uint8_t movePPUps(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3) return 0;
            return static_cast<uint8_t>(data[0x7E + slot]);
        }

        /** Sets the number of PP Ups applied to a move slot and refreshes the checksum. */
        void setMovePPUps(int slot, uint8_t ppUps) noexcept override
        {
            if (slot < 0 || slot > 3) return;
            data[0x7E + slot] = static_cast<std::byte>(ppUps);
            refreshChecksum();
        }

        /**
         * Gets a relearn move ID.
         * Location: 0x82 + slot*2 (2 bytes each, slots 0-3).
         */
        uint16_t relearnMove(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3) return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x82 + slot * 2));
        }

        /** Sets a relearn move ID and refreshes the checksum. */
        void setRelearnMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3) return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x82 + slot * 2), moveID);
            refreshChecksum();
        }

        // ========================================
        // Block D (Misc)
        // ========================================

        /**
         * Gets the Pokemon's friendship/happiness value.
         * Location: 0xCA (1 byte)
         * Affects evolution for some Pokemon and move power for Return/Frustration.
         * @return Friendship value (0-255)
         */
        uint8_t friendship() const noexcept override
        {
            return currentHandler() == 0 ? otFriendship() : htFriendship();
        }

        /**
         * Checks if the Pokemon is an egg.
         * Egg status is stored in bit 30 of the IV32 value.
         * @return true if egg, false otherwise
         */
        bool isEgg() const noexcept override
        {
            return (iv32() & 0x40000000) != 0;
        }

        /**
         * Gets the Pokerus status byte.
         * Location: 0xCB (1 byte)
         * Lower nibble: Days remaining (0 = cured)
         * Upper nibble: Strain
         * @return Pokerus status byte
         */
        uint8_t pokerus() const noexcept
        {
            return static_cast<uint8_t>(data[0x32]);
        }

        /**
         * Checks if the Pokemon is currently infected with Pokerus.
         * @return true if infected, false otherwise
         */
        bool isPokerusInfected() const noexcept
        {
            return (pokerus() & 0xF) > 0;
        }

        /**
         * Checks if the Pokemon has been cured of Pokerus.
         * Cured Pokemon retain the EV gain bonus but can't spread the virus.
         * @return true if cured, false otherwise
         */
        bool isPokerusCured() const noexcept
        {
            return (pokerus() & 0xF0) > 0 && (pokerus() & 0xF) == 0;
        }

        /** Writes the Pokerus byte (0x32). Z-A (PA9) carries it exactly like PK9/SV. */
        void setPokerus(uint8_t value) noexcept override { data[0x32] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Z-A has the Pokerus field (PA9 PokerusState @ 0x32) -- make the row editable, like SV. */
        bool hasPokerus() const noexcept override { return true; }

        // ========================================
        // OT / Origin / Met (Block D + Block A ids)
        // ========================================

        /** Game of origin (Version byte). Location: 0xCE (PA9 differs from PK8's 0xDE). */
        uint8_t originGame() const noexcept override { return static_cast<uint8_t>(data[0xCE]); }
        void setOriginGame(uint8_t version) noexcept override { data[0xCE] = static_cast<std::byte>(version); refreshChecksum(); }

        /** OT visible ID (TID16). Location: 0x0C. */
        uint16_t tid16() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x0C)); }
        void setTID16(uint16_t value) noexcept override { writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x0C), value); refreshChecksum(); }

        /** OT secret ID (SID16). Location: 0x0E. */
        uint16_t sid16() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x0E)); }
        void setSID16(uint16_t value) noexcept override { writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x0E), value); refreshChecksum(); }

        /** Sets the full 32-bit trainer ID. Location: 0x0C (4 bytes). */
        void setId32(uint32_t value) noexcept override { writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x0C), value); refreshChecksum(); }

        /** OT gender (0=Male, 1=Female). Location: 0x125 bit 7. */
        uint8_t otGender() const noexcept override { return (static_cast<uint8_t>(data[0x125]) >> 7) & 0x01; }
        void setOTGender(uint8_t value) noexcept override {
            uint8_t b = (static_cast<uint8_t>(data[0x125]) & 0x7F) | ((value & 0x01) << 7);
            data[0x125] = static_cast<std::byte>(b);
            refreshChecksum();
        }

        /** OT (base) friendship. Location: 0x112. */
        uint8_t otFriendship() const noexcept override { return static_cast<uint8_t>(data[0x112]); }
        void setOTFriendship(uint8_t value) noexcept override { data[0x112] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Language id. Location: 0xD5 (PA9 differs from PK8's 0xE2). */
        uint8_t language() const noexcept override { return static_cast<uint8_t>(data[0xD5]); }
        void setLanguage(uint8_t value) noexcept override { data[0xD5] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Poke Ball id. Location: 0x124. */
        uint8_t ball() const noexcept override { return static_cast<uint8_t>(data[0x124]); }
        void setBall(uint8_t value) noexcept override { data[0x124] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Met location. Location: 0x122. */
        uint16_t metLocation() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x122)); }
        void setMetLocation(uint16_t value) noexcept override { writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x122), value); refreshChecksum(); }

        /** Met level. Location: 0x125 low 7 bits. */
        uint8_t metLevel() const noexcept override { return static_cast<uint8_t>(data[0x125]) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override {
            uint8_t b = (static_cast<uint8_t>(data[0x125]) & 0x80) | (value & 0x7F);
            data[0x125] = static_cast<std::byte>(b);
            refreshChecksum();
        }

        /** Egg location. Location: 0x120. */
        uint16_t eggLocation() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x120)); }
        void setEggLocation(uint16_t value) noexcept override { writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x120), value); refreshChecksum(); }

        /** Met date (year = years since 2000). Location: 0x11C/0x11D/0x11E. */
        uint8_t metYear() const noexcept override { return static_cast<uint8_t>(data[0x11C]); }
        void setMetYear(uint8_t value) noexcept override { data[0x11C] = static_cast<std::byte>(value); refreshChecksum(); }
        uint8_t metMonth() const noexcept override { return static_cast<uint8_t>(data[0x11D]); }
        void setMetMonth(uint8_t value) noexcept override { data[0x11D] = static_cast<std::byte>(value); refreshChecksum(); }
        uint8_t metDay() const noexcept override { return static_cast<uint8_t>(data[0x11E]); }
        void setMetDay(uint8_t value) noexcept override { data[0x11E] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Egg date (year = years since 2000). Location: 0x119/0x11A/0x11B. */
        uint8_t eggYear() const noexcept override { return static_cast<uint8_t>(data[0x119]); }
        void setEggYear(uint8_t value) noexcept override { data[0x119] = static_cast<std::byte>(value); refreshChecksum(); }
        uint8_t eggMonth() const noexcept override { return static_cast<uint8_t>(data[0x11A]); }
        void setEggMonth(uint8_t value) noexcept override { data[0x11A] = static_cast<std::byte>(value); refreshChecksum(); }
        uint8_t eggDay() const noexcept override { return static_cast<uint8_t>(data[0x11B]); }
        void setEggDay(uint8_t value) noexcept override { data[0x11B] = static_cast<std::byte>(value); refreshChecksum(); }

        // ========================================
        // Names & handler (Block B/C/D)
        // ========================================

        /** Sets the nickname (UTF-16, max 12 chars). Location: 0x58 (26 bytes). */
        void setNickname(const std::u16string& value) noexcept override
        {
            setString(reinterpret_cast<uint8_t*>(data.data() + 0x58), 26, value, 12);
            refreshChecksum();
        }

        /** Original Trainer name. Location: 0xF8 (26 bytes). */
        std::u16string otName() const override
        {
            return getString(reinterpret_cast<const uint8_t*>(data.data() + 0xF8), 26);
        }
        void setOTName(const std::u16string& value) noexcept override
        {
            setString(reinterpret_cast<uint8_t*>(data.data() + 0xF8), 26, value, 12);
            refreshChecksum();
        }

        /** Handling (current) Trainer name. Location: 0xA8 (26 bytes). */
        std::u16string htName() const override
        {
            return getString(reinterpret_cast<const uint8_t*>(data.data() + 0xA8), 26);
        }
        void setHTName(const std::u16string& value) noexcept override
        {
            setString(reinterpret_cast<uint8_t*>(data.data() + 0xA8), 26, value, 12);
            refreshChecksum();
        }

        /** Handling Trainer gender (0=Male, 1=Female). Location: 0xC2. */
        uint8_t htGender() const noexcept override { return static_cast<uint8_t>(data[0xC2]); }
        void setHTGender(uint8_t value) noexcept override { data[0xC2] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Handling Trainer friendship. Location: 0xC8. */
        uint8_t htFriendship() const noexcept override { return static_cast<uint8_t>(data[0xC8]); }
        void setHTFriendship(uint8_t value) noexcept override { data[0xC8] = static_cast<std::byte>(value); refreshChecksum(); }

        /** Current handler flag (0 = OT active, 1 = HT active). Location: 0xC4. */
        uint8_t currentHandler() const noexcept override { return static_cast<uint8_t>(data[0xC4]); }
        void setCurrentHandler(uint8_t value) noexcept override { data[0xC4] = static_cast<std::byte>(value); refreshChecksum(); }

        // ========================================
        // Core editable setters (Block A)
        // ========================================

        /** Sets species (0x08) and recalculates stats (base stats change). */
        void setSpecies(uint16_t species) noexcept override
        {
            // Store the INTERNAL index (inverse of speciesID's read conversion; PKHeX PA9.GetInternal9).
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x08),
                                    gen9NationalToInternal(species));
            recalculateStats();
            refreshChecksum();
        }

        /** Sets form (0x24) and recalculates stats (regional variants change base stats). */
        void setForm(uint8_t formValue) noexcept override
        {
            data[0x24] = static_cast<std::byte>(formValue);
            // Maushold and Dudunsparce read their form from EncryptionConstant % 100, so the form byte
            // alone is only half the edit -- left mismatched the Pokemon is illegal, and the games
            // cannot produce that state. A no-op for every other species. See FormInfo.
            setEncryptionConstant(
                correctEncryptionConstantForForm(speciesID(), formValue, encryptionConstant()));
            recalculateStats();
            refreshChecksum();
        }

        /** Sets the held item id (0x0A). */
        void setHeldItem(uint16_t item) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x0A), item);
            refreshChecksum();
        }

        /** Sets the ability id (0x14). */
        void setAbility(uint16_t abilityValue) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x14), abilityValue);
            refreshChecksum();
        }

        /** Ability slot number (1/2/H). Location: 0x16 low 3 bits. */
        uint8_t abilityNumber() const noexcept override { return static_cast<uint8_t>(data[0x16]) & 0x07; }
        void setAbilityNumber(uint8_t number) noexcept override
        {
            uint8_t b = (static_cast<uint8_t>(data[0x16]) & ~0x07) | (number & 0x07);
            data[0x16] = static_cast<std::byte>(b);
            refreshChecksum();
        }

        /** Sets the original nature (0x20; cosmetic in Gen 8+). */
        void setNature(uint8_t natureValue) noexcept override
        {
            data[0x20] = static_cast<std::byte>(natureValue);
            refreshChecksum();
        }

        /** Sets the stat nature (0x21; the mint nature that affects stats in Gen 8+). */
        void setStatNature(uint8_t natureValue) noexcept override
        {
            data[0x21] = static_cast<std::byte>(natureValue);
            recalculateStats();
            refreshChecksum();
        }

        /** Sets the PID (0x1C). */
        void setPID(uint32_t pidValue) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x1C), pidValue);
            refreshChecksum();
        }

        /** Sets the Encryption Constant (0x00). */
        void setEncryptionConstant(uint32_t ec) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x00), ec);
            refreshChecksum();
        }

        /** Sets the current friendship (handler-aware: OT or HT). */
        void setFriendship(uint8_t value) noexcept override
        {
            if (currentHandler() == 0) setOTFriendship(value); else setHTFriendship(value);
            refreshChecksum();
        }

        /** Sets/clears the egg flag (bit 30 of the packed IV32 at 0x8C). */
        void setEgg(bool egg) noexcept override
        {
            uint32_t iv = iv32();
            if (egg) iv |= 0x40000000u; else iv &= ~0x40000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x8C), iv);
            refreshChecksum();
        }

        /** Reads/sets the "custom nickname" flag (bit 31 of the packed IV32 at 0x8C). */
        bool isNicknamed() const noexcept override { return (iv32() & 0x80000000u) != 0; }
        void setIsNicknamed(bool nicknamed) noexcept override
        {
            uint32_t iv = iv32();
            if (nicknamed) iv |= 0x80000000u; else iv &= ~0x80000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x8C), iv);
            refreshChecksum();
        }

        /** Sets gender (0=Male, 1=Female, 2=Genderless). Location: 0x22 bits 1-2 (PA9). */
        void setGender(uint8_t value) noexcept override
        {
            uint8_t b = (static_cast<uint8_t>(data[0x22]) & 0xF9) | ((value & 0x03) << 1);
            data[0x22] = static_cast<std::byte>(b);
            refreshChecksum();
        }

        /**
         * Alpha flag -- a whole byte at 0x23 (PA9), not a bit. Z-A keeps Legends: Arceus's Alphas, and
         * its Pokedex entry records "an Alpha of this species was seen" separately from the form flags.
         * Read-only here: nothing in PKSE creates an Alpha, but a save can already contain one.
         */
        bool isAlpha() const noexcept override { return static_cast<uint8_t>(data[0x23]) != 0; }

        /** Fateful-encounter flag -- bit 0 of the gender byte at 0x22 (setGender leaves bit 0 alone). */
        bool isFatefulEncounter() const noexcept override { return (static_cast<uint8_t>(data[0x22]) & 0x01) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            uint8_t b = (static_cast<uint8_t>(data[0x22]) & ~0x01) | (value ? 0x01 : 0x00);
            data[0x22] = static_cast<std::byte>(b);
            refreshChecksum();
        }

        /**
         * Sets the Pokemon's level (level() is the getter).
         * Clamps to [1,100], writes the level's minimum total EXP (0x10), updates the
         * cached party-stat level byte, then recalculates stats and refreshes the checksum.
         * Defined in the .cpp alongside recalculateStats().
         */
        void setLevel(uint8_t level) noexcept override;

        /** Sets total EXP (0x10) directly and re-derives the cached level. Defined in the .cpp. */
        void setExp(uint32_t value) noexcept override;

        // ========================================
        // Shiny and Gender
        // ========================================

        /**
         * Checks if the Pokemon is shiny (alternate coloration).
         *
         * Gen 8 shiny calculation:
         * XOR = (PID_High ^ PID_Low ^ TID16 ^ SID16)
         * Pokemon is shiny if XOR < 16
         * - XOR = 0: Square shiny
         * - XOR = 1-15: Star shiny
         *
         * @param trainerID32 The trainer's ID32 value
         * @param species Species name (for logging/debugging)
         * @return true if shiny, false otherwise
         */
        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override
        {
            if (trainerID32 == 0) {
                return false;
            }
            uint32_t xorComponent = (pid() ^ trainerID32);
            uint32_t xorResult = (xorComponent ^ (xorComponent >> 16)) & 0xFFFF;
            return xorResult < 16;
        }

        /**
         * Gets the Pokemon's gender.
         * @return 0 = Male, 1 = Female, 2 = Genderless
         */
        uint8_t gender() const noexcept override;

        /**
         * Gets a gender symbol string for display.
         * @return "♂" for male, "♀" for female, "" for genderless
         */
        const char* genderSymbol() const noexcept override
        {
            uint8_t genderValue = gender();
            if (genderValue == 0) return "♂"; // Male
            if (genderValue == 1) return "♀"; // Female
            return ""; // Genderless
        }

        // ========================================
        // Stats - Effort Values (EVs)
        // ========================================

        /**
         * Effort Values (EVs) earned through battling.
         * Location: 0x26-0x2B (1 byte each)
         * Max 252 per stat, 510 total across all stats.
         */
        uint8_t evHP() const noexcept override  { return static_cast<uint8_t>(data[0x26]); }
        uint8_t evATK() const noexcept override { return static_cast<uint8_t>(data[0x27]); }
        uint8_t evDEF() const noexcept override { return static_cast<uint8_t>(data[0x28]); }
        uint8_t evSPE() const noexcept override { return static_cast<uint8_t>(data[0x29]); }
        uint8_t evSPA() const noexcept override { return static_cast<uint8_t>(data[0x2A]); }
        uint8_t evSPD() const noexcept override { return static_cast<uint8_t>(data[0x2B]); }

        /**
         * Sets an Effort Value for a specific stat.
         * Automatically recalculates battle stats and refreshes checksum.
         * @param statIndex 0=HP, 1=ATK, 2=DEF, 3=SPE, 4=SPA, 5=SPD
         * @param value EV value (0-252)
         */
        void setEV(int statIndex, uint8_t value) noexcept override {
            if (statIndex >= 0 && statIndex < 6) {
                data[0x26 + statIndex] = static_cast<std::byte>(value);
                recalculateStats();
                refreshChecksum();
            }
        }

        // ========================================
        // Stats - Individual Values (IVs)
        // ========================================

        /**
         * Gets the packed IV32 value.
         * Location: 0x8C (4 bytes)
         * Contains all 6 IVs plus special flags (IsEgg, IsNicknamed).
         */
        uint32_t iv32() const noexcept { return readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x8C)); }

        /**
         * Individual Values (IVs) - inherent stat potential (0-31).
         * Extracted from IV32 using bit shifts:
         * - HP:  bits 0-4
         * - ATK: bits 5-9
         * - DEF: bits 10-14
         * - SPE: bits 15-19
         * - SPA: bits 20-24
         * - SPD: bits 25-29
         * - Bit 30: IsEgg flag
         * - Bit 31: IsNicknamed flag
         */
        uint8_t ivHP() const noexcept override  { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }

        /**
         * Sets an Individual Value for a specific stat.
         * IVs are packed into a single 32-bit value, so we must:
         * 1. Read the current IV32
         * 2. Clear the 5 bits for this stat
         * 3. Set the new value
         * 4. Write back IV32
         * 5. Recalculate stats and checksum
         *
         * @param statIndex 0=HP, 1=ATK, 2=DEF, 3=SPE, 4=SPA, 5=SPD
         * @param value IV value (0-31)
         */
        void setIV(int statIndex, uint8_t value) noexcept override {
            if (statIndex >= 0 && statIndex < 6 && value <= 31) {
                uint32_t iv = iv32();
                int shift = statIndex * 5;
                uint32_t mask = ~(0x1F << shift);  // Clear the 5 bits for this stat
                iv = (iv & mask) | ((value & 0x1F) << shift);  // Set new value
                writeUInt32LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x8C), iv);
                recalculateStats();
                refreshChecksum();
            }
        }

        // ========================================
        // Checksum Validation
        // ========================================

        /**
         * Gets the stored checksum value.
         * Location: 0x06 (2 bytes)
         * @return Checksum value
         */
        uint16_t checksum() const noexcept override {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x06));
        }

        /**
         * Calculates the checksum from offset 0x08 to SIZE_9STORED.
         * Checksum is the sum of all 16-bit values in the encrypted blocks.
         * Party stats (0x148+) are NOT included in checksum.
         * @return Calculated checksum value
         */
        uint16_t calculateChecksum() const noexcept override {
            uint16_t checksum = 0;

            // Sum all 16-bit values from offset 0x08 to SIZE_9STORED
            const size_t checksumEnd = std::min(dataSize, SIZE_STORED9_LZA);
            for (size_t i = 0x08; i < checksumEnd; i += 2) {
                checksum += readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + i));
            }

            return checksum;
        }

        /**
         * Recalculates and updates the stored checksum.
         * MUST be called after any modification to Pokemon data.
         */
        void refreshChecksum() noexcept override {
            uint16_t newChecksum = calculateChecksum();
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x06), newChecksum);
        }

        /**
         * Validates that stored checksum matches calculated checksum.
         * @return true if valid, false if data is corrupted
         */
        bool checksumValid() const noexcept override {
            return checksum() == calculateChecksum();
        }

        // ========================================
        // Base Stats (Species-Dependent)
        // ========================================

        /**
         * Base stats are determined by species and don't change per individual.
         * These are looked up from the PersonalInfo table.
         */
        uint8_t baseHP() const noexcept override;
        uint8_t baseATK() const noexcept override;
        uint8_t baseDEF() const noexcept override;
        uint8_t baseSPE() const noexcept override;
        uint8_t baseSPA() const noexcept override;
        uint8_t baseSPD() const noexcept override;

        // ========================================
        // Party Stats (Calculated Battle Stats)
        // ========================================

        /**
         * Party stats are the actual values used in battle.
         * Location: 0x148-0x157 (unencrypted section)
         * These are calculated from base stats, IVs, EVs, nature, and level.
         */
        uint8_t level() const noexcept override { return static_cast<uint8_t>(data[0x148]); }
        uint16_t statHPMax() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x14A)); }
        uint16_t statATK() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x14C)); }
        uint16_t statDEF() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x14E)); }
        uint16_t statSPE() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x150)); }
        uint16_t statSPA() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x152)); }
        uint16_t statSPD() const noexcept override { return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x154)); }
        uint16_t statHPCurrent() const noexcept override {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x8A));
        }
        void setStatHPCurrent(uint16_t value) noexcept override {
            const uint16_t max = statHPMax();
            if (max != 0 && value > max) value = max;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t*>(data.data() + 0x8A), value);
            refreshChecksum();
        }

        // ========================================
        // Stat Calculation
        // ========================================

        /**
         * Gets the nature modifier for a given stat.
         * Nature affects 5 stats (all except HP):
         * - Increased stat: 1.1x (110%)
         * - Decreased stat: 0.9x (90%)
         * - Neutral: 1.0x (100%)
         *
         * @param statIndex 0=ATK, 1=DEF, 2=SPE, 3=SPA, 4=SPD
         * @return Modifier percentage (90, 100, or 110)
         */
        int getNatureModifier(int statIndex) const noexcept;

        /**
         * Recalculates all battle stats based on current values.
         * Formula (HP): ((2 * Base + IV + EV/4) * Level / 100) + Level + 10
         * Formula (Other): (((2 * Base + IV + EV/4) * Level / 100) + 5) * NatureMod
         *
         * This should be called after modifying IVs, EVs, or nature.
         */
        void recalculateStats() noexcept override;

        // ========================================
        // Advanced Modification
        // ========================================

        /**
         * Regenerates PID while maintaining gender and shininess.
         * Used to fix legality issues when IVs are modified.
         * @param trainerID32 The trainer's ID32 for shiny calculation
         */
        void regeneratePID(uint32_t trainerID32) noexcept override;

        /**
         * Sets the shiny status of the Pokemon.
         * Modifies PID while preserving gender.
         * @param makeShiny true to make shiny, false to make non-shiny
         * @param trainerID32 The trainer's ID32 for shiny calculation
         */
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;
    };
}

#endif
