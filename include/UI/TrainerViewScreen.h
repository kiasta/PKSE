#ifndef UI_TRAINER_VIEW_SCREEN_H
#define UI_TRAINER_VIEW_SCREEN_H

#include <cstddef>   // std::byte (details.editSnapshot)
#include <memory>
#include <string>
#include <vector>

#include <switch.h>

#include "Globals.h"          // g_injectToGameSave gates the save-destination picker
#include "UI/UIScreen.h"
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Bank.h"
#include "Panels/PartyPokemonPanel.h"
#include "Panels/ItemsPanel.h"
#include "Dialogs/ItemEditDialog.h"
#include "Dialogs/SaveConfirmDialog.h"
#include "Dialogs/StatEditDialog.h"
#include "Dialogs/PickerDialog.h"
#include "Modals/PokemonDetailsModal.h"

// Forward declaration
namespace Trainer {
    class Trainer;
}

namespace UI {
    class TrainerViewScreen : public UIScreen {
    public:
        enum class ViewMode {
            Party,
            Boxes,
            Items,
            Storage,  // HOME-style dual-pane: save boxes (left) <-> bank (right)
            Trainer,  // trainer info card (reached from the HOME main menu)
            Settings  // settings screen (auto-backup + theme)
        };

        // Cursor modes for the Storage view (cycled with Y). Colors: red / blue / green -- the same
        // three-arrow scheme HOME uses, and the cursor arrow is drawn in the active mode's color.
        enum class CursorMode {
            Menu,   // red:   A opens a per-Pokemon menu (Move / Edit / Release)
            Move,   // blue:  A directly picks up / places / swaps one Pokemon
            Multi   // green: A anchors a rectangle, moving expands it, A again grabs the whole block
        };
        // Where the details editor's target Pokemon lives.
        enum class EditSource { Party, Box, Bank };

        TrainerViewScreen(Trainer::Trainer& trainer, const std::string& titleName, const std::string& backupDir, u64 titleId, AccountUid userUid, bool loadedFromCart);
        void update(const PadState& pad, const TouchInput& touch) override;
        void draw(PKSEFramebuffer& fb) override;
        bool shouldExit() const override { return goBack; }
        bool hasRequestedExit() const { return exitRequested; }

        // Storage (bank) view input + helpers (Phase 3.3b). Called from update().
        void handleStorageInput(u64 kDown);
        void returnHeldToOrigin();
        std::unique_ptr<Pokemon::Pokemon>& storageSlot(int pane, int box, int slot);  // pane 0=save,1=bank
        bool storageSlotLocked(int pane, int box, int slot);   // LGPE party members (save pane) are locked
        bool convertForPane(std::unique_ptr<Pokemon::Pokemon>& pk, int destPane);  // convert a mon in place for a dest pane (Phase B)
        void buildAbilityPickerOrder(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t current);  // the species' legal ability slots (all ids too, when illegal edits are allowed)
        void buildCreatorSpeciesOrder();  // creator: filter the species picker to the open game's dex
        void buildMovePickerOrder(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t current);  // learnable moves first
        void preparePickerSearch();  // snapshot the current move/item option set before filtering
        void applyPickerSearch(const std::string& query);  // filter the snapshot without changing legality
        void buildFormPickerOrder(uint16_t species, uint8_t current, Enums::GameVersion group);  // storable forms only (drops battle-only ones + the ones this game doesn't have)
        void buildGenderPickerOrder(uint16_t species, uint8_t form, uint8_t current);  // only the genders the species can be (one row when fixed-gender)
        bool genderEditable(const Pokemon::Pokemon& p) const;  // false -> the Gender row is read-only (nothing to change it TO)
        void openStorageEditor(int pane, int box, int slot);   // open the details modal on a storage slot

