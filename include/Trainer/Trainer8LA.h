/**
 * Generation 8 Legends: Arceus Trainer/Save File Data Management
 *
 * Trainer8LA implements Legends: Arceus-specific logic for:
 * - PA8 Pokemon storage (party and boxes)
 * - Gen 8 block keys
 * - Gen 8 encryption (encryptArray8LA/decryptArray8LA)
 * - Gen 8-specific save file structure
 */

#ifndef TRAINER_TRAINER8LA_H
#define TRAINER_TRAINER8LA_H

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon8LA.h"
#include "Encryption/Encryption8LA.h"
#include "Trainer/Inventory8LA.h"

using namespace Pokemon;
using namespace Encryption;

namespace Trainer
{

    // Core game blocks
    constexpr size_t MY_STATUS8_LA = 0xf25c070e;  // Trainer Details (MyStatus8a)
    constexpr size_t PARTY8_LA = 0x2985fe5d;
    constexpr size_t MONEY8_LA = 0x3279D927;      // Money (u32 scalar block, max 9,999,999)
    constexpr size_t PLAY_TIME8_LA = 0xC4FA7C8C;
    constexpr size_t BOX8_LA = 0x47E1CEAB;        // Box Data (stored-size 0x168 slots)
    constexpr size_t BOX_LAYOUT8_LA = 0x19722c89; // Box Names

    // Item pouches — each is a packed list of 4-byte {itemId u16, count u16} entries (Inventory8LA.h).
    constexpr size_t ITEM_REGULAR8_LA = 0x9FE2790A;     // General items
    constexpr size_t ITEM_KEY8_LA = 0x59A4D0C3;         // Key items
    constexpr size_t ITEM_STORED8_LA = 0x8E434F0D;      // Item storage box
    constexpr size_t ITEM_RECIPE8_LA = 0xF5D9F4A5;      // Crafting recipes
    constexpr size_t SATCHEL_UPGRADES8_LA = 0x75CE2CF6; // u32 Satchel Upgrades (0-39): grows the bag

    // Fixed pouch capacities from PKHeX PlayerBag8a: Key Items 100, Storage 180, Recipes 70. The
    // general Items bag is min(675, SatchelUpgrades + 20) -- computed at runtime (see .cpp).
    constexpr size_t POUCH_CAP_KEY8_LA = 100;
    constexpr size_t POUCH_CAP_STORED8_LA = 180;
    constexpr size_t POUCH_CAP_RECIPE8_LA = 70;
    constexpr size_t POUCH_CAP_REGULAR_MAX8_LA = 675;

    // Version detection block
    constexpr size_t SAVE_REVISION8_LA = 0x0926555A; // Save Revision (u64)

    // Pokedex ("Zukan"). PLA's dex is a research LOG, one big
    // 0x1E460 block holding a per-species research entry (0x58 each, 981) and a per-species+FORM
    // statistics entry (0x18 each, 1480) reached through a lookup table. PKHeX PokedexSave8a.
    // (The placeholder that sat here was 0x2D87BE5C -- Legends: Z-A's key, copy-pasted.)
    constexpr size_t POKEDEX8_LA = 0x02168706;

    // Additional blocks (for future use)
    constexpr size_t CURRENT_BOX8_LA = 0x017C3CBB; // Current box index ("U8 Box Index")

    // Generation 8 constants
    constexpr size_t BOX_COUNT8_LA = 32;         // Number of boxes in Legends: Arceus
    constexpr size_t BOX_NAME_LENGTH8_LA = 0x22; // 34 bytes per box name (UTF-16LE)

    /// The per-species research fields the Pokedex writer changes when a Pokemon arrives.
    enum class ResearchField8LA : uint8_t
    {
        CatchCount,
        AlphaCount,
        LargeCount,
        SmallCount,
        HeavyCount,
        LightCount,
        UpdateCounter,
        /// The research entry's flag word: bit 0 updated, bit 1 has a report, bits 3/4/5 the
        /// displayed gender (female), shiny and alpha (PKHeX PokedexSaveResearchEntry.Flags, low half).
        Flags,
    };

    class Trainer8LA final : public Trainer
    {
    public:

        // Legends: Arceus (PA8), SwishCrypto SCBlock save. Its box/party slots are PACKED (no inter-slot
        // gap); the named slot-stride members below MIRROR Trainer9LZA (Legends: Z-A) — which GAPS its
        // slots — so the shared parse/serialize logic reads identically across the two classes.
        explicit Trainer8LA(std::vector<Block> blocks) : Trainer(std::move(blocks))
        {
            party.reserve(MAX_PARTY_SLOTS);
            boxes.resize(BOX_COUNT8_LA);
            boxNames.resize(BOX_COUNT8_LA);

            for (const auto &block : this->blocks)
            {
                parseBlock(block);
            }
            storedPokemonCounts = countStoredPokemon();
        }

        ~Trainer8LA() override = default;

