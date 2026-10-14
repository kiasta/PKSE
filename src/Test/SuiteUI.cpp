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

#include <memory>
#include <string>

#include "Trainer/Bank.h"
#include "Trainer/Trainer.h"
#include "UI/TrainerViewScreen.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h"
#include "Utils/Logger.h"

namespace Test
{

    namespace
    {
        using ViewModeEnum = UI::TrainerViewScreen::ViewMode;
        using TrainerView = UI::TrainerViewScreen;

        namespace Btn
        {
            constexpr u64 None = 0;
            constexpr u64 BUTTON_A = HidNpadButton_A, BUTTON_B = HidNpadButton_B;
            constexpr u64 BUTTON_Y = HidNpadButton_Y;
            constexpr u64 DPAD_UP = HidNpadButton_Up, DPAD_DOWN = HidNpadButton_Down;
            constexpr u64 Left = HidNpadButton_Left, Right = HidNpadButton_Right;
            constexpr u64 BUTTON_L = HidNpadButton_L, BUTTON_R = HidNpadButton_R;
            constexpr u64 MINUS = HidNpadButton_Minus;
        }

        /// Frames each step is held on screen. At 60fps this is about eight actions a second --
        /// fast enough that a full walkthrough is seconds rather than minutes, slow enough to follow.
        constexpr int FRAMES_PER_STEP = 7;

        // `prep` runs before the press (to place a cursor), `post` after it (to assert the outcome).

        void prepSlot0(TrainerView &screen, Trainer::Trainer &) { screen.selectedItemIndex = 0; }
        void prepParty0(TrainerView &screen, Trainer::Trainer &) { screen.selectedPartyIndex = 0; }

        /// Put the cursor on a slot that holds a real Pokemon, so A opens the details modal rather
        /// than the creator. Species 0 is empty however non-null the pointer is -- several games keep
        /// a live blank in every unoccupied slot.
        /// Park the cursor on an occupied slot, searching EVERY box rather than only the open one.
        ///
        /// The box the journey happens to land on after its L/R navigation is often empty, and the
        /// details sub-journey then degraded to a note -- so on a save whose Pokemon all live in
        /// box 12, the modal was never opened and none of its checks ever ran. Moving the box
        /// cursor is a cursor change, which every step that uses this already allows.
        void prepOccupied(TrainerView &screen, Trainer::Trainer &tr)
        {
            const int slots = (int)tr.getSlotsPerBox();
            const int start = (screen.selectedBoxIndex >= 0 && screen.selectedBoxIndex < (int)tr.boxes.size())
                                  ? screen.selectedBoxIndex
                                  : 0;
            for (int boxIndex = 0; boxIndex < (int)tr.boxes.size(); ++boxIndex)
            {
                const int box = (start + boxIndex) % (int)tr.boxes.size();
                for (int slotIndex = 0; slotIndex < slots && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (isEmptySlot(tr.boxes[box][slotIndex].get()))
                        continue;
                    screen.selectedBoxIndex = box;
                    screen.selectedItemIndex = slotIndex;
                    return;
                }
            }
            screen.selectedItemIndex = 0; // no boxed Pokemon at all: the post-check notes it instead
        }

