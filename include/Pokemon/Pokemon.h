/**
 * Pokemon.h - Base Pokemon Data Class
 *
 * This file defines the abstract base class for all Pokemon data structures.
 * This base class provides a unified interface for accessing Pokemon data across all generations.
 *
 * Derived classes (PK7, PK8, PK9, etc.) implement generation-specific data
 * formats and storage layouts.
 */

#ifndef POKEMON_POKEMON_H
#define POKEMON_POKEMON_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <string>
#include <memory>

#include "Enums/GameVersion.h"

namespace Pokemon {
    /**
     * PKM - Abstract base class for Pokemon entity data
     *
     * This class provides the foundation for all generation-specific Pokemon classes.
     * It defines the common interface that all Pokemon formats must implement,
     * while allowing each generation to handle its own data layout and encryption.
     *
     * Memory Layout:
     * - Each derived class manages its own byte buffer containing Pokemon data
     * - Data may be encrypted or decrypted depending on the context
     * - Size varies by generation (e.g., PK8 = 344 bytes, PK7 = 260 bytes)
     *
     * Usage Pattern:
     * 1. Derived class receives encrypted data
     * 2. Constructor decrypts and stores data in internal buffer
     * 3. Getters/setters access decrypted data directly
     * 4. When saving, data is re-encrypted using generation-specific methods
     */
    class Pokemon {
    protected:
        /**
         * Internal buffer storing the Pokemon's decrypted data.
         * This buffer is managed by the derived class and should contain
         * all Pokemon information in its decrypted form for easy access.
         */
        std::byte* buffer = nullptr;

        /**
         * Span view of the decrypted Pokemon data.
         * Provides safe, bounds-checked access to the data buffer.
         */
        std::span<std::byte> data;

        /**
         * Actual size of the Pokemon data in bytes.
         * This varies by generation:
         * - Gen 8 (PK8): 344 bytes (party) or 328 bytes (stored)
         * - Gen 7 LGP/E (PK7): 260 bytes
         * - Gen 9 (PK9): varies by format
         */
        size_t dataSize;

    public:
        // Virtual destructor to ensure proper cleanup in derived classes
        virtual ~Pokemon() {
            if (buffer) {
                delete[] buffer;
                buffer = nullptr;
            }
        }

        // Delete copy operations to prevent accidental copies of Pokemon data
        Pokemon(const Pokemon&) = delete;
        Pokemon& operator=(const Pokemon&) = delete;

        // Allow move operations for efficient transfers
        Pokemon(Pokemon&&) noexcept = default;
        Pokemon& operator=(Pokemon&&) noexcept = default;

        // ========================================
        // Core Data Properties (Pure Virtual)
        // ========================================

        /**
         * Gets the Pokemon's Species ID.
         * @return Species ID (e.g., 1 = Bulbasaur, 25 = Pikachu, 133 = Eevee)
         */
        virtual uint16_t speciesID() const noexcept = 0;

        /**
         * Gets the Pokemon's species name as a string.
         * @return Species name (e.g., "Pikachu", "Eevee")
         */
        virtual const char* species() const noexcept = 0;

        /**
         * Gets the Pokemon's nickname (custom name set by trainer).
         * @return Nickname as UTF-16 string
         */
        virtual std::u16string nickname() const = 0;

        /**
         * Gets the form ID.
         * @return Form ID (0 = no form)
         */
        virtual uint8_t formID() const noexcept = 0;

        /**
         * Gets the Pokemon's form/variation.
         * @return Form ID (0 = base form)
         */
        virtual uint8_t form() const noexcept = 0;

        /**
         * Gets the held item ID.
         * @return Item ID (0 = no item)
         */
        virtual uint16_t heldItem() const noexcept = 0;

        /**
         * Gets the Pokemon's original trainer ID (32-bit format).
         * @return Trainer ID32 value
         */
        virtual uint32_t id32() const noexcept = 0;

        /**
         * Gets the Pokemon's current experience points.
         * @return Experience value
         */
        virtual uint32_t exp() const noexcept = 0;