        // --- HOME-style rectangle select + block carry (see moveMon below) ---
        int paneCols(int pane) const;      // grid columns (LGPE save boxes are 5 wide, everything else 6)
        int paneSlots(int pane) const;     // slots per box in that pane
        int paneBoxes(int pane) const;     // box count in that pane
        void storagePickup();          // the A press: anchor / grab / put down, whichever applies
        void pickupSingle();           // Move mode: lift the slot under the cursor as a 1x1 block
        void pickupMulti();            // Multi mode: anchor the rectangle, or grab it if already anchoring
        void grabSelection(bool remove);   // lift (or copy, when !remove) the anchored rectangle into moveMon
        void scrunchSelection();       // trim all-empty edge rows/columns off the grabbed block
        void postPickup();             // drop an all-empty block so the hands read as free
        bool checkPutDownBounds() const;   // does the block fit in the focused pane from the cursor cell?
        void putDownBlock();           // exact-slot placement: cell (x,y) -> cursor slot + x + y*cols
        void cancelSelection();        // abandon an in-progress rectangle

        bool lgpeConversionInvolved(int destPane, const Pokemon::Pokemon* pk) const;  // would placing pk into destPane run an LGPE (AV/EV-reset) conversion?
        bool blockInvolvesLgpe(int destPane) const;            // does any carried mon trigger an LGPE conversion into destPane?
        bool gen3DowngradeInvolved(int destPane, const Pokemon::Pokemon* pk) const;  // would placing pk into destPane convert it DOWN into Gen 3 (destructive PID rebuild)?
        bool blockInvolvesGen3Downgrade(int destPane) const;    // does any carried mon trigger a Gen 3 downgrade into destPane?
        Pokemon::Pokemon* detailsTargetPokemon();              // resolve the editor's current target (party/box/bank)
        void mirrorEditedPartyMember();                        // after an edit, keep an LGPE party member's box/party copies in sync
        void snapshotEditTarget();     // capture the target's bytes as the save baseline (modal open + X = Save); revert restores to it
        bool pokemonEditDirty();       // has the details target changed vs the snapshot? (drives the "Unsaved changes" marker)
        void restoreEditTarget();      // roll the details target back to the snapshot -- discards edits when the page closes without Save
        void closeDetailsModal();      // reset all details-modal / edit state and close it
        std::vector<int> visibleItemIndices() const;           // raw indices in the current pouch worth showing (count > 0)
        int currentPouchCapacity() const;                      // slot limit of the current pouch; 0 = appending unsupported
        void sortStorageBox(int pane, int box);                // pack + order one box, pinning party-linked slots
        int currentItemMaxCount() const;                       // per-stack ceiling for the current pouch (never 0)

        // Public state - accessible by UI components (Panels, Dialogs, Modals)
        Trainer::Trainer& trainer;
        std::string titleName;
        std::string backupDir;
        std::string gameVersion;  // Actual game version from NACP (e.g., "1.0.1", "1.3.2")
        u64 titleId;
        AccountUid userUid;
        bool goBack = false;
        bool exitRequested = false;  // True when user presses + to close app

        // This block is public + mutable BY DESIGN: the panels/dialogs/modals read and write it
        // directly (immediate-mode UI). The biggest cohesive clusters are grouped into nested structs
        // (statEdit / creator / details); the rest are flat single-purpose flags + indices, each
        // default-initialised in place so the constructor only wires up the ctor arguments.

        ViewMode selectedMode = ViewMode::Party;
        int currentPage = 0;
        int selectedCategory = 0;  // For Items mode: selected PouchType
        int selectedBoxIndex = 0;  // For Boxes mode: selected box (0-31)
        int selectedPartyIndex = 0;  // For Party mode: selected party Pokemon (0-5)
        bool detailViewActive = false;  // True when detail panel is active (for Items/Boxes)
        int selectedItemIndex = 0;  // Selected item/pokemon index in detail view (item for Items, slot for Boxes)

        // HOME main menu focus (shown when NOT entered). 0 Pokemon(Boxes), 1 Party, 2 Storage (pills);
        // 3 Items, 4 Trainer, 5 Settings (circular icons). Replaces the old left mode-selector.
        int homeMenuIndex = 0;

        // Selected row in the Settings view (0-4); reached from the menu's Settings icon.
        int settingsSelectedRow = 0;

