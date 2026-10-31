/**
 * This is the suite that USES PKSE rather than calling into it. It builds the actual
 * TrainerViewScreen over the run's scratch copy of a save and walks it through every view the app
 * has, pressing buttons the way a player would, while TestRunnerScreen puts the screen being driven
 * on the display so the walkthrough can be WATCHED.
 *
 * PACING IS LOAD-BEARING, and getting it wrong is what made the first version of this look like
 * nothing had changed. The runner's step() runs chunks back to back until its millisecond budget is
 * gone, so a walkthrough written as "one journey per chunk" completed inside a single frame and was
 * never presented -- the app was genuinely driven, and nothing was ever visible. So the script here
 * is a flat list of steps, ONE step per frame, with Runner::yieldFrame() ending each frame; the
 * frames between steps hold the result on screen long enough to read.
 *
 * TWO HAZARDS SHAPE WHAT IT PRESSES, and both matter before adding a step.
 *
 * 1. swkbd BLOCKS. The software keyboard is a library applet: it suspends PKSE until a human types
 *    something. An automated run that opened one would hang the console mid-pass with no way
 *    forward. So the script deliberately never presses the buttons that reach it -- A on a box-name
 *    header (rename), A on a trainer row (name / money), the nickname / EXP / met-date rows of the
 *    details modal, and A in the items view (quantity). Those paths belong to the "save" suite,
 *    which calls the edit functions directly instead of through the keyboard.
 *
 * 2. The BANK is real storage. TrainerViewScreen builds one in its constructor, so without an
 *    override the storage steps would open -- and could modify -- the user's own bank. The suite
 *    points it at the run's working directory first and puts it back afterwards.
 *
 * EVERY CHANGE IS DECLARED. Each step names the fields it is allowed to move; the journey snapshots
 * the whole app before and after and reports anything else. See Test/UIJourney.h for why that
 * inversion is the point rather than a detail.
 *
 * NOT COVERED: touch. TouchInput reads the panel inside update() and keeps its state private, so
 * driving it would mean a test hook in shipped code. Everything here is reachable by button.
 */
#include <algorithm>
#include "Test/TestSuite.h"
#include "Utils/Keyboard.h" // keyboardPromptsSuppressed -- a journey must never reach a prompt
#include "Test/TestRunner.h"
#include "Test/UIJourney.h"
#include "Names/ItemPouches.h"

#include <cstring>
#include <memory>
#include <string>

#include "Trainer/Bank.h"
#include "Trainer/Trainer.h"
#include "UI/TrainerViewScreen.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon7LGPE.h" // the creator's Let's Go size, spirit and mood checks
#include "Pokemon/PersonalRecord.h" // the creator's friendship check
#include "Pokemon/Evolution.h" // speciesNameAsStored -- the creator's default nickname
#include "Utils/Logger.h"
#include "Utils/StringHelpers.h" // utf16ToUtf8 -- the creator check names the nickname it saw

namespace Test
{

    namespace
    {
        using ViewModeEnum = UI::TrainerViewScreen::ViewMode;
        using TrainerView = UI::TrainerViewScreen;

        namespace Button
        {
            constexpr u64 NONE = 0;
            constexpr u64 BUTTON_A = HidNpadButton_A;
            constexpr u64 BUTTON_B = HidNpadButton_B;
            constexpr u64 BUTTON_Y = HidNpadButton_Y;
            constexpr u64 DPAD_UP = HidNpadButton_Up;
            constexpr u64 DPAD_DOWN = HidNpadButton_Down;
            constexpr u64 DPAD_LEFT = HidNpadButton_Left;
            constexpr u64 DPAD_RIGHT = HidNpadButton_Right;
            constexpr u64 BUTTON_L = HidNpadButton_L;
            constexpr u64 BUTTON_R = HidNpadButton_R;
            constexpr u64 MINUS = HidNpadButton_Minus;
        }

        /// Frames each step is held on screen. At 60fps this is about eight actions a second --
        /// fast enough that a full walkthrough is seconds rather than minutes, slow enough to follow.
        constexpr int FRAMES_PER_STEP = 7;

        // `prepare` runs before the press (to place a cursor), `postCheck` after it (to assert the outcome).

        void prepareSlot0(TrainerView &screen, Trainer::Trainer &) { screen.selectedItemIndex = 0; }
        void prepareParty0(TrainerView &screen, Trainer::Trainer &) { screen.selectedPartyIndex = 0; }

        /// Put the cursor on a slot that holds a real Pokemon, so A opens the details modal rather
        /// than the creator. Species 0 is empty however non-null the pointer is -- several games keep
        /// a live blank in every unoccupied slot.
        /// Park the cursor on an occupied slot, searching EVERY box rather than only the open one.
        ///
        /// The box the journey happens to land on after its L/R navigation is often empty, and the
        /// details sub-journey then degraded to a note -- so on a save whose Pokemon all live in
        /// box 12, the modal was never opened and none of its checks ever ran. Moving the box
        /// cursor is a cursor change, which every step that uses this already allows.
        void prepareOccupied(TrainerView &screen, Trainer::Trainer &trainer)
        {
            const int slotsPerBox = (int)trainer.getSlotsPerBox();
            const int startBoxIndex = (screen.selectedBoxIndex >= 0 && screen.selectedBoxIndex < (int)trainer.boxes.size()) ? screen.selectedBoxIndex : 0;
            for (int searchedBoxCount = 0; searchedBoxCount < (int)trainer.boxes.size(); ++searchedBoxCount)
            {
                const int boxIndex = (startBoxIndex + searchedBoxCount) % (int)trainer.boxes.size();
                for (int slotIndex = 0; slotIndex < slotsPerBox && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                    {
                        continue;
                    }
                    screen.selectedBoxIndex = boxIndex;
                    screen.selectedItemIndex = slotIndex;
                    return;
                }
            }
            screen.selectedItemIndex = 0; // no boxed Pokemon at all: the post-check notes it instead
        }