        /**
         * Gets the Pokemon's ability ID.
         * @return Ability ID
         */
        virtual uint16_t ability() const noexcept = 0;

        /**
         * Gets the Pokemon's nature (affects stat growth).
         * @return Nature ID (0-24)
         */
        virtual uint8_t nature() const noexcept = 0;

        /**
         * Gets the Pokemon's stat nature.
         * @return Nature ID (0-24)
         */
        virtual uint8_t statNature() const noexcept = 0;

        /**
         * Gets the Pokemon's current level.
         * @return Level (1-100)
         */
        virtual uint8_t level() const noexcept = 0;

        /**
         * Gets the Pokemon's gender.
         * @return 0 = Male, 1 = Female, 2 = Genderless
         */
        virtual uint8_t gender() const noexcept = 0;

        /**
         * Gets a gender symbol string for display.
         * @return "♂" for male, "♀" for female, "" for genderless
         */
        virtual const char* genderSymbol() const noexcept = 0;

        /**
         * Gets the Personality ID (PID).
         * Used for determining gender, shininess, and other properties.
         * @return PID value
         */
        virtual uint32_t pid() const noexcept = 0;

        /**
         * Gets the Encryption Constant.
         * Used as the seed for encrypting/decrypting Pokemon data.
         * @return Encryption Constant value
         */
        virtual uint32_t encryptionConstant() const noexcept = 0;

        // ========================================
        // Stats - Individual Values (IVs)
        // ========================================

        /**
         * Individual Values (IVs) are inherent stat values (0-31) that determine
         * a Pokemon's potential. Higher IVs result in higher final stats.
         */
        virtual uint8_t ivHP() const noexcept = 0;
        virtual uint8_t ivATK() const noexcept = 0;
        virtual uint8_t ivDEF() const noexcept = 0;
        virtual uint8_t ivSPE() const noexcept = 0;
        virtual uint8_t ivSPA() const noexcept = 0;
        virtual uint8_t ivSPD() const noexcept = 0;

        /**
         * Sets an Individual Value for a specific stat.
         * @param statIndex 0=HP, 1=ATK, 2=DEF, 3=SPE, 4=SPA, 5=SPD
         * @param value IV value (0-31)
         */
        virtual void setIV(int statIndex, uint8_t value) noexcept = 0;

        // ========================================
        // Stats - Effort Values (EVs)
        // ========================================

        /**
         * Effort Values (EVs) are earned through battling and training.
         * They provide additional stat points (max 252 per stat, 510 total).
         */
        virtual uint8_t evHP() const noexcept = 0;
        virtual uint8_t evATK() const noexcept = 0;
        virtual uint8_t evDEF() const noexcept = 0;
        virtual uint8_t evSPE() const noexcept = 0;
        virtual uint8_t evSPA() const noexcept = 0;
        virtual uint8_t evSPD() const noexcept = 0;

        /**
         * Sets an Effort Value for a specific stat.
         * @param statIndex 0=HP, 1=ATK, 2=DEF, 3=SPE, 4=SPA, 5=SPD
         * @param value EV value (0-252)
         */
        virtual void setEV(int statIndex, uint8_t value) noexcept = 0;

        // ========================================
        // Stats - Awakening Values (AVs) - Let's Go Only
        // ========================================

        /**
         * Awakening Values (AVs) are unique to Pokemon Let's Go Pikachu/Eevee.
         * They provide additional stat points similar to EVs but earned differently.
         * Max 200 per stat, earned by using candies or catching Pokemon.
         * Default implementation returns 0 for games without AVs.
         */
        virtual uint8_t avHP() const noexcept { return 0; }
        virtual uint8_t avATK() const noexcept { return 0; }
        virtual uint8_t avDEF() const noexcept { return 0; }
        virtual uint8_t avSPE() const noexcept { return 0; }
        virtual uint8_t avSPA() const noexcept { return 0; }
        virtual uint8_t avSPD() const noexcept { return 0; }

        /**
         * Sets an Awakening Value for a specific stat (Let's Go only).
         * @param statIndex 0=HP, 1=ATK, 2=DEF, 3=SPE, 4=SPA, 5=SPD
         * @param value AV value (0-200)
         * Default implementation is a no-op for games without AVs.
         */
        virtual void setAV(int statIndex, uint8_t value) noexcept { (void)statIndex; (void)value; }