        // Trainer info view: the focused editable row (0 Name, 1 Money) and a
        // one-frame-deferred edit request. Name/Money open the blocking swkbd; deferring one frame lets
        // the row highlight render before the applet suspends the app (same trick as pendingHeaderRename).
        // -1 = nothing pending.
        int trainerSelectedRow = 0;
        int pendingTrainerEdit = -1;
        void editTrainerName();     // swkbd edit of the OT name (charset-validated, per-game length cap)
        void editTrainerMoney();    // numpad edit of money (clamped to the game's max)
        // After a name edit, re-stamp the trainer identity your Pokemon store so they stay
        // recognized as yours (party + boxes; NOT the cross-game bank). Both the OT name AND the
        // Gen 7+ handler (HT) name are part of the identity the games match on, so a bare trainer
        // edit would make your caught mons read as traded (obedience / traded-EXP) and your traded-in
        // mons lose you as their handler. Matches OT by ID32 + the carried OT name, and HT by the carried
        // HT name (the HT format has no TID/SID). `caughtName` is the pre-rename name for a rename, the
        // unchanged name otherwise; genuinely foreign stamps are untouched. Returns count changed.
        int restampCaughtPokemonIdentity(const std::u16string& caughtName);

        // Box swap (Phase 3 3.1): press Y to grab the slot under the cursor, then Y on another
        // occupied slot to swap them. swapSourceBox/Slot record the grabbed slot.
        bool swapActive = false;
        int swapSourceBox = -1;
        int swapSourceSlot = -1;

        // A tap on a box-name pill sets this instead of opening the rename immediately, so the header
        // highlight draws one frame BEFORE the blocking keyboard applet (otherwise the selection only
        // appears after the dialog closes). Consumed at the top of the next update().
        bool pendingHeaderRename = false;

        // --- HOME-style Storage view: dual-pane save boxes <-> bank ---
        std::unique_ptr<Trainer::Bank> bank;        // created in the ctor
        CursorMode cursorMode = CursorMode::Menu;   // default red (menu); cycled with Y
        int storageFocusPane = 0;                   // 0 = save (left), 1 = bank (right)
        int stSaveBox = 0, stSaveSlot = 0;          // cursor in the save pane
        int stBankBox = 0, stBankSlot = 0;          // cursor in the bank pane
        std::string storageStatus;                  // transient status line (e.g. a refused cross-game drop)
        int storageStatusFrames = 0;                // frames left to show storageStatus (counts down)
        // Post a message to that line. This is PKSE's only way to tell the user anything: there is no
        // console on the Switch, and logErrorToFile writes somewhere they cannot read while the app is
        // running. Anything a user needs to know about MUST come through here, not just the log.
        void postStatus(const std::string& msg, int frames = 240) {
            storageStatus = msg;
            storageStatusFrames = frames;
        }
        // --- The carried block ---
        // Cells of the rectangle currently in hand, row-major over selectDimensions (w x h). A null
        // cell is a hole -- the source slot was empty, or it was locked and deliberately left behind.
        // An empty vector means hands free.
        //
        // A Move-mode pick-up is just a 1x1 block, so this is the ONLY carry state: there is no
        // separate "held single Pokemon" to keep in sync, and every guard that asks "are we holding
        // something?" (compaction, sort, exit, box rename) answers for both cases at once.
        std::vector<std::unique_ptr<Pokemon::Pokemon>> moveMon;
        // Two meanings:
        //   while currentlySelecting -> the anchor cell as (column, row) of the rectangle being drawn
        //   while carrying           -> the block's dimensions as (width, height)
        std::pair<int, int> selectDimensions{0, 0};
        bool currentlySelecting = false;         // Multi mode: a rectangle is being dragged out
        int selectPane = 0, selectBox = 0;       // which pane/box the in-progress rectangle lives in
        // Origin of the block's TOP-LEFT cell, so B can put the whole thing back where it came from.
        int heldPane = 0, heldFromBox = 0, heldFromSlot = 0;
        bool carrying() const { return !moveMon.empty(); }
        int carriedCount() const;                // non-null cells in moveMon
        const Pokemon::Pokemon* firstCarried() const;   // first non-null cell, or nullptr

