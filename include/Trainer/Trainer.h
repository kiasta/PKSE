#ifndef TRAINER_TRAINER_H
#define TRAINER_TRAINER_H

#include <cstdint>
#include <vector>
#include <span>
#include <array>
#include <memory>
#include <string>

#include "Save/Block.h"
#include "Utils/Logger.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Trainer/Inventory.h"
#include "Pokemon/Pokemon.h"
#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"

using namespace Save;
using namespace Utils;
using namespace Enums;

namespace Trainer
{

    // Pokemon storage constants (common across generations)
    constexpr size_t MAX_PARTY_SLOTS = 6; // Maximum Pokemon in party
    constexpr size_t BOX_SLOTS = 30;      // Pokemon per box (6x5 grid)

    const char *getSpeciesName(uint16_t speciesId);

    const char *getItemName(uint16_t itemId);

    size_t getItemCount();

    const char *getNatureName(uint8_t natureId);

    const char *getAbilityName(uint16_t abilityId);
}

namespace Trainer
{
    class Trainer
    {
    protected:
        /**
         * All save file blocks for re-serialization.
         * Blocks contain encrypted data segments identified by key values.
         */
        std::vector<Block> blocks;

    public:

        /// Trainer name (UTF-8 string)
        std::string trainerName;

        /// Money/currency amount
        uint32_t money;

        /// Trainer ID (32-bit format: SID16 << 16 | TID16)
        uint32_t ID32;

        /// Trainer gender (0 = Male, 1 = Female), read from the save. Stamped as a created pokemon's OT
        /// gender so it matches the current trainer -- Gen 3 flags a mismatch as "Apparently met".
        uint8_t trainerGender = 0;

        /// Trainer ID (16-bit visible ID)
        uint16_t TID16;

        /// Secret ID (16-bit hidden ID)
        uint16_t SID16;

        /// Display Trainer ID (Gen 7+)
        ///
        /// NOTE ON NAMING. TID/SID (and TID16/SID16/ID32 above) keep PKHeX's spellings, which are
        /// also what the games' own documentation and the community use. Spelling out only two of
        /// the five would leave the set less uniform than it is now, not more, and every offset in
        /// this layer is cross-checked against PKHeX by name.
        uint32_t TID;

        /// Display Secret ID (Gen 7+)
        uint32_t SID;

        /// Save file revision (DLC version detection)
        /// 0 = Base game, higher values indicate DLC/updates
        int saveRevision = 0;

        /// Owning a DLC is NOT a precondition for holding its content. The games gate the
        /// DLC *areas*, not the Pokemon -- a player without the Expansion Pass can be traded an
        /// Isle of Armor or Crown Tundra species (or one sent from HOME) and use it normally,
        /// because the patch ships the data to everyone. PKHeX agrees: nothing under
        /// PKHeX.Core/Legality references SaveRevision, and its legality caps are the
        /// full-DLC ones unconditionally. So revision must never be used to decide what a save
        /// may CONTAIN. Its one real constraint is the Pokedex -- PKHeX declines to *register* a
        /// DLC-group species in a DLC-less save -- which is about the dex structure, not the
        /// Pokemon's validity.

        /// Human-readable save revision string (e.g., "Base", "IoA", "CT", "MD")
        std::string saveRevisionString = "Base";

        /// Inferred game version string (e.g., "v1.0", "v1.3", "v2.0")
        /// Based on save revision and known version mappings
        std::string gameVersionString = "";

        /// Items organized by pouch type (Medicine, Balls, etc.)
        std::vector<std::vector<InventoryItem>> items;

        /// Names of each box (UTF-8 strings)
        std::vector<std::string> boxNames;

        /// Storage box the game/editor should open on. Persisted for the games that store it
        /// (SV / Z-A "U32 Box Index" block); left 0 for games PKSE doesn't round-trip it for.
        uint8_t currentBox = 0;
        uint8_t getCurrentBox() const noexcept { return currentBox; }
        void setCurrentBox(uint8_t box) noexcept { currentBox = box; }

        std::vector<std::unique_ptr<::Pokemon::Pokemon>> party;