        /**
         * Checks if this Pokemon format uses Awakening Values (AVs).
         * @return true for Let's Go Pokemon, false otherwise
         */
        virtual bool hasAwakeningValues() const noexcept { return false; }
        /**
         * Is this an Alpha? Legends: Arceus and Legends Z-A only; every other format
         * has no such concept and is false.
         * 
         * Read-only across the codebase — nothing in PKSE creates an Alpha — but a save
         * can already contain one, and the legality checker has to know: an Alpha is a
         * property of the ENCOUNTER (alpha slots and alpha statistics are separate templates
         * with their own guaranteed IVs), not something the player cna confer afterwords
         */
        virtual bool isAlpha() const noexcept { return false; }

        // ========================================
        // Moves
        // ========================================

        /**
         * Gets the move ID in a given slot.
         * @param slot Move slot (0-3)
         * @return Move ID (0 = empty slot). Default 0 for formats not yet wired.
         */
        virtual uint16_t move(int slot) const noexcept { (void)slot; return 0; }

        /**
         * Sets the move ID in a given slot.
         * Moves are part of the checksummed data, so implementations must call
         * refreshChecksum(). Does NOT auto-set PP (that needs a base-PP table) —
         * set PP explicitly via setMovePP() when required.
         * @param slot Move slot (0-3)
         * @param moveID Move ID to store
         */
        virtual void setMove(int slot, uint16_t moveID) noexcept { (void)slot; (void)moveID; }

        /**
         * Gets the current PP of a move slot.
         * @param slot Move slot (0-3)
         */
        virtual uint8_t movePP(int slot) const noexcept { (void)slot; return 0; }

        /**
         * Sets the current PP of a move slot.
         * @param slot Move slot (0-3)
         * @param pp PP value
         */
        virtual void setMovePP(int slot, uint8_t pp) noexcept { (void)slot; (void)pp; }

        /**
         * Gets the number of PP Ups applied to a move slot (0-3).
         * @param slot Move slot (0-3)
         */
        virtual uint8_t movePPUps(int slot) const noexcept { (void)slot; return 0; }

        /**
         * Sets the number of PP Ups applied to a move slot.
         * @param slot Move slot (0-3)
         * @param ppUps PP Up count (0-3)
         */
        virtual void setMovePPUps(int slot, uint8_t ppUps) noexcept { (void)slot; (void)ppUps; }

        /**
         * Gets a relearn move ID (Gen 6+ "remembered" moves; 0 for older formats).
         * @param slot Relearn slot (0-3)
         */
        virtual uint16_t relearnMove(int slot) const noexcept { (void)slot; return 0; }

        /**
         * Sets a relearn move ID (Gen 6+).
         * @param slot Relearn slot (0-3)
         * @param moveID Move ID to store
         */
        virtual void setRelearnMove(int slot, uint16_t moveID) noexcept { (void)slot; (void)moveID; }

        // ========================================
        // OT / Origin / Met data
        // ========================================
        // Getters default to 0 and setters are no-ops so a format that hasn't been
        // wired yet still compiles. Wired formats must call refreshChecksum() in setters.

        /** Game of origin (the Version byte). @return GameVersion numeric id (0 if unwired). */
        virtual uint8_t originGame() const noexcept { return 0; }
        virtual void setOriginGame(uint8_t version) noexcept { (void)version; }

        /** Storage-format game group (which subclass this is), NOT the origin Version byte. */
        virtual Enums::GameVersion getGameGroup() const noexcept = 0;

        /** Original Trainer visible ID (TID16). */
        virtual uint16_t tid16() const noexcept { return 0; }
        virtual void setTID16(uint16_t value) noexcept { (void)value; }

        /** Original Trainer secret ID (SID16). */
        virtual uint16_t sid16() const noexcept { return 0; }
        virtual void setSID16(uint16_t value) noexcept { (void)value; }

        /** Sets the full 32-bit trainer ID (id32() is the getter). */
        virtual void setId32(uint32_t value) noexcept { (void)value; }