        // Red per-Pokemon action menu (Move / Edit / Release / Cancel).
        bool storageMenuActive = false;
        int storageMenuIndex = 0;
        int menuPane = 0, menuBox = 0, menuSlot = 0;           // the slot the menu acts on
        // Group menu for the carried block (Release all / Return to origin / Cancel), opened with Minus.
        bool groupMenuActive = false;
        int groupMenuIndex = 0;
        // Release confirmation (single slot, or the whole carried block when releaseGroup).
        bool releaseConfirmActive = false;
        int releasePane = 0, releaseBox = 0, releaseSlot = 0;
        bool releaseGroup = false;
        // Leaving the storage view with unsaved bank changes: prompt Save / Discard / Cancel (HOME-style).
        bool storageExitConfirmActive = false;
        int storageExitConfirmIndex = 0;   // 0=Save & Exit, 1=Discard & Exit, 2=Cancel
        // Set when + (exit app) raised that prompt instead of B. Closing the app is not an answer to
        // "save the bank?", so + asks first and the exit resumes once Save or Discard has been picked.
        bool exitAfterBankChoice = false;
        void beginAppExit();   // the + handler's tail: prompt about the game save, else leave
        // Touchable storage slot rects, captured during draw for tap hit-testing next frame.
        struct TouchTarget { int pane, box, slot, x, y, w, h; };
        std::vector<TouchTarget> storageTouchTargets;
        // Touchable popup/dialog buttons (id = menu item index, or a dialog-specific id), captured
        // during draw and hit-tested next frame. Only the active overlay populates this.
        struct TouchButton { int id, x, y, w, h; };
        std::vector<TouchButton> touchButtons;
        int touchedButtonId(const TouchInput& touch) const;  // id of a tapped button, or -1
        void renameBox(int boxIndex);       // swkbd rename of a SAVE box; no-op where unsupported
        void renameBankBox(int box);        // swkbd rename of a BANK box (default label is "Bank N")

        // Item editing state
        bool itemEditDialogActive = false;  // True when editing an item's amount
        int itemEditDialogValue = 0;    // Current value being edited
        int itemEditDialogOriginalValue = 0;  // Original value before editing
        // Items list: Y asks before removing the selected item. A in this dialog does the delete,
        // B cancels. Only reachable from the Items view, so it never lets X-to-save fire (home-menu only).
        bool itemRemoveConfirmActive = false;

        // Save confirmation state
        bool saveConfirmActive = false;
        // Save destination picker.
        //   0 = this backup   1 = new named backup   2 = game save
        //
        // Whether "game save" is offered depends on WHERE THIS SESSION CAME FROM, not on a blanket
        // setting:
        //   - Loaded from the live save  -> always offered, and the default. Writing your own save
        //     back is the ordinary thing a save editor does; gating it behind a toggle is friction
        //     for no safety gain, because the data you'd overwrite is the data you just read.
        //   - Loaded from an older backup -> offered only when the Settings lock is on, and it
        //     raises an extra confirmation, because THIS is the case that rolls a game backwards.
        //
        // A TITLE session is also not offered "this backup". That backup is the snapshot taken
        // automatically when the save was loaded, not a file the user chose, so presenting it
        // beside two deliberate destinations just poses a question with no obvious answer -- and
        // picking it silently sends the edits somewhere the game will never read. A title session
        // gets the two answers that mean something: write it back, or file it under a new name.
        enum SaveDest { DestThisBackup = 0, DestNewBackup = 1, DestGameSave = 2 };
        bool loadedFromCart = false;