        bool boxHasAnyone(const TrainerView &screen, const Trainer::Trainer &trainer)
        {
            const int boxIndex = screen.selectedBoxIndex;
            if (boxIndex < 0 || boxIndex >= (int)trainer.boxes.size()) return false;
            const int slotsPerBox = (int)trainer.getSlotsPerBox();
            for (int slotIndex = 0; slotIndex < slotsPerBox && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
            {
                if (!isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                {
                    return true;
                }
            }
            return false;
        }

        void checkHome(Context &context, TrainerView &screen, int wantedHomeIndex, ViewModeEnum wantedMode, const char *modeName)
        {
            context.check(screen.homeMenuIndex == wantedHomeIndex, "the HOME cursor is not on the expected entry", std::string("want=") + modeName + " index=" + std::to_string(screen.homeMenuIndex));
            context.check(screen.selectedMode == wantedMode, "the focused HOME entry selected the wrong view", std::string("want=") + modeName);
        }
        void checkHome0(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 0, ViewModeEnum::Boxes, "Boxes");
        }
        void checkHome1(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 1, ViewModeEnum::Party, "Party");
        }
        void checkHome2(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 2, ViewModeEnum::Storage, "Storage");
        }
        void checkHome3(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 3, ViewModeEnum::Items, "Items");
        }
        void checkHome4(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 4, ViewModeEnum::Trainer, "Trainer");
        }
        void checkHome5(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            checkHome(context, screen, 5, ViewModeEnum::Settings, "Settings");
        }

        void checkEntered(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.detailViewActive, "pressing A on a HOME entry did not open its view");
        }
        void checkLeft(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(!screen.detailViewActive, "a view could be entered but not left with B");
            context.check(!screen.goBack, "leaving a view exited the editor instead of returning to HOME");
        }
        void checkCursorMoved(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.selectedItemIndex != 0, "Right did not move the box cursor", "slot=" + std::to_string(screen.selectedItemIndex));
        }
        void checkDetailsOpen(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            if (!boxHasAnyone(screen, trainer))
            {
                context.skip("the open box is empty, so the details modal could not be opened", "box=" + std::to_string(screen.selectedBoxIndex + 1));
                return;
            }
            context.check(screen.details.active, "A on an occupied slot did not open the details modal");
        }
        /// Species must be an EDITABLE row on the details page, and selecting it must open the
        /// species picker. `leftOrder` is the draw-order list of editable rows the modal publishes,
        /// so this asserts on what the cursor can actually reach rather than on pixels.
        void checkSpeciesRow(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            // empty box -- checkDetailsOpen already noted it
            if (!screen.details.active) return;
            const auto &leftColumnOrder = screen.details.leftOrder;
            context.check(std::find(leftColumnOrder.begin(), leftColumnOrder.end(), 32) != leftColumnOrder.end(), "the details page has no editable Species row", "rows=" + std::to_string(leftColumnOrder.size()));
        }
        void prepareSpeciesRow(TrainerView &screen, Trainer::Trainer &)
        {
            if (screen.details.active)
            {
                screen.details.selectedField = 32;
            }
        }
        void checkSpeciesPicker(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active) return;
            context.check(screen.pickerActive, "A on the Species row did not open a picker");
            if (!screen.pickerActive) return;
            context.check(screen.pickerKind == UI::Dialogs::PickerKind::Species, "A on the Species row opened the wrong picker");
            // The list is dex-filtered per game, so an empty one means every species was rejected
            // -- which would leave the user unable to pick anything at all.
            context.check(!screen.pickerOrder.empty(), "the species picker offered nothing");
        }
        void checkSpeciesPickerClosed(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active) return;
            context.check(!screen.pickerActive, "B did not close the species picker");
        }

        /// The snapshot from before the current step, kept by uiRun; a post check that compares
        /// against the pre-step state reads it.
        UISnapshot g_snapshotBefore;

        // What the pokemon was before the species edit, so the check can tell a change from a no-op
        // and confirm the discard put it back.
        uint16_t g_speciesIdBefore = 0;
        std::vector<uint8_t> g_recordBefore; // the whole record, so the discard can be checked byte for byte
        bool g_unsavedBeforeEdit = false;
        void prepareSpeciesApply(TrainerView &screen, Trainer::Trainer &)
        {
            g_speciesIdBefore = 0;
            g_recordBefore.clear();
            g_unsavedBeforeEdit = screen.hasUnsavedChanges;
            if (!screen.details.active) return;
            if (const Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon())
            {
                g_speciesIdBefore = pokemon->speciesID();
                const auto recordBytes = pokemon->getData();
                g_recordBefore.assign(reinterpret_cast<const uint8_t *>(recordBytes.data()), reinterpret_cast<const uint8_t *>(recordBytes.data()) + recordBytes.size());
            }
        }
        /// Applying a species must leave a COHERENT Pokemon, not just a different id. Four fields
        /// are species-derived and a stale one is a Bad Egg in-game rather than an oddity, so each
        /// is checked against the new species rather than assumed.
        void checkSpeciesApplied(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active || g_speciesIdBefore == 0) return;
            const Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
            if (!context.check(pokemon != nullptr, "the details target vanished during a species edit")) return;

            context.check(pokemon->speciesID() != g_speciesIdBefore, "picking a different species did not change the Pokemon", "species=" + std::to_string(pokemon->speciesID()));
            context.check(pokemon->isStructurallyValid(), "the species edit left the Pokemon with a bad checksum", "species=" + std::to_string(pokemon->speciesID()));

            const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(pokemon->speciesID(), 0);
            context.check(pokemon->form() < personalInfo.formCount || personalInfo.formCount == 0, "the species edit left a form the new species does not have", "species=" + std::to_string(pokemon->speciesID()) + " form=" + std::to_string(pokemon->form()) + " formCount=" + std::to_string(personalInfo.formCount));
            context.check(pokemon->level() >= 1 && pokemon->level() <= 100, "the species edit produced an out-of-range level", "level=" + std::to_string(pokemon->level()));
            if (pokemon->hasAbility())
            {
                const Pokemon::AbilitySlots abilitySlots = Pokemon::getAbilitySlots(pokemon->speciesID(), pokemon->form(), pokemon->getGameGroup());
                bool abilityAllowed = false;
                for (int abilitySlotIndex = 0; abilitySlotIndex < abilitySlots.count; ++abilitySlotIndex)
                {
                    if (abilitySlots.slot[abilitySlotIndex] == pokemon->ability())
                    {
                        abilityAllowed = true;
                    }
                }
                context.check(abilityAllowed, "the species edit left an ability the new species cannot have", "species=" + std::to_string(pokemon->speciesID()) + " ability=" + std::to_string(pokemon->ability()));
            }
        }
        void checkDiscardPrompt(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (g_speciesIdBefore == 0) return;
            context.check(screen.details.discardConfirmActive, "closing the details page after an edit did not ask about the unsaved change");
        }
        /// Discarding must put the ORIGINAL Pokemon back -- a rollback that leaves the edit in
        /// place is worse than no prompt at all, because the user was told it was undone.
        void checkSpeciesDiscarded(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            if (g_speciesIdBefore == 0) return;
            context.check(!screen.details.active, "discarding did not close the details page");
            const int boxIndex = screen.selectedBoxIndex;
            const int slotIndex = screen.selectedItemIndex;
            if (boxIndex < 0 || boxIndex >= (int)trainer.boxes.size() || slotIndex < 0 || slotIndex >= (int)Trainer::BOX_SLOTS) return;
            const Pokemon::Pokemon *pokemon = trainer.boxes[boxIndex][slotIndex].get();
            if (!context.check(pokemon != nullptr, "the edited Pokemon vanished when the edit was discarded")) return;
            context.check(pokemon->speciesID() == g_speciesIdBefore, "discarding a species change did not roll the Pokemon back", "want=" + std::to_string(g_speciesIdBefore) + " got=" + std::to_string(pokemon->speciesID()));
            // The whole record, not just the species: form, ability, stats and the checksum are all
            // derived from the species, and a rollback that restored only the id would leave them.
            const auto recordBytes = pokemon->getData();
            const bool sameBytes = recordBytes.size() == g_recordBefore.size() && std::equal(g_recordBefore.begin(), g_recordBefore.end(), reinterpret_cast<const uint8_t *>(recordBytes.data()));
            context.check(sameBytes, "discarding a species change did not restore the Pokemon byte for byte");
            // The edit was the only change since the page opened, and it has been undone, so the
            // editor must be exactly as dirty as it was before it.
            context.check(screen.hasUnsavedChanges == g_unsavedBeforeEdit, "discarding the only edit left the editor marked as having unsaved changes", std::string("unsavedBefore=") + (g_unsavedBeforeEdit ? "1" : "0") + " unsavedAfter=" + (screen.hasUnsavedChanges ? "1" : "0"));
        }
        void checkDetailsClosed(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(!screen.details.active, "B did not close the details modal");
        }
        void checkPartyDetailsOpen(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.details.active, "A on a party Pokemon did not open the details modal");
        }
        void checkCursorModeIsMenu(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.cursorMode == TrainerView::CursorMode::Menu, "Y did not cycle the storage cursor mode back to Menu");
        }
        /// The item cursor can only move in a pouch with more than one item in it.
        void checkItemCursorMoved(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            const int pouchIndex = screen.selectedCategory;
            int listedItemCount = 0;
            if (pouchIndex >= 0 && pouchIndex < (int)trainer.items.size())
            {
                for (const auto &item : trainer.items[pouchIndex])
                {
                    listedItemCount += item.itemId != 0;
                }
            }
            if (listedItemCount < 2)
            {
                context.skip("the open pouch holds fewer than two items, so the item cursor could not move", "pouch=" + std::to_string(pouchIndex) + " items=" + std::to_string(listedItemCount));
                return;
            }
            context.check(screen.selectedItemIndex != g_snapshotBefore.slot, "Up/Down did not move the item cursor", "pouch=" + std::to_string(pouchIndex) + " slot=" + std::to_string(screen.selectedItemIndex));
        }
        void checkCursorModeCycled(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.cursorMode != TrainerView::CursorMode::Menu, "Y did not cycle the storage cursor mode");
        }
        // ADDING AN ITEM. Minus in the items view offers the legal ids the pouch does not hold; A adds
        // the highlighted one with a count of 1. The bag is put back afterwards (restoreBag) so the
        // save suite still sees the save as it was found -- this suite navigates, it does not edit.
        std::vector<std::vector<Trainer::InventoryItem>> g_bagBefore;
        bool g_unsavedBeforeBag = false;
        int g_addPouchIndex = -1;
        int g_addItemId = -1;
        bool g_addExpected = false;
        void prepareAddItemPicker(TrainerView &screen, Trainer::Trainer &trainer)
        {
            g_bagBefore = trainer.items;
            g_unsavedBeforeBag = screen.hasUnsavedChanges;
            g_addPouchIndex = screen.selectedCategory;
            g_addItemId = -1;
            // Whether the picker SHOULD open: some legal id for this pouch is not held, and either it
            // is present at count 0 or the pouch has room to append one.
            g_addExpected = false;
            if (g_addPouchIndex < 0 || g_addPouchIndex >= (int)trainer.items.size()) return;
            const auto &pouch = trainer.items[g_addPouchIndex];
            const bool canAppend = (int)pouch.size() < screen.currentPouchCapacity();
            for (uint16_t legalItemId : Names::getPouchItems(trainer.getGameGroup(), g_addPouchIndex))
            {
                bool held = false;
                bool revivable = false;
                for (const auto &item : pouch)
                {
                    if (item.itemId == legalItemId)
                    {
                        held = item.count != 0;
                        revivable = item.count == 0;
                    }
                }
                if (!held && (revivable || canAppend))
                {
                    g_addExpected = true;
                    return;
                }
            }
        }
        void checkAddItemPicker(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!g_addExpected)
            {
                context.skip("the open pouch has nothing to add (every legal item held, or full)", "pouch=" + std::to_string(g_addPouchIndex));
                return;
            }
            const bool isPouchPicker = screen.pickerKind == UI::Dialogs::PickerKind::PouchItem || screen.pickerKind == UI::Dialogs::PickerKind::PouchItemG1 || screen.pickerKind == UI::Dialogs::PickerKind::PouchItemG2 || screen.pickerKind == UI::Dialogs::PickerKind::PouchItemG3;
            if (context.check(screen.pickerActive && isPouchPicker, "Minus in the items view did not open the add-item picker", "pouch=" + std::to_string(g_addPouchIndex)) && context.check(!screen.pickerOrder.empty(), "the add-item picker offered nothing"))
            {
                g_addItemId = screen.pickerOrder[screen.pickerSel];
            }
        }
        void checkItemAdded(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            // the picker did not open; checkAddItemPicker already said why
            if (g_addItemId < 0) return;
            int addedItemCount = 0;
            if (g_addPouchIndex >= 0 && g_addPouchIndex < (int)trainer.items.size())
            {
                for (const auto &item : trainer.items[g_addPouchIndex])
                {
                    if (item.itemId == g_addItemId)
                    {
                        addedItemCount = item.count;
                    }
                }
            }
            context.check(addedItemCount == 1, "A in the add-item picker did not add the item with a count of 1", "pouch=" + std::to_string(g_addPouchIndex) + " item=" + std::to_string(g_addItemId) + " count=" + std::to_string(addedItemCount));
            context.check(!screen.pickerActive, "the add-item picker stayed open after adding");
            context.check(screen.hasUnsavedChanges, "adding an item did not mark the editor as having unsaved changes");
        }
        bool addItemPickerOpen(const TrainerView &screen, const Trainer::Trainer &) { return screen.pickerActive; }

        /// Not a user action: puts the bag and the dirty flag back as they were before the add.
        void restoreBag(TrainerView &screen, Trainer::Trainer &trainer)
        {
            if (g_bagBefore.empty()) return;
            trainer.items = g_bagBefore;
            screen.hasUnsavedChanges = g_unsavedBeforeBag;
            screen.pickerActive = false;
            g_bagBefore.clear();
        }

        // CREATING A POKEMON. A on an empty box slot opens the creator's species picker; A there builds
        // a new Pokemon into the slot and opens it in the details page; B asks Keep/Discard and Y
        // discards. The slot and the dirty flag are put back afterwards (restoreCreatorSlot), because
        // several games keep a live blank in every empty slot and the save suite that runs next
        // requires the unedited save back byte for byte.
        int g_creatorBoxIndex = -1;
        int g_creatorSlotIndex = -1;
        bool g_creatorSlotSaved = false;
        std::unique_ptr<Pokemon::Pokemon> g_creatorSlotBefore;
        bool g_unsavedBeforeCreate = false;
        int g_createdSpeciesId = 0;

        /// An empty box slot the creator may fill. A slot holding a party member is locked (Let's Go),
        /// the same test TrainerViewScreen::storageSlotLocked makes.
        bool emptyUnlockedSlot(const Trainer::Trainer &trainer, int &foundBoxIndex, int &foundSlotIndex)
        {
            const int slotsPerBox = (int)trainer.getSlotsPerBox();
            for (int boxIndex = 0; boxIndex < (int)trainer.boxes.size(); ++boxIndex)
            {
                for (int slotIndex = 0; slotIndex < slotsPerBox && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()) && trainer.getPartyPosition(boxIndex, slotIndex) <= 0)
                    {
                        foundBoxIndex = boxIndex;
                        foundSlotIndex = slotIndex;
                        return true;
                    }
                }
            }
            return false;
        }
        bool anyEmptyBoxSlot(const TrainerView &, const Trainer::Trainer &trainer)
        {
            int foundBoxIndex = -1;
            int foundSlotIndex = -1;
            return emptyUnlockedSlot(trainer, foundBoxIndex, foundSlotIndex);
        }
        void prepareEmptySlot(TrainerView &screen, Trainer::Trainer &trainer)
        {
            g_creatorSlotSaved = false;
            g_createdSpeciesId = 0;
            if (!emptyUnlockedSlot(trainer, g_creatorBoxIndex, g_creatorSlotIndex)) return;
            const Pokemon::Pokemon *blankPokemon = trainer.boxes[g_creatorBoxIndex][g_creatorSlotIndex].get();
            g_creatorSlotBefore = blankPokemon ? blankPokemon->clone() : nullptr;
            g_creatorSlotSaved = true;
            g_unsavedBeforeCreate = screen.hasUnsavedChanges;
            screen.selectedBoxIndex = g_creatorBoxIndex;
            screen.selectedItemIndex = g_creatorSlotIndex;
        }
        void checkCreatorPicker(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!context.check(screen.pickerActive && screen.creator.active, "A on an empty box slot did not open the creator")) return;
            context.check(screen.pickerKind == UI::Dialogs::PickerKind::Species, "A on an empty box slot opened the wrong picker");
            if (context.check(!screen.pickerOrder.empty(), "the creator's species picker offered nothing"))
            {
                g_createdSpeciesId = screen.pickerOrder[screen.pickerSel];
            }
        }
        bool creatorPickerOpen(const TrainerView &screen, const Trainer::Trainer &) { return screen.pickerActive && screen.creator.active; }
        /// The fields the creator fills in, each the way the game itself would.
        void checkCreatedFields(Context &context, const Pokemon::Pokemon &created)
        {
            const Enums::GameVersion group = created.getGameGroup();
            const int generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
            const std::string speciesDetail = "species=" + std::to_string(created.speciesID()) + " form=" + std::to_string(created.form());
            // The name the game itself stores for the species: upper case before Gen 5, the record's
            // own language, the game's own punctuation.
            const std::u16string storedName = Pokemon::speciesNameAsStored(created.speciesID(), group, created.language());
            context.check(created.nickname() == storedName, "the creator's default nickname is not the name the game stores for the species", speciesDetail + " nickname=\"" + Utils::utf16ToUtf8(created.nickname()) + "\" want=\"" + Utils::utf16ToUtf8(storedName) + "\"");
            if (created.hasFriendship())
            {
                const uint8_t baseFriendship = Pokemon::getPersonalRecord(group, created.speciesID(), created.form()).baseFriendship;
                context.check(created.friendship() == baseFriendship, "the creator's friendship is not the species' base friendship in this game", speciesDetail + " friendship=" + std::to_string(created.friendship()) + " base=" + std::to_string(baseFriendship));
            }
            if (created.hasMetData() && generation >= 3)
            {
                context.check(created.metLocation() != 0, "the creator left the met location at 0, which is no place in this game", speciesDetail);
            }
            // Ball and met level go last because their setters read other fields (Heart Gold/Soul
            // Silver's second ball byte; the obedience level from Scarlet/Violet on). Written last,
            // writing them again changes nothing.
            if (generation >= 3)
            {
                auto rewritten = created.clone();
                rewritten->setMetLevel(created.metLevel());
                rewritten->setBall(created.ball());
                rewritten->refreshChecksum();
                const std::span<const std::byte> createdBytes = created.getData();
                const std::span<const std::byte> rewrittenBytes = rewritten->getData();
                const bool same = createdBytes.size() == rewrittenBytes.size() && std::memcmp(createdBytes.data(), rewrittenBytes.data(), createdBytes.size()) == 0;
                context.check(same, "the creator set the ball or the met level before a field they depend on", speciesDetail + " ball=" + std::to_string(created.ball()) + " metLevel=" + std::to_string(created.metLevel()));
            }
            if (group == Enums::GameVersion::GG)
            {
                const auto &letsGoPokemon = static_cast<const Pokemon::Pokemon7LGPE &>(created);
                auto resizedPokemon = letsGoPokemon.clone();
                auto &resized = static_cast<Pokemon::Pokemon7LGPE &>(*resizedPokemon);
                resized.resetSizeAbsolutes();
                const float createdHeight = letsGoPokemon.heightAbsolute();
                const float resizedHeight = resized.heightAbsolute();
                const float createdWeight = letsGoPokemon.weightAbsolute();
                const float resizedWeight = resized.weightAbsolute();
                context.check(std::memcmp(&createdHeight, &resizedHeight, sizeof(float)) == 0 && std::memcmp(&createdWeight, &resizedWeight, sizeof(float)) == 0, "the creator's Let's Go height and weight are not the ones its size scalars give", speciesDetail + " height=" + std::to_string(createdHeight) + " want=" + std::to_string(resizedHeight));
                context.check(letsGoPokemon.spirit() == Pokemon::Pokemon7LGPE::INITIAL_SPIRIT_MOOD && letsGoPokemon.mood() == Pokemon::Pokemon7LGPE::INITIAL_SPIRIT_MOOD, "the creator's Let's Go Pokemon does not arrive with full spirit and mood", speciesDetail + " spirit=" + std::to_string(letsGoPokemon.spirit()) + " mood=" + std::to_string(letsGoPokemon.mood()));
                context.check(letsGoPokemon.receivedYear() == created.metYear(), "the creator's Let's Go Pokemon has no received date", speciesDetail + " received=" + std::to_string(letsGoPokemon.receivedYear()) + " met=" + std::to_string(created.metYear()));
            }
        }
        /// A created Pokemon must be COMPLETE, not just present. In Legends: Arceus that includes its
        /// absolute height and weight: the game stores them rather than deriving them, the Pokedex
        /// and the size research tasks read them, and a creator that left them 0 made a Pokemon
        /// with no size at all.
        void checkCreated(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            const Pokemon::Pokemon *created = trainer.boxes[g_creatorBoxIndex][g_creatorSlotIndex].get();
            if (!context.check(!isEmptySlot(created), "picking a species in the creator did not put a Pokemon in the slot", "box=" + std::to_string(g_creatorBoxIndex + 1) + " slot=" + std::to_string(g_creatorSlotIndex + 1))) return;
            context.check(created->speciesID() == g_createdSpeciesId, "the creator made a different species from the one picked", "want=" + std::to_string(g_createdSpeciesId) + " got=" + std::to_string(created->speciesID()));
            context.check(created->isStructurallyValid(), "the created Pokemon has a bad checksum", "species=" + std::to_string(created->speciesID()));
            context.check(screen.details.active && screen.creator.editing, "the creator did not open the new Pokemon in the details page");
            if (created->getGameGroup() == Enums::GameVersion::PLA)
            {
                const auto &createdPokemon = static_cast<const Pokemon::Pokemon8LA &>(*created);
                auto resizedPokemon = createdPokemon.clone();
                auto &resized = static_cast<Pokemon::Pokemon8LA &>(*resizedPokemon);
                resized.resetSizeAbsolutes();
                const float createdHeight = createdPokemon.heightAbsolute();
                const float createdWeight = createdPokemon.weightAbsolute();
                const float resizedHeight = resized.heightAbsolute();
                const float resizedWeight = resized.weightAbsolute();
                const std::string sizeDetail = "species=" + std::to_string(created->speciesID()) + " height=" + std::to_string(createdHeight) + " weight=" + std::to_string(createdWeight);
                context.check(createdHeight > 0 && createdWeight > 0, "the creator made a Legends: Arceus Pokemon with no height or weight", sizeDetail);
                context.check(std::memcmp(&createdHeight, &resizedHeight, sizeof(float)) == 0 && std::memcmp(&createdWeight, &resizedWeight, sizeof(float)) == 0, "the creator's Legends: Arceus height and weight are not the ones its size scalars give", sizeDetail + " wantHeight=" + std::to_string(resizedHeight) + " wantWeight=" + std::to_string(resizedWeight));
            }
            checkCreatedFields(context, *created);
        }
        bool creatorEditing(const TrainerView &screen, const Trainer::Trainer &) { return screen.details.active && screen.creator.editing; }
        void checkKeepPrompt(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.creator.keepConfirmActive, "closing a newly created Pokemon did not ask whether to keep it");
        }
        bool keepPromptOpen(const TrainerView &screen, const Trainer::Trainer &) { return screen.creator.keepConfirmActive; }
        void checkCreationDiscarded(Context &context, TrainerView &screen, Trainer::Trainer &trainer)
        {
            context.check(!screen.details.active && !screen.creator.keepConfirmActive, "discarding a newly created Pokemon did not close the details page");
            context.check(isEmptySlot(trainer.boxes[g_creatorBoxIndex][g_creatorSlotIndex].get()), "discarding a newly created Pokemon left it in the slot", "box=" + std::to_string(g_creatorBoxIndex + 1) + " slot=" + std::to_string(g_creatorSlotIndex + 1));
        }
        /// Not a user action: puts the slot's original contents and the dirty flag back.
        void restoreCreatorSlot(TrainerView &screen, Trainer::Trainer &trainer)
        {
            if (!g_creatorSlotSaved) return;
            screen.details.active = false;
            screen.creator.active = false;
            screen.creator.editing = false;
            screen.creator.keepConfirmActive = false;
            screen.pickerActive = false;
            trainer.boxes[g_creatorBoxIndex][g_creatorSlotIndex] = std::move(g_creatorSlotBefore);
            screen.hasUnsavedChanges = g_unsavedBeforeCreate;
            g_creatorSlotSaved = false;
        }

        /// Both answers are checked: with unsaved changes B must ask before leaving, without them it
        /// must leave. (Whether the editor SHOULD still be dirty here is checkSpeciesDiscarded's check.)
        void checkExit(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (screen.hasUnsavedChanges)
            {
                context.check(screen.saveConfirmActive && !screen.goBack, "B on the HOME menu with unsaved changes did not ask to save first");
            }
            else
            {
                context.check(screen.goBack, "B on the HOME menu did not leave the editor");
            }
        }

        /// True when the save holds at least one boxed Pokemon anywhere.
        ///
        /// The details half of the box journey cannot run without one, and running it anyway does
        /// not merely fail -- it CORRUPTS THE RUN. A on an empty box slot opens the CREATOR, so the
        /// next A applies a species and writes a brand-new Pokemon into the box; the editor is then
        /// dirty, every later B raises the save prompt instead of going back, and each of the
        /// remaining journeys reports its own navigation as broken. One empty save produced 42
        /// failures that way, none of which was about the thing being tested.
        bool anyBoxedPokemon(const TrainerView &, const Trainer::Trainer &trainer)
        {
            const int slotsPerBox = (int)trainer.getSlotsPerBox();
            for (int boxIndex = 0; boxIndex < (int)trainer.boxes.size(); ++boxIndex)
            {
                for (int slotIndex = 0; slotIndex < slotsPerBox && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (!isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /// True when the party holds at least one Pokemon.
        ///
        /// Same failure as the boxes and a different blast radius: with an empty party, A opens no
        /// details page, so the B meant to CLOSE it leaves the party view instead -- and the next B,
        /// meant to leave the view, exits the editor. Every journey after that is one level out.
        bool anyPartyPokemon(const TrainerView &, const Trainer::Trainer &trainer)
        {
            for (const auto &pokemon : trainer.party)
            {
                if (!isEmptySlot(pokemon.get()))
                {
                    return true;
                }
            }
            return false;
        }

        struct Step
        {
            const char *journey;
            const char *what;
            u64 button;
            int presses;
            uint32_t allow;
            void (*prepare)(TrainerView &, Trainer::Trainer &);
            void (*postCheck)(Context &, TrainerView &, Trainer::Trainer &);
            /// The snapshot field the action exists to change ("slot", "box", "detailsModal" ...), or
            /// null when a post check covers it or the step cannot promise a change. The allow mask
            /// says what may move; this says what must, so a button that does nothing fails.
            const char *mustChange = nullptr;
            /// Optional precondition. When it answers false the step is SKIPPED rather than pressed:
            /// a button the screen is not in a state to receive does not test anything, and here it
            /// actively derails what follows. The skip is noted once per journey, loudly, because a
            /// step that silently did not run is the coverage gap this suite exists to expose.
            bool (*needs)(const TrainerView &, const Trainer::Trainer &) = nullptr;
            /// What the skip says when `needs` answers false; null means the Pokemon-to-open message.
            const char *skippedBecause = nullptr;
        };

        constexpr uint32_t ALLOW_NAVIGATION = Allow::AllowHome | Allow::AllowView;
        constexpr uint32_t ALLOW_CURSOR = Allow::AllowCursor;
        constexpr uint32_t ALLOW_MODAL = Allow::AllowModal | Allow::AllowCursor | Allow::AllowView;
        constexpr uint32_t ALLOW_STORAGE = Allow::AllowStorage | Allow::AllowCursor;

        const Step JOURNEY_SCRIPT[] = {
            // the HOME menu, every destination and both row wraps
            {"home", "open the editor", Button::NONE, 1, ALLOW_NAVIGATION, nullptr, nullptr},
            {"home", "focus Pokemon (Boxes)", Button::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, checkHome0},
            {"home", "focus Party", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, checkHome1},
            {"home", "focus Storage", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, checkHome2},
            {"home", "focus Items", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, checkHome3},
            {"home", "focus Trainer", Button::DPAD_RIGHT, 1, ALLOW_NAVIGATION, nullptr, checkHome4},
            {"home", "focus Settings", Button::DPAD_RIGHT, 1, ALLOW_NAVIGATION, nullptr, checkHome5},
            {"home", "wrap right, back to Items", Button::DPAD_RIGHT, 1, ALLOW_NAVIGATION, nullptr, checkHome3},
            {"home", "wrap left, back to Settings", Button::DPAD_LEFT, 1, ALLOW_NAVIGATION, nullptr, checkHome5},

            {"boxes", "return to the pill column", Button::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, checkHome0},
            {"boxes", "open the box view", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, checkEntered},
            {"boxes", "move the cursor right", Button::DPAD_RIGHT, 1, ALLOW_CURSOR, prepareSlot0, checkCursorMoved},
            {"boxes", "move the cursor down", Button::DPAD_DOWN, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "move the cursor left", Button::DPAD_LEFT, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "move the cursor up", Button::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "next box with R", Button::BUTTON_R, 1, ALLOW_CURSOR, nullptr, nullptr, "box"},
            {"boxes", "previous box with L", Button::BUTTON_L, 1, ALLOW_CURSOR, nullptr, nullptr, "box"},
            // Everything from here to the end of the journey needs a Pokemon to open. On a save with
            // none, A opens the creator instead and the step after it would CREATE one -- see
            // anyBoxedPokemon().
            {"boxes", "open the details modal", Button::BUTTON_A, 1, ALLOW_MODAL, prepareOccupied, checkDetailsOpen, nullptr,
             anyBoxedPokemon},
            {"boxes", "scroll the details modal", Button::DPAD_DOWN, 1, ALLOW_MODAL, nullptr, checkSpeciesRow, nullptr,
             anyBoxedPokemon},
            {"boxes", "open the species picker", Button::BUTTON_A, 1, ALLOW_MODAL, prepareSpeciesRow, checkSpeciesPicker,
             nullptr, anyBoxedPokemon},
            {"boxes", "close the species picker", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkSpeciesPickerClosed,
             nullptr, anyBoxedPokemon},
            // Closing an UNEDITED page must not prompt -- checked before the destructive half
            // below, which necessarily does.
            {"boxes", "close the details modal", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkDetailsClosed, nullptr,
             anyBoxedPokemon},

            // Editing the species is DESTRUCTIVE, so it goes last in this journey and is undone
            // immediately: B raises the unsaved-changes prompt and Y discards, which rolls the
            // Pokemon back to the snapshot taken when the page opened. Nothing reaches the save.
            {"boxes", "reopen the details modal", Button::BUTTON_A, 1, ALLOW_MODAL, prepareOccupied, checkDetailsOpen,
             nullptr, anyBoxedPokemon},
            {"boxes", "reopen the species picker", Button::BUTTON_A, 1, ALLOW_MODAL, prepareSpeciesRow, checkSpeciesPicker,
             nullptr, anyBoxedPokemon},
            {"boxes", "move to another species", Button::DPAD_DOWN, 1, ALLOW_MODAL, nullptr, nullptr, nullptr,
             anyBoxedPokemon},
            {"boxes", "apply the species change", Button::BUTTON_A, 1, ALLOW_MODAL | Allow::AllowData, prepareSpeciesApply,
             checkSpeciesApplied, nullptr, anyBoxedPokemon},
            {"boxes", "ask to close after editing", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkDiscardPrompt, nullptr,
             anyBoxedPokemon},
            {"boxes", "discard the species change", Button::BUTTON_Y, 1, ALLOW_MODAL | Allow::AllowData, nullptr,
             checkSpeciesDiscarded, nullptr, anyBoxedPokemon},
            // The creator, undone like the species edit: Y discards the new Pokemon, then the test
            // puts the slot back exactly as it was (see restoreCreatorSlot).
            {"creator", "open the creator on an empty slot", Button::BUTTON_A, 1, ALLOW_MODAL, prepareEmptySlot,
             checkCreatorPicker, nullptr, anyEmptyBoxSlot, "this save has no empty box slot, so the creator was not walked"},
            {"creator", "create the first species offered", Button::BUTTON_A, 1, ALLOW_MODAL | Allow::AllowBoxes | Allow::AllowUnsaved,
             nullptr, checkCreated, nullptr, creatorPickerOpen, "the creator did not open, so no Pokemon was created"},
            {"creator", "ask to close the new Pokemon", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkKeepPrompt, nullptr,
             creatorEditing, "the creator did not open a new Pokemon, so there was nothing to close"},
            {"creator", "discard the new Pokemon", Button::BUTTON_Y, 1, ALLOW_MODAL | Allow::AllowBoxes | Allow::AllowUnsaved,
             nullptr, checkCreationDiscarded, nullptr, keepPromptOpen, "the keep prompt did not open, so nothing was discarded"},
            {"creator", "(test) put the empty slot back", Button::NONE, 1, ALLOW_MODAL | Allow::AllowBoxes | Allow::AllowUnsaved,
             restoreCreatorSlot, nullptr},
            {"boxes", "leave the box view", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkLeft},

            {"party", "focus Party", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, checkHome1},
            {"party", "open the party view", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, checkEntered},
            // A one-member party has nowhere to move, so these two promise nothing; the details
            // steps below need a member and are gated on one.
            {"party", "move down the party", Button::DPAD_DOWN, 2, ALLOW_CURSOR, nullptr, nullptr},
            {"party", "move up the party", Button::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr},
            {"party", "open the details modal", Button::BUTTON_A, 1, ALLOW_MODAL, prepareParty0, checkPartyDetailsOpen,
             "detailsModal", anyPartyPokemon},
            {"party", "close the details modal", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkDetailsClosed,
             "detailsModal", anyPartyPokemon},
            {"party", "leave the party view", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkLeft},

            // Items (no A: the quantity keypad is swkbd and would block the run)
            {"items", "focus Items", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, nullptr, "homeIndex"},
            {"items", "step into the icon row", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, checkHome3},
            {"items", "open the items view", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, checkEntered},
            {"items", "move down the item list", Button::DPAD_DOWN, 3, ALLOW_CURSOR, nullptr, checkItemCursorMoved},
            {"items", "move up the item list", Button::DPAD_UP, 1, ALLOW_CURSOR, nullptr, checkItemCursorMoved},
            {"items", "next pouch with R", Button::BUTTON_R, 1, ALLOW_CURSOR, nullptr, nullptr, "category"},
            {"items", "previous pouch with L", Button::BUTTON_L, 1, ALLOW_CURSOR, nullptr, nullptr, "category"},
            {"items", "open the add-item picker", Button::MINUS, 1, ALLOW_MODAL, prepareAddItemPicker, checkAddItemPicker},
            // Gated: with the picker shut, A would open the quantity keypad instead.
            {"items", "add the highlighted item", Button::BUTTON_A, 1, ALLOW_MODAL | Allow::AllowItems | Allow::AllowUnsaved,
             nullptr, checkItemAdded, nullptr, addItemPickerOpen, "the add-item picker did not open, so no item was added"},
            // Puts the bag back (see restoreBag); a frame with no input, declared to change only that.
            {"items", "(test) put the bag back", Button::NONE, 1, Allow::AllowItems | Allow::AllowUnsaved | ALLOW_MODAL,
             restoreBag, nullptr},
            {"items", "leave the items view", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkLeft},

            // Storage (the bank pane; pointed at the run's scratch bank). L/R change the focused
            // pane's box; the D-pad crosses between the panes at their facing edges.
            {"storage", "return to the pill column", Button::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, checkHome0},
            {"storage", "focus Storage", Button::DPAD_DOWN, 2, ALLOW_NAVIGATION, nullptr, checkHome2},
            {"storage", "open the storage view", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_STORAGE, nullptr,
             checkEntered},
            {"storage", "move the cursor right", Button::DPAD_RIGHT, 1, ALLOW_STORAGE, nullptr, nullptr, "stSaveSlot"},
            {"storage", "move the cursor down", Button::DPAD_DOWN, 1, ALLOW_STORAGE, nullptr, nullptr, "stSaveSlot"},
            {"storage", "next box with R", Button::BUTTON_R, 1, ALLOW_STORAGE, nullptr, nullptr, "box"},
            {"storage", "previous box with L", Button::BUTTON_L, 1, ALLOW_STORAGE, nullptr, nullptr, "box"},
            // Ten presses reach the save pane's right edge from anywhere in a row and cross it; the
            // bank pane's own right edge goes nowhere, so the extra presses are harmless.
            {"storage", "cross to the bank pane", Button::DPAD_RIGHT, 10, ALLOW_STORAGE, nullptr, nullptr, "focusPane"},
            {"storage", "cross back to the save pane", Button::DPAD_LEFT, 10, ALLOW_STORAGE, nullptr, nullptr, "focusPane"},
            {"storage", "cycle the cursor mode", Button::BUTTON_Y, 1, ALLOW_STORAGE | Allow::AllowSwap, nullptr,
             checkCursorModeCycled},
            {"storage", "cycle back to Menu mode", Button::BUTTON_Y, 2, ALLOW_STORAGE | Allow::AllowSwap, nullptr,
             checkCursorModeIsMenu},
            {"storage", "leave the storage view", Button::BUTTON_B, 1, ALLOW_MODAL | ALLOW_STORAGE, nullptr, checkLeft},

            // Trainer card (no A: rows 0 and 1 open the keyboard / keypad)
            {"trainer", "focus Trainer", Button::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, nullptr, "homeIndex"},
            {"trainer", "step along to Trainer", Button::DPAD_RIGHT, 1, ALLOW_NAVIGATION, nullptr, checkHome4},
            {"trainer", "open the trainer card", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, checkEntered},
            {"trainer", "move down the rows", Button::DPAD_DOWN, 1, ALLOW_CURSOR, nullptr, nullptr, "trainerRow"},
            {"trainer", "move up the rows", Button::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "trainerRow"},
            {"trainer", "leave the trainer card", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkLeft},

            {"settings", "focus Settings", Button::DPAD_RIGHT, 1, ALLOW_NAVIGATION, nullptr, checkHome5},
            {"settings", "open settings", Button::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, checkEntered},
            {"settings", "move down the rows", Button::DPAD_DOWN, 2, ALLOW_CURSOR, nullptr, nullptr, "settingsRow"},
            {"settings", "move up the rows", Button::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "settingsRow"},
            {"settings", "leave settings", Button::BUTTON_B, 1, ALLOW_MODAL, nullptr, checkLeft},

            {"exit", "press B on the HOME menu", Button::BUTTON_B, 1,
             Allow::AllowExit | Allow::AllowModal | Allow::AllowView, nullptr, checkExit},
        };
        constexpr int JOURNEY_STEP_COUNT = (int)(sizeof(JOURNEY_SCRIPT) / sizeof(JOURNEY_SCRIPT[0]));

        std::unique_ptr<TrainerView> g_screen;
        bool g_hasSnapshot = false;
        /// The journey whose "nothing to open" skip has already been noted for THIS target.
        const char *g_skipNotedJourney = nullptr;
        /// Keyboard prompts refused so far, so a step that raises a NEW one can be named.
        int g_keyboardPromptsSeen = 0;
        std::string g_previousBankRoot;
        bool g_bankOverridden = false;
        /// The save's current box before the walk. The editor copies the box on screen into the
        /// trainer every frame, the walk moves to a box with a Pokemon in it, and the save suite
        /// that runs next writes this same trainer and requires an unedited save byte for byte.
        uint8_t g_currentBoxBefore = 0;

        void teardown()
        {
            Runner::setDrivenScreen(nullptr);
            g_screen.reset();
            g_hasSnapshot = false;
            g_skipNotedJourney = nullptr;
            if (g_bankOverridden)
            {
                Trainer::Bank::setRootOverride(g_previousBankRoot);
                g_bankOverridden = false;
            }
        }
    }

    int uiPlan(Context &context)
    {
        teardown();
        if (!context.trainer) return 0;
        // Chunk 0 builds the screen, chunks 1..JOURNEY_STEP_COUNT*FRAMES_PER_STEP run the script, and
        // ONE MORE is needed to reach `stepIndex >= JOURNEY_STEP_COUNT` and tear down. Without it the
        // driven screen and its reference to the trainer outlive the target -- and the runner frees the
        // previous target's trainer before loading the next, so the very next frame draws a
        // TrainerViewScreen whose `trainer&` has been freed. That is a console-only crash: the host
        // harness never draws, so it never dereferences it.
        return JOURNEY_STEP_COUNT * FRAMES_PER_STEP + 2;
    }

    void uiRun(Context &context, int chunk)
    {
        if (!context.trainer) return;
        // One chunk per frame, always: the walkthrough is meant to be watched, and without this the
        // budget loop would run the whole thing between two frames (see the file header).
        Runner::yieldFrame();

        Trainer::Trainer &trainer = *context.trainer;

        if (chunk == 0)
        {
            // Point the bank at the run's scratch directory BEFORE the screen is built: its
            // constructor opens one, and the user's real bank is not this test's business.
            g_previousBankRoot.clear();
            g_currentBoxBefore = trainer.getCurrentBox();
            g_skipNotedJourney = nullptr; // a fresh target gets to report its own coverage gaps
            g_keyboardPromptsSeen = Utils::keyboardPromptsSuppressed();
            Trainer::Bank::setRootOverride(context.workDirectory.empty() ? std::string("sdmc:/PKSE/tests/work") : context.workDirectory);
            g_bankOverridden = true;

            g_screen = std::make_unique<TrainerView>(trainer, context.target, context.workDirectory, context.titleId, context.accountUid, false);
            if (!context.check(g_screen != nullptr, "the editor screen could not be built")) return;

            // Publish it: from here TestRunnerScreen draws THIS instead of its own progress view.
            Runner::setDrivenScreen(g_screen.get());
            g_snapshotBefore = UISnapshot::take(*g_screen, trainer);
            g_hasSnapshot = true;

            const std::string banner = "[ui] " + context.target + " -- walking the editor (" + std::to_string(JOURNEY_STEP_COUNT) + " steps)";
            if (context.report)
            {
                context.report->addTrace(banner);
            }
            Utils::logTest(banner);
            context.check(!g_screen->detailViewActive, "the editor opened inside a view instead of on the HOME menu");
            return;
        }

        if (!g_screen) return;
        TrainerView &screen = *g_screen;

        // Chunk 0 built the screen, so the script starts at chunk 1 -- without the offset the
        // first step would share chunk 0 with the build and never run.
        const int stepIndex = (chunk - 1) / FRAMES_PER_STEP;
        if (stepIndex >= JOURNEY_STEP_COUNT)
        {
            // Restored here and not in teardown(), which also runs once the trainer has been freed.
            teardown();
            trainer.setCurrentBox(g_currentBoxBefore);
            return;
        }

        Journey journey(context, screen, &screen, &trainer, JOURNEY_SCRIPT[stepIndex].journey);
        if (g_hasSnapshot)
        {
            journey.restore(g_snapshotBefore);
        }

        if ((chunk - 1) % FRAMES_PER_STEP != 0)
        {
            // A holding frame: pump the screen so its animations and timers keep running, but do
            // not snapshot, verify or trace. These frames exist to make the previous step readable
            // on screen; checking them would re-hash every box in the save sixty times a second and
            // bury the trace under "(hold) -> changed: nothing".
            journey.idle();
            return;
        }

        const Step &step = JOURNEY_SCRIPT[stepIndex];
        if (step.needs && !step.needs(screen, trainer))
        {
            // Note ONCE per journey. Every gated step in a journey shares one precondition, so a
            // note per step would print the same sentence a dozen times and bury it. Reset per
            // target in chunk 0 -- the journey names are the same string literals for every save,
            // so comparing them alone would report the first target's gap and stay silent for the
            // rest, which is the opposite of what a coverage note is for.
            if (g_skipNotedJourney != step.journey)
            {
                g_skipNotedJourney = step.journey;
                context.skip(step.skippedBecause ? step.skippedBecause : "this save has no Pokemon to open, so the details page was not walked", std::string("journey=") + step.journey);
            }
            journey.idle(); // keep pumping the screen; press nothing
            return;
        }
        if (step.prepare)
        {
            step.prepare(screen, trainer);
        }

        if (step.button == Button::NONE)
        {
            journey.settle(step.what, step.allow, 2);
        }
        else
        {
            journey.act(step.what, step.button, step.allow, step.presses);
        }

        if (step.postCheck)
        {
            step.postCheck(context, screen, trainer);
        }
        if (step.mustChange)
        {
            context.check(journey.lastChangedField(step.mustChange), "a UI action did not do what it is for", std::string("journey=") + step.journey + " action=\"" + step.what + "\" expected=" + step.mustChange + " changed=" + journey.lastChanged());
        }

        // A journey that reached a keyboard did something it did not intend. The run cannot hang on
        // one -- Runner::start refuses the applet outright -- but a refusal is still a step landing
        // somewhere the script does not think it is, which is exactly what a derailed journey looks
        // like from the inside. Report it rather than let the suppression hide it.
        {
            const int suppressedPromptCount = Utils::keyboardPromptsSuppressed();
            if (suppressedPromptCount != g_keyboardPromptsSeen)
            {
                g_keyboardPromptsSeen = suppressedPromptCount;
                context.check(false, "a UI action tried to open the keyboard, which would block an automated run", std::string("journey=") + step.journey + " action=\"" + step.what + "\"");
            }
        }

        g_snapshotBefore = journey.snapshot();

        Runner::DriveCaption caption;
        caption.target = context.target;
        caption.journey = step.journey;
        caption.action = step.what;
        caption.changed = journey.lastChanged();
        caption.changedSomethingUndeclared = journey.lastWasBad();
        caption.step = stepIndex + 1;
        caption.stepCount = JOURNEY_STEP_COUNT;
        Runner::setDriveCaption(caption);
    }
}