        /** Original Trainer gender (0 = Male, 1 = Female). */
        virtual uint8_t otGender() const noexcept { return 0; }
        virtual void setOTGender(uint8_t value) noexcept { (void)value; }

        /** Original Trainer (base) friendship (0-255). */
        virtual uint8_t otFriendship() const noexcept { return 0; }
        virtual void setOTFriendship(uint8_t value) noexcept { (void)value; }

        /** Language ID the Pokemon was raised in. */
        virtual uint8_t language() const noexcept { return 0; }
        virtual void setLanguage(uint8_t value) noexcept { (void)value; }

        /** Poke Ball the Pokemon is contained in. */
        virtual uint8_t ball() const noexcept { return 0; }
        virtual void setBall(uint8_t value) noexcept { (void)value; }

        /** Met location id (where the Pokemon was caught/received). */
        virtual uint16_t metLocation() const noexcept { return 0; }
        virtual void setMetLocation(uint16_t value) noexcept { (void)value; }

        /** Met level (level at which the Pokemon was met). */
        virtual uint8_t metLevel() const noexcept { return 0; }
        virtual void setMetLevel(uint8_t value) noexcept { (void)value; }

        /** Egg location id (0 = not hatched from an egg / not applicable). */
        virtual uint16_t eggLocation() const noexcept { return 0; }
        virtual void setEggLocation(uint16_t value) noexcept { (void)value; }

        /** Met date components (year stored as years-since-2000). */
        virtual uint8_t metYear() const noexcept { return 0; }
        virtual void setMetYear(uint8_t value) noexcept { (void)value; }
        virtual uint8_t metMonth() const noexcept { return 0; }
        virtual void setMetMonth(uint8_t value) noexcept { (void)value; }
        virtual uint8_t metDay() const noexcept { return 0; }
        virtual void setMetDay(uint8_t value) noexcept { (void)value; }

        /** Egg date components (year stored as years-since-2000). */
        virtual uint8_t eggYear() const noexcept { return 0; }
        virtual void setEggYear(uint8_t value) noexcept { (void)value; }
        virtual uint8_t eggMonth() const noexcept { return 0; }
        virtual void setEggMonth(uint8_t value) noexcept { (void)value; }
        virtual uint8_t eggDay() const noexcept { return 0; }
        virtual void setEggDay(uint8_t value) noexcept { (void)value; }

        // ========================================
        // Names & handler
        // ========================================

        /** Sets the nickname (UTF-16, max getMaxNicknameLength() chars). Does not change isNicknamed. */
        virtual void setNickname(const std::u16string& value) noexcept { (void)value; }

        /** Nickname capacity in CHARACTERS -- 12 in every format but Gen 3, whose field is 10. */
        virtual int getMaxNicknameLength() const noexcept { return 12; }

        /**
         * Whether this format can represent every character of `value`. Only Gen 3 ever says no: it
         * predates Unicode and stores names in its own single-byte table, so a name the Switch keyboard
         * was happy to produce may be unwritable. Check before setNickname -- the Gen 3 encoder ends the
         * name at the first character it can't map, which silently truncates rather than refusing.
         */
        virtual bool canStoreNickname(const std::u16string& value) const noexcept { (void)value; return true; }

        /** "Has a custom nickname" flag. Formats that don't wire it report false / ignore the set. */
        virtual bool isNicknamed() const noexcept { return false; }
        virtual void setIsNicknamed(bool value) noexcept { (void)value; }

        /** Fateful-encounter ("obtained in a fateful encounter") flag; false / no-op where unwired. */
        virtual bool isFatefulEncounter() const noexcept { return false; }
        virtual void setFatefulEncounter(bool value) noexcept { (void)value; }

        /** Original Trainer name (UTF-16; empty if unwired). */
        virtual std::u16string otName() const { return std::u16string(); }
        virtual void setOTName(const std::u16string& value) noexcept { (void)value; }

        /** Handling (current) Trainer name (UTF-16; empty if unwired). */
        virtual std::u16string htName() const { return std::u16string(); }
        virtual void setHTName(const std::u16string& value) noexcept { (void)value; }