        /// Cursor into the VISIBLE rows, not a SaveDest -- the two stopped being interchangeable
        /// once a title session dropped a row from the middle of the enum. Map with saveDestAt().
        int saveDestIndex = 0;
        int saveDestCount() const { return loadedFromCart ? 2 : (g_injectToGameSave ? 3 : 2); }
        /// Which destination a visible row means. Title sessions lead with the game save because
        /// it is both the default and the point of the session; backup sessions keep the original
        /// order, where row 0 is the backup already open.
        SaveDest saveDestAt(int row) const {
            if (loadedFromCart) return row == 0 ? DestGameSave : DestNewBackup;
            return static_cast<SaveDest>(row);
        }
        /// The row the save dialog opens on -- the first one, in both modes.
        int defaultSaveDestRow() const { return 0; }

        /// Set once a value outside the games' own limits has actually been committed this session
        /// (EV > 252, AV > 200, or an EV total over 510 — only reachable with "Allow illegal values"
        /// on). The save dialog then carries a warning line. Entirely separate from the destination
        /// logic: it is about what is being written, not where.
        bool illegalDataWritten = false;

        // Create sdmc:/PKSE/{title}/{name}/ seeded with a copy of the current backup, suffixing
        // -2, -3... if the name is taken. Returns the new path, or "" on failure.
        std::string createNamedBackupDir(const std::string& name);
        // Last gate before overwriting the player's real save data. Reached only by choosing the
        // "Game save" destination, which is itself only offered when the Settings lock is on.
        bool saveInjectConfirmActive = false;
        // Write to destDir, optionally injecting into the game. Shared by every destination so the
        // success/failure handling can't drift between them.
        void performSave(const std::string& destDir, bool injectToTitle);
        bool hasUnsavedChanges = false;
        bool exitingWithUnsavedChanges = false;
        bool exitingViaPlus = false;  // True when exiting via + button (exit app) vs B button (go back)

        // Stat editor (IV / EV / AV + shiny). Original* is the value on dialog entry; Current* is the
        // in-progress edit, preserved when switching between the IV/EV/AV modes.
        struct StatEditState {
            bool dialogActive = false;
            int  selectedStat = 0;                          // 0-5: HP, ATK, DEF, SPE, SPA, SPD
            Dialogs::StatEditMode mode = Dialogs::StatEditMode::IV;
            int  value = 0;                                 // current edit (the active IV/EV/AV)
            int  originalIV = 0, originalEV = 0, originalAV = 0;   // on entry (AV = Let's Go)
            int  currentIV = 0,  currentEV = 0,  currentAV = 0;    // edits, kept across mode switches
        };
        StatEditState statEdit;