        /// Box Pokemon storage [box_index][slot_index] - stored polymorphically
        /// nullptr = empty slot
        std::vector<std::array<std::unique_ptr<::Pokemon::Pokemon>, BOX_SLOTS>> boxes;

        explicit Trainer(std::vector<Block> blocks) : blocks(std::move(blocks)) {}

        /// Virtual destructor to ensure proper cleanup in derived classes
        virtual ~Trainer() = default;

        Trainer(const Trainer &) = delete;
        Trainer &operator=(const Trainer &) = delete;

        // Allow move operations for efficient transfers
        Trainer(Trainer &&) noexcept = default;
        Trainer &operator=(Trainer &&) noexcept = default;

        /**
         * Updates the party block with modified Pokemon data.
         * Each generation implements this with generation-specific encryption.
         */
        virtual void updatePartyBlock() = 0;

        /**
         * Updates the box blocks with modified Pokemon data.
         * Each generation implements this with generation-specific encryption.
         */
        virtual void updateBoxBlock() = 0;

        /**
         * Updates the item block with modified inventory data.
         * Item structure varies slightly between generations.
         */
        virtual void updateItemBlock() = 0;

        /**
         * Max number of item slots pouch `pouch` can hold, for the add-item flow. Default 0
         * means "appending unsupported" (the UI uses a large sentinel for the id-indexed games, which
         * never append). Every slot-list group from Gen 1 to Gen 7 overrides this: its writer stores
         * only that many entries and silently drops the rest, so an add past it must be refused.
         * Legends: Arceus overrides it too: its pouches are fixed-capacity packed lists, and the
         * general-items bag grows with the player's Satchel Upgrades (as in PKHeX PlayerBag8a).
         */
        virtual size_t getItemPouchCapacity(int pouch) const { return 0; }

        /**
         * Largest stack pouch `pouch` holds for one item, or 0 when the group leaves it to the
         * editor's own per-pouch limits. The slot-list groups from Gen 1 to Gen 7 answer from their
         * Inventory tables (PKHeX's per-pouch MaxCount).
         */
        virtual int getItemMaxCount(int pouch) const { return 0; }

        /// Display name of pouch `pouch`, or nullptr when the group leaves it to the editor.
        virtual const char *getItemPouchName(int pouch) const { return nullptr; }

        /**
         * True if items are stored ID-INDEXED (count at itemId * stride: BDSP / S-V / Z-A), false if
         * SLOT-BASED (a list of {itemId,count}: Gens 1-7, LGPE, SWSH, LA). This decides how the
         * editor removes or retypes an item, because the two models save opposite ways:
         *   - slot-based: erase the entry / reassign its itemId (the region is rewritten each save);
         *   - id-indexed: set count 0 but KEEP the entry so its id-slot is written to 0 (updateItemBlock
         *     only touches ids still present, so an erased entry would ghost the old count).
         */
        virtual bool itemsAreIdIndexed() const { return false; }

        /// A species-0, sanity-0, checksum-valid entity in this trainer's format: zeros encrypted
        /// with seed 0, which the generation's ctor decrypts straight back. Raw zeros must never
        /// reach a ctor -- they decrypt to a Bad Egg.
        virtual std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const = 0;

        virtual size_t getBoxCount() const noexcept = 0;

        virtual size_t getSlotsPerBox() const noexcept = 0;

        virtual size_t getPartySize() const noexcept = 0;

        virtual GameVersion getGameGroup() const noexcept = 0;

        virtual GameVersion getGameVersion() const noexcept = 0;

        /**
         * The language the SAVE is written in, as an Enums::LanguageID byte.
         *
         * This is a different question from what language the person holding the console reads in,
         * and it is asked for exactly one reason: a Pokemon created here is stamped with it, so the
         * trainer name copied into its OT field can actually be stored. On a Japanese Gen 3 save
         * that matters, because the language decides which character table the name is written
         * through and the Japanese one is the only place its alphabet exists.
         *
         * English by default. Only the two Gen 3 groups override it -- Gen 3 is the last
         * generation whose text is a per-language font
         * map, so from Gen 4 on the save's language cannot make a name unstorable and there is
         * nothing here for a newer format to answer. Gen 3 cannot tell its five international
         * languages apart either (no save byte says which), so an override answers Japanese or
         * English and nothing finer; PKHeX's SAV3 stops in the same place.
         */
        virtual uint8_t language() const noexcept
        {
            return static_cast<uint8_t>(Enums::LanguageID::English);
        }