        bool boxHasAnyone(const TrainerView &screen, const Trainer::Trainer &tr)
        {
            const int box = screen.selectedBoxIndex;
            if (box < 0 || box >= (int)tr.boxes.size())
                return false;
            const int slots = (int)tr.getSlotsPerBox();
            for (int slotIndex = 0; slotIndex < slots && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                if (!isEmptySlot(tr.boxes[box][slotIndex].get()))
                    return true;
            return false;
        }

        void ckHome(Context &context, TrainerView &text, int want, ViewModeEnum mode, const char *name)
        {
            context.check(text.homeMenuIndex == want, "the HOME cursor is not on the expected entry",
                      std::string("want=") + name + " index=" + std::to_string(text.homeMenuIndex));
            context.check(text.selectedMode == mode, "the focused HOME entry selected the wrong view",
                      std::string("want=") + name);
        }
        void ckHome0(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 0, ViewModeEnum::Boxes, "Boxes");
        }
        void ckHome1(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 1, ViewModeEnum::Party, "Party");
        }
        void ckHome2(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 2, ViewModeEnum::Storage, "Storage");
        }
        void ckHome3(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 3, ViewModeEnum::Items, "Items");
        }
        void ckHome4(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 4, ViewModeEnum::Trainer, "Trainer");
        }
        void ckHome5(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            ckHome(context, screen, 5, ViewModeEnum::Settings, "Settings");
        }

        void ckEntered(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.detailViewActive, "pressing A on a HOME entry did not open its view");
        }
        void ckLeft(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(!screen.detailViewActive, "a view could be entered but not left with B");
            context.check(!screen.goBack, "leaving a view exited the editor instead of returning to HOME");
        }
        void ckCursorMoved(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.selectedItemIndex != 0, "Right did not move the box cursor",
                    "slot=" + std::to_string(screen.selectedItemIndex));
        }
        void ckDetailsOpen(Context &context, TrainerView &screen, Trainer::Trainer &tr)
        {
            if (!boxHasAnyone(screen, tr))
            {
                context.skip("the open box is empty, so the details modal could not be opened",
                       "box=" + std::to_string(screen.selectedBoxIndex + 1));
                return;
            }
            context.check(screen.details.active, "A on an occupied slot did not open the details modal");
        }
        /// Species must be an EDITABLE row on the details page, and selecting it must open the
        /// species picker. `leftOrder` is the draw-order list of editable rows the modal publishes,
        /// so this asserts on what the cursor can actually reach rather than on pixels.
        void ckSpeciesRow(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            // empty box -- ckDetailsOpen already noted it
            if (!screen.details.active) return;
            const auto &leftColumnOrder = screen.details.leftOrder;
            context.check(std::find(leftColumnOrder.begin(), leftColumnOrder.end(), 32) != leftColumnOrder.end(),
                    "the details page has no editable Species row",
                    "rows=" + std::to_string(leftColumnOrder.size()));
        }
        void prepSpeciesRow(TrainerView &screen, Trainer::Trainer &)
        {
            if (screen.details.active)
                screen.details.selectedField = 32;
        }
        void ckSpeciesPicker(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active)
                return;
            context.check(screen.pickerActive, "A on the Species row did not open a picker");
            if (!screen.pickerActive)
                return;
            context.check(screen.pickerKind == UI::Dialogs::PickerKind::Species,
                    "A on the Species row opened the wrong picker");
            // The list is dex-filtered per game, so an empty one means every species was rejected
            // -- which would leave the user unable to pick anything at all.
            context.check(!screen.pickerOrder.empty(), "the species picker offered nothing");
        }
        void ckSpeciesPickerClosed(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active)
                return;
            context.check(!screen.pickerActive, "B did not close the species picker");
        }

        /// The snapshot from before the current step, kept by uiRun; a post check that compares
        /// against the pre-step state reads it.
        UISnapshot gSnap;

        // What the pokemon was before the species edit, so the check can tell a change from a no-op
        // and confirm the discard put it back.
        uint16_t gSpeciesBefore = 0;
        std::vector<uint8_t> gRecordBefore; // the whole record, so the discard can be checked byte for byte
        bool gUnsavedBeforeEdit = false;
        void prepSpeciesApply(TrainerView &screen, Trainer::Trainer &)
        {
            gSpeciesBefore = 0;
            gRecordBefore.clear();
            gUnsavedBeforeEdit = screen.hasUnsavedChanges;
            if (!screen.details.active)
                return;
            if (const Pokemon::Pokemon *p = screen.detailsTargetPokemon())
            {
                gSpeciesBefore = p->speciesID();
                const auto recordBytes = p->getData();
                gRecordBefore.assign(reinterpret_cast<const uint8_t *>(recordBytes.data()),
                                     reinterpret_cast<const uint8_t *>(recordBytes.data()) + recordBytes.size());
            }
        }
        /// Applying a species must leave a COHERENT Pokemon, not just a different id. Four fields
        /// are species-derived and a stale one is a Bad Egg in-game rather than an oddity, so each
        /// is checked against the new species rather than assumed.
        void ckSpeciesApplied(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!screen.details.active || gSpeciesBefore == 0)
                return;
            const Pokemon::Pokemon *p = screen.detailsTargetPokemon();
            if (!context.check(p != nullptr, "the details target vanished during a species edit"))
                return;

            context.check(p->speciesID() != gSpeciesBefore,
                    "picking a different species did not change the Pokemon",
                    "species=" + std::to_string(p->speciesID()));
            context.check(p->isStructurallyValid(),
                    "the species edit left the Pokemon with a bad checksum",
                    "species=" + std::to_string(p->speciesID()));

            const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(p->speciesID(), 0);
            context.check(p->form() < pi.formCount || pi.formCount == 0,
                    "the species edit left a form the new species does not have",
                    "species=" + std::to_string(p->speciesID()) +
                        " form=" + std::to_string(p->form()) +
                        " formCount=" + std::to_string(pi.formCount));
            context.check(p->level() >= 1 && p->level() <= 100,
                    "the species edit produced an out-of-range level",
                    "level=" + std::to_string(p->level()));
            if (p->hasAbility())
            {
                const Pokemon::AbilitySlots slots =
                    Pokemon::getAbilitySlots(p->speciesID(), p->form(), p->getGameGroup());
                bool passed = false;
                for (int slotIndex = 0; slotIndex < slots.count; ++slotIndex)
                    if (slots.slot[slotIndex] == p->ability())
                        passed = true;
                context.check(passed, "the species edit left an ability the new species cannot have",
                        "species=" + std::to_string(p->speciesID()) +
                            " ability=" + std::to_string(p->ability()));
            }
        }
        void ckDiscardPrompt(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (gSpeciesBefore == 0)
                return;
            context.check(screen.details.discardConfirmActive,
                    "closing the details page after an edit did not ask about the unsaved change");
        }
        /// Discarding must put the ORIGINAL Pokemon back -- a rollback that leaves the edit in
        /// place is worse than no prompt at all, because the user was told it was undone.
        void ckSpeciesDiscarded(Context &context, TrainerView &screen, Trainer::Trainer &tr)
        {
            if (gSpeciesBefore == 0)
                return;
            context.check(!screen.details.active, "discarding did not close the details page");
            const int box = screen.selectedBoxIndex, slot = screen.selectedItemIndex;
            if (box < 0 || box >= (int)tr.boxes.size() ||
                slot < 0 || slot >= (int)Trainer::BOX_SLOTS)
                return;
            const Pokemon::Pokemon *p = tr.boxes[box][slot].get();
            if (!context.check(p != nullptr, "the edited Pokemon vanished when the edit was discarded"))
                return;
            context.check(p->speciesID() == gSpeciesBefore,
                    "discarding a species change did not roll the Pokemon back",
                    "want=" + std::to_string(gSpeciesBefore) +
                        " got=" + std::to_string(p->speciesID()));
            // The whole record, not just the species: form, ability, stats and the checksum are all
            // derived from the species, and a rollback that restored only the id would leave them.
            const auto recordBytes = p->getData();
            const bool sameBytes =
                recordBytes.size() == gRecordBefore.size() &&
                std::equal(gRecordBefore.begin(), gRecordBefore.end(), reinterpret_cast<const uint8_t *>(recordBytes.data()));
            context.check(sameBytes, "discarding a species change did not restore the Pokemon byte for byte");
            // The edit was the only change since the page opened, and it has been undone, so the
            // editor must be exactly as dirty as it was before it.
            context.check(screen.hasUnsavedChanges == gUnsavedBeforeEdit,
                    "discarding the only edit left the editor marked as having unsaved changes",
                    std::string("unsavedBefore=") + (gUnsavedBeforeEdit ? "1" : "0") +
                        " unsavedAfter=" + (screen.hasUnsavedChanges ? "1" : "0"));
        }
        void ckDetailsClosed(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(!screen.details.active, "B did not close the details modal");
        }
        void ckPartyDetailsOpen(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.details.active, "A on a party Pokemon did not open the details modal");
        }
        void ckCursorModeIsMenu(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.cursorMode == TrainerView::CursorMode::Menu,
                    "Y did not cycle the storage cursor mode back to Menu");
        }
        /// The item cursor can only move in a pouch with more than one item in it.
        void ckItemCursorMoved(Context &context, TrainerView &screen, Trainer::Trainer &tr)
        {
            const int pouch = screen.selectedCategory;
            int listed = 0;
            if (pouch >= 0 && pouch < (int)tr.items.size())
                for (const auto &item : tr.items[pouch])
                    listed += item.itemId != 0;
            if (listed < 2)
            {
                context.skip("the open pouch holds fewer than two items, so the item cursor could not move",
                             "pouch=" + std::to_string(pouch) + " items=" + std::to_string(listed));
                return;
            }
            context.check(screen.selectedItemIndex != gSnap.slot, "Up/Down did not move the item cursor",
                    "pouch=" + std::to_string(pouch) + " slot=" + std::to_string(screen.selectedItemIndex));
        }
        void ckCursorModeCycled(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            context.check(screen.cursorMode != TrainerView::CursorMode::Menu,
                    "Y did not cycle the storage cursor mode");
        }
        // ADDING AN ITEM. Minus in the items view offers the legal ids the pouch does not hold; A adds
        // the highlighted one with a count of 1. The bag is put back afterwards (restoreBag) so the
        // save suite still sees the save as it was found -- this suite navigates, it does not edit.
        std::vector<std::vector<Trainer::InventoryItem>> gBagBefore;
        bool gUnsavedBeforeBag = false;
        int gAddPouch = -1;
        int gAddItemId = -1;
        bool gAddExpected = false;
        void prepAddItemPicker(TrainerView &screen, Trainer::Trainer &tr)
        {
            gBagBefore = tr.items;
            gUnsavedBeforeBag = screen.hasUnsavedChanges;
            gAddPouch = screen.selectedCategory;
            gAddItemId = -1;
            // Whether the picker SHOULD open: some legal id for this pouch is not held, and either it
            // is present at count 0 or the pouch has room to append one.
            gAddExpected = false;
            if (gAddPouch < 0 || gAddPouch >= (int)tr.items.size())
                return;
            const auto &pouch = tr.items[gAddPouch];
            const bool canAppend = (int)pouch.size() < screen.currentPouchCapacity();
            for (uint16_t legalId : Names::getPouchItems(tr.getGameGroup(), gAddPouch))
            {
                bool held = false, revivable = false;
                for (const auto &item : pouch)
                    if (item.itemId == legalId)
                    {
                        held = item.count != 0;
                        revivable = item.count == 0;
                    }
                if (!held && (revivable || canAppend))
                {
                    gAddExpected = true;
                    return;
                }
            }
        }
        void ckAddItemPicker(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (!gAddExpected)
            {
                context.skip("the open pouch has nothing to add (every legal item held, or full)",
                             "pouch=" + std::to_string(gAddPouch));
                return;
            }
            const bool pouchPicker = screen.pickerKind == UI::Dialogs::PickerKind::PouchItem ||
                                     screen.pickerKind == UI::Dialogs::PickerKind::PouchItemG1 ||
                                     screen.pickerKind == UI::Dialogs::PickerKind::PouchItemG3;
            if (context.check(screen.pickerActive && pouchPicker, "Minus in the items view did not open the add-item picker",
                              "pouch=" + std::to_string(gAddPouch)) &&
                context.check(!screen.pickerOrder.empty(), "the add-item picker offered nothing"))
                gAddItemId = screen.pickerOrder[screen.pickerSel];
        }
        void ckItemAdded(Context &context, TrainerView &screen, Trainer::Trainer &tr)
        {
            if (gAddItemId < 0)
                return; // the picker did not open; ckAddItemPicker already said why
            int count = 0;
            if (gAddPouch >= 0 && gAddPouch < (int)tr.items.size())
                for (const auto &item : tr.items[gAddPouch])
                    if (item.itemId == gAddItemId)
                        count = item.count;
            context.check(count == 1, "A in the add-item picker did not add the item with a count of 1",
                          "pouch=" + std::to_string(gAddPouch) + " item=" + std::to_string(gAddItemId) +
                              " count=" + std::to_string(count));
            context.check(!screen.pickerActive, "the add-item picker stayed open after adding");
            context.check(screen.hasUnsavedChanges, "adding an item did not mark the editor as having unsaved changes");
        }
        /// Not a user action: puts the bag and the dirty flag back as they were before the add.
        bool addItemPickerOpen(const TrainerView &screen, const Trainer::Trainer &) { return screen.pickerActive; }

        void restoreBag(TrainerView &screen, Trainer::Trainer &tr)
        {
            if (gBagBefore.empty())
                return;
            tr.items = gBagBefore;
            screen.hasUnsavedChanges = gUnsavedBeforeBag;
            screen.pickerActive = false;
            gBagBefore.clear();
        }

        /// Both answers are checked: with unsaved changes B must ask before leaving, without them it
        /// must leave. (Whether the editor SHOULD still be dirty here is ckSpeciesDiscarded's check.)
        void ckExit(Context &context, TrainerView &screen, Trainer::Trainer &)
        {
            if (screen.hasUnsavedChanges)
                context.check(screen.saveConfirmActive && !screen.goBack,
                        "B on the HOME menu with unsaved changes did not ask to save first");
            else
                context.check(screen.goBack, "B on the HOME menu did not leave the editor");
        }

        /// True when the save holds at least one boxed Pokemon anywhere.
        ///
        /// The details half of the box journey cannot run without one, and running it anyway does
        /// not merely fail -- it CORRUPTS THE RUN. A on an empty box slot opens the CREATOR, so the
        /// next A applies a species and writes a brand-new Pokemon into the box; the editor is then
        /// dirty, every later B raises the save prompt instead of going back, and each of the
        /// remaining journeys reports its own navigation as broken. One empty save produced 42
        /// failures that way, none of which was about the thing being tested.
        /// True when the party holds at least one Pokemon.
        ///
        /// Same failure as the boxes and a different blast radius: with an empty party, A opens no
        /// details page, so the B meant to CLOSE it leaves the party view instead -- and the next B,
        /// meant to leave the view, exits the editor. Every journey after that is one level out.
        bool anyPartyPokemon(const TrainerView &, const Trainer::Trainer &tr)
        {
            for (const auto &member : tr.party)
                if (!isEmptySlot(member.get()))
                    return true;
            return false;
        }

        bool anyBoxedPokemon(const TrainerView &, const Trainer::Trainer &tr)
        {
            const int slots = (int)tr.getSlotsPerBox();
            for (int boxIndex = 0; boxIndex < (int)tr.boxes.size(); ++boxIndex)
                for (int slotIndex = 0; slotIndex < slots && slotIndex < (int)Trainer::BOX_SLOTS; ++slotIndex)
                    if (!isEmptySlot(tr.boxes[boxIndex][slotIndex].get()))
                        return true;
            return false;
        }

        struct Step
        {
            const char *journey;
            const char *what;
            u64 button;
            int presses;
            uint32_t allow;
            void (*prep)(TrainerView &, Trainer::Trainer &);
            void (*post)(Context &, TrainerView &, Trainer::Trainer &);
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
            {"home", "open the editor", Btn::None, 1, ALLOW_NAVIGATION, nullptr, nullptr},
            {"home", "focus Pokemon (Boxes)", Btn::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, ckHome0},
            {"home", "focus Party", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, ckHome1},
            {"home", "focus Storage", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, ckHome2},
            {"home", "focus Items", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, ckHome3},
            {"home", "focus Trainer", Btn::Right, 1, ALLOW_NAVIGATION, nullptr, ckHome4},
            {"home", "focus Settings", Btn::Right, 1, ALLOW_NAVIGATION, nullptr, ckHome5},
            {"home", "wrap right, back to Items", Btn::Right, 1, ALLOW_NAVIGATION, nullptr, ckHome3},
            {"home", "wrap left, back to Settings", Btn::Left, 1, ALLOW_NAVIGATION, nullptr, ckHome5},

            {"boxes", "return to the pill column", Btn::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, ckHome0},
            {"boxes", "open the box view", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, ckEntered},
            {"boxes", "move the cursor right", Btn::Right, 1, ALLOW_CURSOR, prepSlot0, ckCursorMoved},
            {"boxes", "move the cursor down", Btn::DPAD_DOWN, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "move the cursor left", Btn::Left, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "move the cursor up", Btn::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "slot"},
            {"boxes", "next box with R", Btn::BUTTON_R, 1, ALLOW_CURSOR, nullptr, nullptr, "box"},
            {"boxes", "previous box with L", Btn::BUTTON_L, 1, ALLOW_CURSOR, nullptr, nullptr, "box"},
            // Everything from here to the end of the journey needs a Pokemon to open. On a save with
            // none, A opens the creator instead and the step after it would CREATE one -- see
            // anyBoxedPokemon().
            {"boxes", "open the details modal", Btn::BUTTON_A, 1, ALLOW_MODAL, prepOccupied, ckDetailsOpen, nullptr,
             anyBoxedPokemon},
            {"boxes", "scroll the details modal", Btn::DPAD_DOWN, 1, ALLOW_MODAL, nullptr, ckSpeciesRow, nullptr,
             anyBoxedPokemon},
            {"boxes", "open the species picker", Btn::BUTTON_A, 1, ALLOW_MODAL, prepSpeciesRow, ckSpeciesPicker,
             nullptr, anyBoxedPokemon},
            {"boxes", "close the species picker", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckSpeciesPickerClosed,
             nullptr, anyBoxedPokemon},
            // Closing an UNEDITED page must not prompt -- checked before the destructive half
            // below, which necessarily does.
            {"boxes", "close the details modal", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckDetailsClosed, nullptr,
             anyBoxedPokemon},

            // Editing the species is DESTRUCTIVE, so it goes last in this journey and is undone
            // immediately: B raises the unsaved-changes prompt and Y discards, which rolls the
            // Pokemon back to the snapshot taken when the page opened. Nothing reaches the save.
            {"boxes", "reopen the details modal", Btn::BUTTON_A, 1, ALLOW_MODAL, prepOccupied, ckDetailsOpen,
             nullptr, anyBoxedPokemon},
            {"boxes", "reopen the species picker", Btn::BUTTON_A, 1, ALLOW_MODAL, prepSpeciesRow, ckSpeciesPicker,
             nullptr, anyBoxedPokemon},
            {"boxes", "move to another species", Btn::DPAD_DOWN, 1, ALLOW_MODAL, nullptr, nullptr, nullptr,
             anyBoxedPokemon},
            {"boxes", "apply the species change", Btn::BUTTON_A, 1, ALLOW_MODAL | Allow::AllowData, prepSpeciesApply,
             ckSpeciesApplied, nullptr, anyBoxedPokemon},
            {"boxes", "ask to close after editing", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckDiscardPrompt, nullptr,
             anyBoxedPokemon},
            {"boxes", "discard the species change", Btn::BUTTON_Y, 1, ALLOW_MODAL | Allow::AllowData, nullptr,
             ckSpeciesDiscarded, nullptr, anyBoxedPokemon},
            {"boxes", "leave the box view", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckLeft},

            {"party", "focus Party", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, ckHome1},
            {"party", "open the party view", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, ckEntered},
            // A one-member party has nowhere to move, so these two promise nothing; the details
            // steps below need a member and are gated on one.
            {"party", "move down the party", Btn::DPAD_DOWN, 2, ALLOW_CURSOR, nullptr, nullptr},
            {"party", "move up the party", Btn::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr},
            {"party", "open the details modal", Btn::BUTTON_A, 1, ALLOW_MODAL, prepParty0, ckPartyDetailsOpen,
             "detailsModal", anyPartyPokemon},
            {"party", "close the details modal", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckDetailsClosed,
             "detailsModal", anyPartyPokemon},
            {"party", "leave the party view", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckLeft},

            // Items (no A: the quantity keypad is swkbd and would block the run)
            {"items", "focus Items", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, nullptr, "homeIndex"},
            {"items", "step into the icon row", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, ckHome3},
            {"items", "open the items view", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, ckEntered},
            {"items", "move down the item list", Btn::DPAD_DOWN, 3, ALLOW_CURSOR, nullptr, ckItemCursorMoved},
            {"items", "move up the item list", Btn::DPAD_UP, 1, ALLOW_CURSOR, nullptr, ckItemCursorMoved},
            {"items", "next pouch with R", Btn::BUTTON_R, 1, ALLOW_CURSOR, nullptr, nullptr, "category"},
            {"items", "previous pouch with L", Btn::BUTTON_L, 1, ALLOW_CURSOR, nullptr, nullptr, "category"},
            {"items", "open the add-item picker", Btn::MINUS, 1, ALLOW_MODAL, prepAddItemPicker, ckAddItemPicker},
            // Gated: with the picker shut, A would open the quantity keypad instead.
            {"items", "add the highlighted item", Btn::BUTTON_A, 1, ALLOW_MODAL | Allow::AllowItems | Allow::AllowUnsaved,
             nullptr, ckItemAdded, nullptr, addItemPickerOpen, "the add-item picker did not open, so no item was added"},
            // Puts the bag back (see restoreBag); a frame with no input, declared to change only that.
            {"items", "(test) put the bag back", Btn::None, 1, Allow::AllowItems | Allow::AllowUnsaved | ALLOW_MODAL,
             restoreBag, nullptr},
            {"items", "leave the items view", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckLeft},

            // Storage (the bank pane; pointed at the run's scratch bank). L/R change the focused
            // pane's box; the D-pad crosses between the panes at their facing edges.
            {"storage", "return to the pill column", Btn::DPAD_UP, 6, ALLOW_NAVIGATION, nullptr, ckHome0},
            {"storage", "focus Storage", Btn::DPAD_DOWN, 2, ALLOW_NAVIGATION, nullptr, ckHome2},
            {"storage", "open the storage view", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_STORAGE, nullptr,
             ckEntered},
            {"storage", "move the cursor right", Btn::Right, 1, ALLOW_STORAGE, nullptr, nullptr, "stSaveSlot"},
            {"storage", "move the cursor down", Btn::DPAD_DOWN, 1, ALLOW_STORAGE, nullptr, nullptr, "stSaveSlot"},
            {"storage", "next box with R", Btn::BUTTON_R, 1, ALLOW_STORAGE, nullptr, nullptr, "box"},
            {"storage", "previous box with L", Btn::BUTTON_L, 1, ALLOW_STORAGE, nullptr, nullptr, "box"},
            // Ten presses reach the save pane's right edge from anywhere in a row and cross it; the
            // bank pane's own right edge goes nowhere, so the extra presses are harmless.
            {"storage", "cross to the bank pane", Btn::Right, 10, ALLOW_STORAGE, nullptr, nullptr, "focusPane"},
            {"storage", "cross back to the save pane", Btn::Left, 10, ALLOW_STORAGE, nullptr, nullptr, "focusPane"},
            {"storage", "cycle the cursor mode", Btn::BUTTON_Y, 1, ALLOW_STORAGE | Allow::AllowSwap, nullptr,
             ckCursorModeCycled},
            {"storage", "cycle back to Menu mode", Btn::BUTTON_Y, 2, ALLOW_STORAGE | Allow::AllowSwap, nullptr,
             ckCursorModeIsMenu},
            {"storage", "leave the storage view", Btn::BUTTON_B, 1, ALLOW_MODAL | ALLOW_STORAGE, nullptr, ckLeft},

            // Trainer card (no A: rows 0 and 1 open the keyboard / keypad)
            {"trainer", "focus Trainer", Btn::DPAD_DOWN, 1, ALLOW_NAVIGATION, nullptr, nullptr, "homeIndex"},
            {"trainer", "step along to Trainer", Btn::Right, 1, ALLOW_NAVIGATION, nullptr, ckHome4},
            {"trainer", "open the trainer card", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, ckEntered},
            {"trainer", "move down the rows", Btn::DPAD_DOWN, 1, ALLOW_CURSOR, nullptr, nullptr, "trainerRow"},
            {"trainer", "move up the rows", Btn::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "trainerRow"},
            {"trainer", "leave the trainer card", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckLeft},

            {"settings", "focus Settings", Btn::Right, 1, ALLOW_NAVIGATION, nullptr, ckHome5},
            {"settings", "open settings", Btn::BUTTON_A, 1, ALLOW_NAVIGATION | ALLOW_CURSOR, nullptr, ckEntered},
            {"settings", "move down the rows", Btn::DPAD_DOWN, 2, ALLOW_CURSOR, nullptr, nullptr, "settingsRow"},
            {"settings", "move up the rows", Btn::DPAD_UP, 1, ALLOW_CURSOR, nullptr, nullptr, "settingsRow"},
            {"settings", "leave settings", Btn::BUTTON_B, 1, ALLOW_MODAL, nullptr, ckLeft},

            {"exit", "press B on the HOME menu", Btn::BUTTON_B, 1,
             Allow::AllowExit | Allow::AllowModal | Allow::AllowView, nullptr, ckExit},
        };
        constexpr int JOURNEY_STEP_COUNT = (int)(sizeof(JOURNEY_SCRIPT) / sizeof(JOURNEY_SCRIPT[0]));

        std::unique_ptr<TrainerView> gScreen;
        bool gHaveSnap = false;
        /// The journey whose "nothing to open" skip has already been noted for THIS target.
        const char *gSkipNotedJourney = nullptr;
        /// Keyboard prompts refused so far, so a step that raises a NEW one can be named.
        int gKeyboardPromptsSeen = 0;
        std::string gPrevBankRoot;
        bool gBankOverridden = false;

        void teardown()
        {
            Runner::setDrivenScreen(nullptr);
            gScreen.reset();
            gHaveSnap = false;
            gSkipNotedJourney = nullptr;
            if (gBankOverridden)
            {
                Trainer::Bank::setRootOverride(gPrevBankRoot);
                gBankOverridden = false;
            }
        }
    }

    int uiPlan(Context &context)
    {
        teardown();
        if (!context.trainer)
            return 0;
        // Chunk 0 builds the screen, chunks 1..JOURNEY_STEP_COUNT*FRAMES_PER_STEP run the script, and
        // ONE MORE is needed to reach `stepIdx >= JOURNEY_STEP_COUNT` and tear down. Without it the
        // driven screen and its reference to the trainer outlive the target -- and the runner frees the
        // previous target's trainer before loading the next, so the very next frame draws a
        // TrainerViewScreen whose `trainer&` has been freed. That is a console-only crash: the host
        // harness never draws, so it never dereferences it.
        return JOURNEY_STEP_COUNT * FRAMES_PER_STEP + 2;
    }

    void uiRun(Context &context, int chunk)
    {
        if (!context.trainer)
            return;
        // One chunk per frame, always: the walkthrough is meant to be watched, and without this the
        // budget loop would run the whole thing between two frames (see the file header).
        Runner::yieldFrame();

        Trainer::Trainer &tr = *context.trainer;

        if (chunk == 0)
        {
            // Point the bank at the run's scratch directory BEFORE the screen is built: its
            // constructor opens one, and the user's real bank is not this test's business.
            gPrevBankRoot.clear();
            gSkipNotedJourney = nullptr; // a fresh target gets to report its own coverage gaps
            gKeyboardPromptsSeen = Utils::keyboardPromptsSuppressed();
            Trainer::Bank::setRootOverride(context.workDir.empty() ? std::string("sdmc:/PKSE/tests/work")
                                                               : context.workDir);
            gBankOverridden = true;

            gScreen = std::make_unique<TrainerView>(tr, context.target, context.workDir, context.titleId,
                                                    context.accountUid, false);
            if (!context.check(gScreen != nullptr, "the editor screen could not be built"))
                return;

            // Publish it: from here TestRunnerScreen draws THIS instead of its own progress view.
            Runner::setDrivenScreen(gScreen.get());
            gSnap = UISnapshot::take(*gScreen, tr);
            gHaveSnap = true;

            const std::string banner = "[ui] " + context.target + " -- walking the editor (" +
                                       std::to_string(JOURNEY_STEP_COUNT) + " steps)";
            if (context.rep)
                context.rep->addTrace(banner);
            Utils::logTest(banner);
            context.check(!gScreen->detailViewActive,
                      "the editor opened inside a view instead of on the HOME menu");
            return;
        }

        if (!gScreen)
            return;
        TrainerView &s = *gScreen;

        // Chunk 0 built the screen, so the script starts at chunk 1 -- without the offset the
        // first step would share chunk 0 with the build and never run.
        const int stepIdx = (chunk - 1) / FRAMES_PER_STEP;
        if (stepIdx >= JOURNEY_STEP_COUNT)
        {
            teardown();
            return;
        }

        Journey j(context, s, &s, &tr, JOURNEY_SCRIPT[stepIdx].journey);
        if (gHaveSnap)
            j.restore(gSnap);

        if ((chunk - 1) % FRAMES_PER_STEP != 0)
        {
            // A holding frame: pump the screen so its animations and timers keep running, but do
            // not snapshot, verify or trace. These frames exist to make the previous step readable
            // on screen; checking them would re-hash every box in the save sixty times a second and
            // bury the trace under "(hold) -> changed: nothing".
            j.idle();
            return;
        }

        const Step &st = JOURNEY_SCRIPT[stepIdx];
        if (st.needs && !st.needs(s, tr))
        {
            // Note ONCE per journey. Every gated step in a journey shares one precondition, so a
            // note per step would print the same sentence a dozen times and bury it. Reset per
            // target in setup() -- the journey names are the same string literals for every save,
            // so comparing them alone would report the first target's gap and stay silent for the
            // rest, which is the opposite of what a coverage note is for.
            if (gSkipNotedJourney != st.journey)
            {
                gSkipNotedJourney = st.journey;
                context.skip(st.skippedBecause ? st.skippedBecause
                                               : "this save has no Pokemon to open, so the details page was not walked",
                         std::string("journey=") + st.journey);
            }
            j.idle(); // keep pumping the screen; press nothing
            return;
        }
        if (st.prep)
            st.prep(s, tr);

        if (st.button == Btn::None)
            j.settle(st.what, st.allow, 2);
        else
            j.act(st.what, st.button, st.allow, st.presses);

        if (st.post)
            st.post(context, s, tr);
        if (st.mustChange)
            context.check(j.lastChangedField(st.mustChange), "a UI action did not do what it is for",
                          std::string("journey=") + st.journey + " action=\"" + st.what + "\" expected=" +
                              st.mustChange + " changed=" + j.lastChanged());

        // A journey that reached a keyboard did something it did not intend. The run cannot hang on
        // one -- Runner::start refuses the applet outright -- but a refusal is still a step landing
        // somewhere the script does not think it is, which is exactly what a derailed journey looks
        // like from the inside. Report it rather than let the suppression hide it.
        {
            const int suppressed = Utils::keyboardPromptsSuppressed();
            if (suppressed != gKeyboardPromptsSeen)
            {
                gKeyboardPromptsSeen = suppressed;
                context.check(false, "a UI action tried to open the keyboard, which would block an automated run",
                          std::string("journey=") + st.journey + " action=\"" + st.what + "\"");
            }
        }

        gSnap = j.snapshot();

        Runner::DriveCaption caption;
        caption.target = context.target;
        caption.journey = st.journey;
        caption.action = st.what;
        caption.changed = j.lastChanged();
        caption.changedSomethingUndeclared = j.lastWasBad();
        caption.step = stepIdx + 1;
        caption.total = JOURNEY_STEP_COUNT;
        Runner::setDriveCaption(caption);
    }
}