        /** Handling Trainer gender (0 = Male, 1 = Female). */
        virtual uint8_t htGender() const noexcept { return 0; }
        virtual void setHTGender(uint8_t value) noexcept { (void)value; }

        /** Handling Trainer friendship (0-255). */
        virtual uint8_t htFriendship() const noexcept { return 0; }
        virtual void setHTFriendship(uint8_t value) noexcept { (void)value; }

        /** Current handler flag (0 = OT active, 1 = HT active). */
        virtual uint8_t currentHandler() const noexcept { return 0; }
        virtual void setCurrentHandler(uint8_t value) noexcept { (void)value; }

        // ========================================
        // Core editable setters
        // ========================================
        // Getters for most of these already exist above (speciesID/heldItem/ability/
        // nature/statNature/form/pid/encryptionConstant/friendship/isEgg). These add
        // the write side. Implementations refresh the checksum, and recalculate stats
        // when the field affects them (species/form/statNature).

        virtual void setSpecies(uint16_t species) noexcept { (void)species; }
        virtual void setForm(uint8_t form) noexcept { (void)form; }
        virtual void setHeldItem(uint16_t item) noexcept { (void)item; }
        virtual void setAbility(uint16_t ability) noexcept { (void)ability; }

        /** Ability slot number (which of the species' abilities: 1/2/H). */
        virtual uint8_t abilityNumber() const noexcept { return 0; }
        virtual void setAbilityNumber(uint8_t number) noexcept { (void)number; }

        virtual void setNature(uint8_t nature) noexcept { (void)nature; }
        virtual void setStatNature(uint8_t nature) noexcept { (void)nature; }
        virtual void setPID(uint32_t pid) noexcept { (void)pid; }
        virtual void setEncryptionConstant(uint32_t ec) noexcept { (void)ec; }
        virtual void setFriendship(uint8_t value) noexcept { (void)value; }
        virtual void setEgg(bool egg) noexcept { (void)egg; }

        /** Sets gender (0 = Male, 1 = Female, 2 = Genderless). gender() is the getter. */
        virtual void setGender(uint8_t gender) noexcept { (void)gender; }

        /**
         * Sets the Pokemon's level (level() is the getter). Writes the level's minimum
         * total EXP, refreshes the cached party-level byte, then recalculates stats and
         * checksum. Implementations clamp to [1,100].
         */
        virtual void setLevel(uint8_t level) noexcept {}   // default no-op; overridden where supported

        /** Sets total EXP directly and re-derives the level; no-op where unwired. */
        virtual void setExp(uint32_t value) noexcept { (void)value; }

        // ========================================
        // Base Stats (Species-Dependent)
        // ========================================

        /**
         * Base stats are determined by species and don't change per individual.
         * These are looked up from the species data table.
         */
        virtual uint8_t baseHP() const noexcept = 0;
        virtual uint8_t baseATK() const noexcept = 0;
        virtual uint8_t baseDEF() const noexcept = 0;
        virtual uint8_t baseSPE() const noexcept = 0;
        virtual uint8_t baseSPA() const noexcept = 0;
        virtual uint8_t baseSPD() const noexcept = 0;

        // ========================================
        // Calculated Stats (Battle Stats)
        // ========================================

        /**
         * These are the actual stats used in battle, calculated from:
         * - Base stats (species-dependent)
         * - IVs (individual values)
         * - EVs (effort values)
         * - Nature (stat modifiers)
         * - Level
         */
        virtual uint16_t statHPMax() const noexcept = 0;
        virtual uint16_t statATK() const noexcept = 0;
        virtual uint16_t statDEF() const noexcept = 0;
        virtual uint16_t statSPE() const noexcept = 0;
        virtual uint16_t statSPA() const noexcept = 0;
        virtual uint16_t statSPD() const noexcept = 0;
        virtual uint16_t statHPCurrent() const noexcept { return statHPMax(); }
        virtual void setStatHPCurrent(uint16_t value) noexcept { (void)value; }

        // ========================================
        // Status and Conditions
        // ========================================