        // Pokemon details modal (the HOME "Check Summary" editor page): open/target, cursor, overlays,
        // hexagon mode, the left-pane scroll + nav list, and the edit baseline that drives the top-bar
        // "Unsaved changes" marker (snapshot on open, re-taken by X = Save; empty when not editing).
        struct DetailsState {
            bool active = false;
            EditSource source = EditSource::Box;         // where the edited Pokemon lives
            int  bankBox = 0, bankSlot = 0;              // bank target (EditSource::Bank)
            int  partyIndex = 0;                         // party slot (0-5) when editing a party mon
            int  category = 0;                           // 0=Main,1=Met,2=Stats,3=Moves,4=Cosmetic,5=OT/Misc
            int  selectedStat = 0;                       // Stats category: which stat is selected
            int  selectedField = 0;                      // center column: selected field (see the modal draw)
            bool editing = false;                        // editing a stat value / main field
            int  hexMode = 0;                            // hexagon (Y-cycled): 0 Summary, 1 Base Points (EV/AV), 2 Judge (IVs)
            bool legalityOverlay = false;                // full legality issue list (Y / tap)
            bool ribbonOverlay = false;                  // full ribbon/mark list (X / tap)
            /// "You have unsaved changes" prompt, raised when the page is closed while
            /// pokemonEditDirty(). Closing used to roll the mon straight back to its snapshot and
            /// leave, so edits vanished with nothing said -- the top-bar marker was the only clue,
            /// and it disappears along with the page. The creator's brand-new mon already asked
            /// Keep/Discard on the way out; an EXISTING mon being edited did not.
            bool discardConfirmActive = false;
            int  lastCenterField = 0;                    // remembered center-column row when hopping to/from moves
            int  leftScroll = 0;                         // left-pane vertical scroll (px); auto-follows selection, reset on (re)open
            std::vector<int> leftOrder;                  // left-pane editable field ids in DRAW order (rebuilt each draw)
            std::vector<std::byte> editSnapshot;         // baseline bytes for the "Unsaved changes" marker + revert
        };
        DetailsState details;
        // Creator: the Species picker was opened in "create a new mon" mode; where the built mon lands,
        // plus the not-yet-accepted-mon editing flow (Keep/Discard on exit).
        struct CreatorState {
            bool active = false;                 // Species picker is in create-a-new-mon mode
            int  pane = 0, box = 0, slot = 0;    // where the built mon is dropped
            bool editing = false;                // details modal is on a just-created, not-yet-accepted mon
            bool keepConfirmActive = false;      // "Keep this new Pokemon?" prompt shown on exit
            int  keepConfirmIndex = 1;           // cursor: 0 = Discard, 1 = Keep (default)
        };
        CreatorState creator;
        // Lossy-move acknowledgements. TWO independent warnings about two unrelated losses, kept apart
        // because they are not the same question:
        //
        //   Gen 3 down-convert  -- rebuilds the PID to preserve nature, and drops nickname/ribbons/held
        //                          item. Destructive and irreversible, so it is shown ALWAYS, whatever
        //                          the Move warning setting says.
        //   Let's Go transfer   -- resets AV/EV training to 0 and drops unlearnable moves. Recoverable
        //                          by re-training, so it is gated by g_moveWarn.
        //
        // A move can be both (an LGPE mon into FireRed). The Gen 3 notice supersedes: it is the more
        // severe of the two and stacking dialogs on one action reads as a bug.
        enum class PendingMove { None, PlaceHeld };
        bool gen3ConvertConfirmActive = false;   // "Convert to Gen 3?"     -- never gated
        bool lgpeTransferConfirmActive = false;  // "Let's Go transfer"     -- gated by g_moveWarn
        int  moveConfirmIndex = 1;               // cursor: 0 = Cancel, 1 = Continue (default)
        // The action both warnings guard is the same one, so the stash they resume is shared.
        PendingMove pendingMove = PendingMove::None;
        int pendingMovePane = 0, pendingMoveBox = 0, pendingMoveSlot = 0;
        bool moveConfirmActive() const noexcept { return gen3ConvertConfirmActive || lgpeTransferConfirmActive; }

        // Reusable selection panel (picker) for choosing a value from a list — nature, gender, move.
        bool pickerActive = false;
        Dialogs::PickerKind pickerKind = Dialogs::PickerKind::Nature;
        int pickerSlot = 0;   // which of the 4 move slots (when pickerKind == Move)
        int pickerSel = 0;    // highlighted option index
        int pickerCount = 0;  // number of options in the list
        // A pouch-item picker opened to CHANGE an existing item's type (Potion -> Super Potion),
        // not to add a new one. Changes the confirm behavior + the picker title; reuses PouchItem.
        bool itemPickerReplace = false;
        // Pickers whose visible rows do not map directly to stored values use this row -> value list.
        // pickerLegalCount marks a green legal prefix where applicable (abilities and moves).
        std::vector<int> pickerOrder;
        int pickerLegalCount = 0;
        // Searchable move/item pickers keep their complete, already-filtered option set here.
        // pickerOrder is then the visible subset, so every existing draw/apply path keeps using the
        // same row -> value mapping and clearing the query can restore the exact original list.
        std::string pickerSearchQuery;
        std::vector<int> pickerSearchOrder;
        int pickerSearchLegalCount = 0;
        int pickerSearchValue = 0;
        // Met-location picker: the origin version whose location table is shown, so the picker can
        // resolve each id in pickerOrder to a name (a location id names a different place per game).
        uint8_t pickerMetVersion = 0;
        // Form picker: the species whose forms are being listed, so the picker can name each form.
        uint16_t pickerFormSpecies = 0;
        // Location picker mode: true routes the pick to the egg-met location, false to the met location.
        bool pickerMetIsEgg = false;
    };
}

#endif