        /// Let's Go only -- the Partner Pikachu/Eevee. No other format has such a slot.
        virtual bool isStarterPokemon(size_t boxIndex, size_t slotIndex) const noexcept
        {
            (void)boxIndex;
            (void)slotIndex;
            return false; // Default: no starter tracking for most games
        }

        virtual int getPartyPosition(size_t boxIndex, size_t slotIndex) const noexcept
        {
            (void)boxIndex;
            (void)slotIndex;
            return 0; // Default: no index-based party tracking for most games
        }

        virtual bool isPartyPokemonStarter(size_t partyIndex) const noexcept
        {
            (void)partyIndex;
            return false; // Default: no starter tracking for most games
        }

        virtual void mirrorPartyMemberFromBox(size_t boxIndex, size_t slotIndex)
        {
            (void)boxIndex;
            (void)slotIndex;
        }

        virtual void mirrorPartyMemberFromParty(size_t partyIndex)
        {
            (void)partyIndex;
        }

        const std::vector<Block> &getBlocks() const
        {
            return blocks;
        }

        /**
         * Swaps the Pokemon occupying two box slots. Derived classes may override to also
         * update generation-specific bookkeeping (e.g. LGPE party/starter storage pointers
         * that reference slots by index).
         */
        virtual void swapBoxSlots(size_t firstBox, size_t firstSlot, size_t secondBox, size_t childStatus)
        {
            if (firstBox >= boxes.size() || secondBox >= boxes.size())
                return;
            if (firstSlot >= BOX_SLOTS || childStatus >= BOX_SLOTS)
                return;
            std::swap(boxes[firstBox][firstSlot], boxes[secondBox][childStatus]);
        }

        virtual bool compactStorage() { return false; }

        /**
         * Whether this game actually STORES box names in its save.
         *
         * Let's Go does not — PKHeX's `SAV7b` implements no `IBoxDetailName` and Beluga has no
         * BoxLayout at all, because its storage is one flat 1000-slot list with no per-box
         * metadata. The `"Box N"` strings PKSE shows for LGPE are its own UI labels, so a rename
         * there would have nowhere to go. The rename UI must gate on this rather than appear and
         * silently discard the user's input.
         */
        virtual bool supportsBoxNames() const noexcept { return false; }

        /**
         * Serialize `boxNames` back into the save. Mirrors `updateBoxBlock()` and must be called
         * from the same place in each game's save function — critically, BEFORE that game's
         * checksum/hash pass, since the names live inside the checksummed region.
         *
         * Default is a no-op for the games that have nowhere to put them (see supportsBoxNames).
         */
        virtual void updateBoxNameBlock() {}

        /**
         * Serialize `currentBox` back into the save (the "U32 Box Index" block). Called from the
         * same place as updateBoxNameBlock, before the checksum pass. Default no-op for games PKSE
         * doesn't persist a current-box index for (their in-game box selection is left untouched).
         */
        virtual void updateCurrentBoxBlock() {}

        /**
         * Register every Pokemon now in the party and boxes as seen + caught in this game's Pokedex.
         * Called from the same place as updateBoxNameBlock, BEFORE the checksum/hash pass.
         *
         * Done at SAVE time over the whole storage rather than at the point a Pokemon is created or
         * withdrawn, deliberately: there are several ways a Pokemon can enter a save (the creator, a
         * bank withdrawal, a bulk move, a party swap) and hooking each one is how a path gets missed.
         * Walking storage once catches all of them, and it is self-healing -- a Pokemon an older build
         * added without registering gets picked up the next time the save is written.
         *
         * Only ever SETS flags. A player's existing Pokedex is never cleared, so a game the player has
         * legitimately progressed cannot be walked backwards by opening it in PKSE. It must also be
         * idempotent: an unedited save writes the same dex again. Legends: Arceus is the one format
         * with counters (its research tasks), and it counts only Pokemon that arrived since the save
         * was read or last written.
         *
         * Default is a no-op for the games whose Pokedex format is not implemented yet -- those saves
         * are left exactly as they were rather than half-written.
         */
        virtual void updatePokedexBlock() {}