        /**
         * Gets the Pokemon's friendship/happiness value.
         * @return Friendship value (0-255)
         */
        virtual uint8_t friendship() const noexcept = 0;

        /**
         * Checks if the Pokemon is an egg.
         * @return true if egg, false otherwise
         */
        virtual bool isEgg() const noexcept = 0;

        /**
         * Checks if the Pokemon is shiny (alternate coloration).
         * Shininess is determined by XOR of trainer ID and PID.
         * @param trainerID32 The trainer's ID32 value
         * @param species Species name (for logging/debugging)
         * @return true if shiny, false otherwise
         */
        virtual bool isShiny(uint32_t trainerID32, std::string species) const noexcept = 0;

        /**
         * Checks if the Pokemon is infected with Pokerus.
         * @return true if infected, false otherwise
         */
        virtual bool isPokerusInfected() const noexcept = 0;

        /**
         * Checks if the Pokemon has been cured of Pokerus.
         * @return true if cured, false otherwise
         */
        virtual bool isPokerusCured() const noexcept = 0;

        /**
         * Sets the raw Pokerus byte (high nibble = strain, low nibble = days remaining). Canonical
         * editor values: 0x00 = none, 0x12 = freshly infected (strain 1, 2 days), 0x10 = cured.
         * Default no-op for games with no Pokerus mechanic (Let's Go).
         */
        virtual void setPokerus(uint8_t /*value*/) noexcept {}

        /** Whether this game actually has the Pokerus mechanic, so editing it is meaningful. */
        virtual bool hasPokerus() const noexcept { return false; }

        // ========================================
        // Data Integrity
        // ========================================

        /**
         * Gets the stored checksum value.
         * The checksum validates data integrity.
         * @return Checksum value
         */
        virtual uint16_t checksum() const noexcept = 0;

        /**
         * Calculates the checksum from current data.
         * @return Calculated checksum value
         */
        virtual uint16_t calculateChecksum() const noexcept = 0;

        /**
         * Updates the stored checksum to match current data.
         * This MUST be called after any data modifications.
         */
        virtual void refreshChecksum() noexcept = 0;

        /**
         * Validates that the stored checksum matches calculated checksum.
         * @return true if valid, false if corrupted
         */
        virtual bool checksumValid() const noexcept = 0;

        // ========================================
        // Stat Recalculation
        // ========================================

        /**
         * Recalculates all battle stats based on current IVs, EVs, nature, and level.
         * This should be called after modifying any stat-affecting values.
         */
        virtual void recalculateStats() noexcept = 0;

        // ========================================
        // Advanced Modification
        // ========================================

        /**
         * Regenerates PID while maintaining gender and shininess.
         * Used to fix legality issues when IVs are modified.
         * @param trainerID32 The trainer's ID32 for shiny calculation
         */
        virtual void regeneratePID(uint32_t trainerID32) noexcept = 0;

        /**
         * Sets the shiny status of the Pokemon.
         * Modifies PID while preserving gender.
         * @param makeShiny true to make shiny, false to make non-shiny
         * @param trainerID32 The trainer's ID32 for shiny calculation
         */
        virtual void setShiny(bool makeShiny, uint32_t trainerID32) noexcept = 0;

        // ========================================
        // Data Access
        // ========================================

        /**
         * Gets the size of the Pokemon data.
         * @return Data size in bytes
         */
        size_t getDataSize() const noexcept { return dataSize; }

        /**
         * Gets direct access to the decrypted data buffer.
         * WARNING: Use with caution. Modifying data directly requires
         * calling RefreshChecksum() afterwards.
         * @return Span view of the data buffer
         */
        std::span<std::byte> getData() noexcept { return data; }

        /**
         * Gets read-only access to the decrypted data buffer.
         * @return Const span view of the data buffer
         */
        std::span<const std::byte> getData() const noexcept { return data; }

        /** Deep-copy this Pokemon into a new owning instance (nullptr if unsupported). */
        virtual std::unique_ptr<Pokemon> clone() const { return nullptr; }

    protected:
        // Default constructor for derived classes
        Pokemon() = default;
    };
}

#endif