        Trainer8LA(const Trainer8LA &) = delete;
        Trainer8LA &operator=(const Trainer8LA &) = delete;

        Trainer8LA(Trainer8LA &&) noexcept = default;
        Trainer8LA &operator=(Trainer8LA &&) noexcept = default;

        /**
         * Updates the PARTY_KEY block with modified Pokemon data.
         * Uses Gen 8 encryption (encryptArray8LA).
         */
        void updatePartyBlock() override;

        /**
         * Updates the BOX_KEY block with modified Pokemon data.
         * Uses Gen 8 encryption (encryptArray8LA).
         */
        void updateBoxBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        bool supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return BOX_NAME_LENGTH8_LA / 2 - 1; }

        void updateItemBlock() override;
        void updateTrainerInfoBlock() override; // money / OT name
        void updatePokedexBlock() override;     // PokedexSave8a: research + statistics entries

        /// One of a species' research fields, read from the Pokedex block's bytes. 0 for a species
        /// outside the research log or a block too short to hold it.
        static uint16_t researchField(const std::vector<uint8_t> &pokedexBytes, uint16_t speciesId, ResearchField8LA field) noexcept;

        /// The update counter the Pokedex hands the next species whose research changes.
        static uint16_t nextResearchUpdateCounter(const std::vector<uint8_t> &pokedexBytes) noexcept;

        // Fixed-capacity packed pouches (KeyItems 100 / Stored 180 / Recipes 70), and the general
        // Items bag = min(675, SatchelUpgrades + 20). Enables in-pouch item creation for PLA.
        size_t getItemPouchCapacity(int pouch) const override;

        /**
         * Creates a species-0, checksum-valid blank PA8 entity (mirrors updateBoxBlock()'s
         * encrypted-blank fallback: zeroed party-size SIZE_PARTY8_LA buffer -> encryptArray8LA(seed 0)
         * -> Pokemon8LA, which keeps a party-size buffer). Starting point for the Pokemon creator.
         */
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override
        {
            return BOX_COUNT8_LA;
        }

        size_t getSlotsPerBox() const noexcept override
        {
            return BOX_SLOTS;
        }

        size_t getPartySize() const noexcept override
        {
            return party.size();
        }

        GameVersion getGameGroup() const noexcept override
        {
            return GameVersion::PLA;
        }

        /// Legends: Arceus is the only title in its group, so the group IS the title and there is
        /// no game byte to consult.
        GameVersion getGameVersion() const noexcept override { return GameVersion::PLA; }

    private:
        // Legends: Arceus PACKS its slots: party slots are SIZE_PARTY8_LA (0x178) and box slots
        // SIZE_STORED8_LA (0x168), stored back-to-back with no inter-slot gap. (Named members mirror
        // Trainer9LZA — which gaps its slots — so the shared parse/serialize logic reads identically.)
        size_t partySlotStride = SIZE_PARTY8_LA; // packed: no gap between party slots
        size_t boxSlotStride = SIZE_STORED8_LA;  // packed: no gap between box slots
        size_t slotGapZero = 0;                  // no gap bytes after pokemon data

        /**
         * How many of each Pokemon storage held when the save was read or last written, keyed on
         * encryption constant + species. updatePokedexBlock counts a Pokemon toward catch research
         * only while storage holds more of its key than this, so one that was already here, or was
         * only moved, never counts again. A clone shares its original's key, hence counts, not a set.
         */
        std::map<uint64_t, size_t> storedPokemonCounts;
        std::map<uint64_t, size_t> countStoredPokemon() const;

        void parseBlock(const Block &block);

        /**
         * Parses the MY_STATUS block (MyStatus8a) for trainer ID + name.
         * Location: ID32 at 0x10 (TID16 0x10 / SID16 0x12), OT name at 0x20 (26 bytes).
         */
        void parseMyStatusBlock(const Block &block);

        /**
         * Parses the PARTY block to extract party Pokemon.
         * Format: 6 slots of SIZE_PARTY8_LA (0x178 = 376 bytes each)
         */
        void parsePartyBlock(const Block &block);

        /** Money is at 0x04. */
        void parseMoneyBlock(const Block &block);

        // THERE'S NO TRAINER CARD PARSING IN GEN 8

        void parseItemBlock(const Block &block);

        /**
         * Parses the BOX block to extract box Pokemon.
         * Format: 32 boxes * 30 slots * SIZE_STORED8_LA (0x168 = 360 bytes each)
         */
        void parseBoxBlock(const Block &block);

        /** 32 names of BOX_NAME_LENGTH bytes, UTF-16LE. */
        void parseBoxLayoutBlock(const Block &block);

        /** Parses the CURRENT_BOX block ("U8 Box Index") so the editor opens on the last box used. */
        void parseCurrentBoxBlock(const Block &block);

        /**
         * Parses the SAVE_REVISION block (u64) for version detection. Legends: Arceus shipped without
         * DLC, so only the base-game revision is expected.
         */
        void parseSaveRevisionBlock(const Block &block);
    };
}

#endif