        /** Longest box name this game accepts, in characters (not bytes). 0 = renaming unsupported. */
        virtual size_t getMaxBoxNameLength() const noexcept { return 0; }

        /**
         * Boxes whose name the user actually changed this session.
         *
         * `updateBoxNameBlock()` writes ONLY these, and that restriction is load-bearing. Every
         * game's box-name parser substitutes a display default ("Box 3") when the save holds an
         * empty name, so `boxNames` is a mix of real names and placeholders. Writing the whole
         * array back persists placeholders the player never typed — writing an untouched Z-A save
         * back that way drifted 215 bytes of invented names.
         */
        std::vector<bool> boxNameDirty;
        void markBoxNameDirty(size_t box)
        {
            if (boxNameDirty.size() < boxNames.size())
                boxNameDirty.resize(boxNames.size(), false);
            if (box < boxNameDirty.size())
                boxNameDirty[box] = true;
        }
        bool isBoxNameDirty(size_t box) const noexcept
        {
            return box < boxNameDirty.size() && boxNameDirty[box];
        }

        /**
         * Whether this game's character set can represent `name`.
         *
         * The Switch keyboard happily produces accents, CJK and emoji. Gen 8/9 store UTF-16 and take
         * essentially anything, but Gen 3 has a ~70-glyph table — so a perfectly ordinary-looking
         * name can be unstorable there. The UI must ask BEFORE accepting, rather than let the write
         * path silently drop characters and hand back a mangled name.
         */
        virtual bool canStoreBoxName(const std::string &name) const
        {
            (void)name;
            return true;
        }

        /**
         * Serialize the editable trainer-identity fields (money, OT name) back into this
         * save's blocks/buffer. Mirrors updateItemBlock()/updateBoxBlock() and MUST be called from the
         * same place in each game's save function -- BEFORE that game's checksum/hash/encrypt pass,
         * since these fields live inside the checksummed region.
         *
         * Each field is written to the SAME block+offset it was parsed from, so the value the editor
         * shows round-trips exactly. Pure virtual so a new game can't drop trainer edits: a
         * missing override is a compile error, not a quiet no-persist.
         *
         * Editing gender is intentionally disabled ATM as it needs its own research and implementation.
         */
        virtual void updateTrainerInfoBlock() = 0;

        /**
         * Largest money value this game stores, for the money editor's clamp. Defaults to the modern
         * cap (PKHeX SaveFile.MaxMoney = 9,999,999); Gen 3 overrides to 999,999 (PKHeX SAV3.MaxMoney).
         */
        virtual uint32_t getMaxMoney() const noexcept { return 9999999; }

        /**
         * Longest OT name this game accepts, in characters. Defaults to 12 (PKHeX MaxStringLengthTrainer
         * for every Switch title); Gen 3 overrides to 7. The Gen-3 CHARACTER-SET limit is enforced
         * separately via canStoreBoxName() (its ~70-glyph table is shared by box and trainer names).
         */
        virtual size_t getMaxTrainerNameLength() const noexcept { return 12; }

        /**
         * How many DIGITS a trainer name may contain -- a separate cap from the length, and one the
         * games enforce at name entry. PKHeX TrainerNameVerifier.GetMaxNumberCount: no limit before
         * generation 4, four in gens 4-5, five from generation 6 on. Every Switch title is generation 6+, so 5; Gen 3
         * overrides to "no limit". Negative means unlimited.
         *
         * A name no longer than the cap is exempt, so "12345" is accepted on a cap of five while
         * "123456" is not -- see nameHasTooManyDigits() in TrainerViewScreen.cpp.
         */
        virtual int getMaxTrainerNameDigits() const noexcept { return 5; }

    protected:
        /**
         * Default constructor for derived classes.
         * Protected to prevent direct instantiation of base class.
         */
        Trainer() = default;
    };
}

#endif
