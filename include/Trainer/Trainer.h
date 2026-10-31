#ifndef TRAINER_TRAINER_H
#define TRAINER_TRAINER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    /// Byte offset of the species in a DECRYPTED Gen 4-9 record (Gen 3 keeps it in a shuffled substructure).
    constexpr size_t RECORD_SPECIES_OFFSET = 0x08;

    const char *getSpeciesName(uint16_t speciesId);

    const char *getItemName(uint16_t itemId);

    size_t getItemCount();

    const char *getNatureName(uint8_t natureId);

    const char *getAbilityName(uint16_t abilityId);

    /**
     * A counted Gen 1/2 list entry PKSE cannot turn into a Pokemon -- a glitch species such as
     * MissingNo., or a marker outside the species range. The games and PKHeX's PokeList1/PokeList2
     * keep every counted entry whatever it holds, so it is carried as the raw bytes of its list
     * columns and written back verbatim, at its own position, unless something now occupies it.
     */
    struct UnmappedListEntry
    {
        size_t entryIndex;
        uint8_t marker;
        std::vector<uint8_t> bodyBytes;
        std::vector<uint8_t> originalTrainerNameBytes;
        std::vector<uint8_t> nicknameBytes;
    };

    /// One entry a Gen 1/2 list writer emits: a Pokemon, or an UnmappedListEntry kept from the file.
    struct ListEntrySource
    {
        ::Pokemon::Pokemon *pokemon;
        const UnmappedListEntry *unmappedEntry;
    };

    /**
     * The order a Gen 1/2 list is written in. A box hands its slots POSITIONALLY (slot N was list
     * entry N on load), so an unmapped entry returns to its slot while that slot is still empty. The
     * party is handed compacted, so an unmapped entry is put back at its original index, or at the
     * end when fewer entries precede it now.
     *
     * The storage view keeps the list within `capacity`: a kept entry's box slot is locked and
     * counts as occupied (Trainer::isBoxSlotUnmapped), and the party view shows each kept entry in
     * its party slot (Trainer::getUnmappedPartySlots). If a caller still hands more than fits, the
     * entries that fit are kept and the overflow is logged as an error, since a list cannot hold
     * more than `capacity` and something is lost.
     */
    inline std::vector<ListEntrySource> orderListEntries(const std::vector<::Pokemon::Pokemon *> &pokemonSlots, const std::vector<UnmappedListEntry> &unmappedEntries, size_t capacity, bool slotsArePositional)
    {
        const auto occupied = [](const ::Pokemon::Pokemon *pokemon) { return pokemon && pokemon->speciesID() != 0; };
        std::vector<ListEntrySource> listEntries;
        if (slotsArePositional)
        {
            for (size_t slotIndex = 0; slotIndex < capacity; ++slotIndex)
            {
                ::Pokemon::Pokemon *pokemon = slotIndex < pokemonSlots.size() ? pokemonSlots[slotIndex] : nullptr;
                // A Pokemon placed on a kept entry's (invisible) slot goes first; the kept entry
                // follows it rather than being dropped.
                if (occupied(pokemon))
                    listEntries.push_back({pokemon, nullptr});
                for (const UnmappedListEntry &unmappedEntry : unmappedEntries)
                {
                    if (unmappedEntry.entryIndex == slotIndex)
                        listEntries.push_back({nullptr, &unmappedEntry});
                }
            }
        }
        else
        {
            for (::Pokemon::Pokemon *pokemon : pokemonSlots)
            {
                if (occupied(pokemon))
                    listEntries.push_back({pokemon, nullptr});
            }
            for (const UnmappedListEntry &unmappedEntry : unmappedEntries)
            {
                const size_t insertIndex = std::min(unmappedEntry.entryIndex, listEntries.size());
                listEntries.insert(listEntries.begin() + static_cast<std::ptrdiff_t>(insertIndex), ListEntrySource{nullptr, &unmappedEntry});
            }
        }
        if (listEntries.size() > capacity)
        {
            Utils::logErrorToFile("orderListEntries: a Gen 1/2 list holds " + std::to_string(listEntries.size()) + " entries but has room for " + std::to_string(capacity) + "; the last " + std::to_string(listEntries.size() - capacity) + " are not written");
            listEntries.resize(capacity);
        }
        return listEntries;
    }

    /// The positions in a written list (orderListEntries' output) that hold an UnmappedListEntry.
    inline std::vector<size_t> unmappedListPositions(const std::vector<ListEntrySource> &listEntries)
    {
        std::vector<size_t> unmappedPositions;
        for (size_t listPosition = 0; listPosition < listEntries.size(); ++listPosition)
        {
            if (listEntries[listPosition].unmappedEntry)
                unmappedPositions.push_back(listPosition);
        }
        return unmappedPositions;
    }
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
         * English by default. The Gen 3 groups override it because their text is a per-language
         * font map; Gen 3 cannot tell its five international languages apart (no save byte says
         * which), so an override answers Japanese or English and nothing finer, as PKHeX's SAV3
         * does. The Gen 4 groups override it with the trainer record's language byte, so a Pokemon
         * created on a non-English save is not stamped English.
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
         * Whether the save parsed into something PKSE can show and write back. A reader always
         * returns a trainer (there are no exceptions), so every caller must ask this before opening
         * or saving one. The block-based formats are valid exactly when their reader produced
         * blocks; the flat formats answer from their own load.
         */
        virtual bool isValid() const noexcept
        {
            return !blocks.empty();
        }

        /**
         * Swaps the Pokemon occupying two box slots. Derived classes may override to also
         * update generation-specific bookkeeping (e.g. LGPE party/starter storage pointers
         * that reference slots by index).
         */
        virtual void swapBoxSlots(size_t firstBox, size_t firstSlot, size_t secondBox, size_t secondSlot)
        {
            if (firstBox >= boxes.size() || secondBox >= boxes.size())
                return;
            if (firstSlot >= BOX_SLOTS || secondSlot >= BOX_SLOTS)
                return;
            std::swap(boxes[firstBox][firstSlot], boxes[secondBox][secondSlot]);
        }

        /**
         * Whether a box slot belongs to one of the game's battle teams. The game keeps each team as
         * (box, slot) pointers, so moving, replacing or sorting such a slot leaves the team pointing
         * at whatever lands there; PKHeX refuses to overwrite these slots (team member or locked
         * team alike), and so must every storage action. False for a format without battle teams.
         */
        virtual bool isBoxSlotLocked(size_t boxIndex, size_t slotIndex) const noexcept
        {
            (void)boxIndex;
            (void)slotIndex;
            return false;
        }

        /**
         * Whether a box slot holds a kept UnmappedListEntry: a counted Gen 1/2 list entry PKSE cannot
         * read (MissingNo., a species past the game's range). The slot reads as null, but it is not
         * free -- the entry is written back there -- so it counts toward the box's capacity, and the
         * storage view shows it as a locked placeholder that nothing can be dropped onto. False for
         * every format without counted lists.
         */
        virtual bool isBoxSlotUnmapped(size_t boxIndex, size_t slotIndex) const noexcept
        {
            (void)boxIndex;
            (void)slotIndex;
            return false;
        }

        /**
         * The party slots (0-based, in written order) that hold a kept UnmappedListEntry. `party`
         * holds only the Pokemon, compacted; these entries sit between them when the party is
         * written, and they count toward MAX_PARTY_SLOTS. Empty for every format without counted
         * lists.
         */
        virtual std::vector<size_t> getUnmappedPartySlots() const { return {}; }

        /// The index into `party` of the Pokemon shown in party slot `partySlotIndex`, or -1 when
        /// that slot holds an unmapped entry (getUnmappedPartySlots) or nothing.
        int partyIndexAtSlot(size_t partySlotIndex) const
        {
            size_t unmappedBeforeCount = 0;
            for (const size_t unmappedSlotIndex : getUnmappedPartySlots())
            {
                if (unmappedSlotIndex == partySlotIndex)
                    return -1;
                if (unmappedSlotIndex < partySlotIndex)
                    ++unmappedBeforeCount;
            }
            const size_t partyIndex = partySlotIndex - unmappedBeforeCount;
            return partyIndex < party.size() ? static_cast<int>(partyIndex) : -1;
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

        /// The battle-team pointer list PKHeX reads in BoxLayout7, TeamIndexes8 and BoxLayout8b:
        /// six teams of six s16 entries, each (box << 8) | slot, negative for an empty team place.
        /// True when any entry names this box slot.
        static bool battleTeamsHoldSlot(const uint8_t *teamPointerBytes, size_t byteCount, size_t boxIndex, size_t slotIndex) noexcept
        {
            constexpr size_t BATTLE_TEAM_POINTER_COUNT = 6 * 6;
            for (size_t pointerIndex = 0; pointerIndex < BATTLE_TEAM_POINTER_COUNT && pointerIndex * 2 + 2 <= byteCount; ++pointerIndex)
            {
                const int16_t pointer = static_cast<int16_t>(teamPointerBytes[pointerIndex * 2] | (teamPointerBytes[pointerIndex * 2 + 1] << 8));
                if (pointer >= 0 && static_cast<size_t>(pointer >> 8) == boxIndex && static_cast<size_t>(pointer & 0xFF) == slotIndex)
                    return true;
            }
            return false;
        }

        /// battleTeamsHoldSlot() over the SwishCrypto block with this key (TeamIndexes8).
        bool battleTeamBlockHoldsSlot(size_t teamIndexesKey, size_t boxIndex, size_t slotIndex) const noexcept
        {
            for (const auto &block : blocks)
            {
                if (block.key == teamIndexesKey)
                    return battleTeamsHoldSlot(block.data.data(), block.data.size(), boxIndex, slotIndex);
            }
            return false;
        }

        /**
         * Whether the encrypted record at `slotBytes` is exactly `pokemon` as it was loaded, so a
         * writer can leave the file's bytes alone. Writers re-encrypt (and some re-checksum) every
         * record they touch; skipping an unchanged one is what keeps an unedited save byte-exact and
         * what keeps a Bad Egg's invalid checksum invalid, the way PKHeX leaves slots it never set.
         * It also catches a record moved elsewhere: the bytes still hold the old occupant, so they
         * no longer match whatever now sits in that slot.
         */
        template <typename DecryptRecord>
        static bool slotHoldsLoadedRecord(const ::Pokemon::Pokemon *pokemon, const uint8_t *slotBytes, size_t recordSize, DecryptRecord decryptRecord)
        {
            if (!pokemon || pokemon->getDataSize() < recordSize)
                return false;
            std::byte *decryptedRecord = decryptRecord(std::span<const std::byte>(reinterpret_cast<const std::byte *>(slotBytes), recordSize));
            const bool recordMatches = std::memcmp(decryptedRecord, pokemon->getData().data(), recordSize) == 0;
            delete[] decryptedRecord;
            return recordMatches;
        }

        /**
         * Whether the encrypted record at `slotBytes` already reads as an empty slot: it decrypts to
         * species 0 AND is not all zeros. Empty slots carry stale bytes the game never cleared, and
         * differ from each other, so the only way to leave them byte-exact is to not write them.
         *
         * The all-zero test is not redundant: the cipher seeds its PRNG with the encryption
         * constant, a zeroed slot has EC 0, and the first PRNG word from seed 0 is 0x0000 -- so an
         * all-zero slot decrypts to species 0 too while the rest is checksum-rejected garbage (the
         * game shows a Bad Egg). That slot must be repaired, not kept.
         */
        template <typename DecryptRecord>
        static bool slotHoldsGameBlank(const uint8_t *slotBytes, size_t recordSize, size_t speciesOffset, DecryptRecord decryptRecord)
        {
            bool anyNonZero = false;
            for (size_t byteIndex = 0; byteIndex < recordSize && !anyNonZero; ++byteIndex)
                anyNonZero = (slotBytes[byteIndex] != 0);
            if (!anyNonZero)
                return false;
            std::byte *decryptedRecord = decryptRecord(std::span<const std::byte>(reinterpret_cast<const std::byte *>(slotBytes), recordSize));
            const uint16_t speciesId = Utils::readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(decryptedRecord) + speciesOffset);
            delete[] decryptedRecord;
            return speciesId == 0;
        }
    };
}

#endif
