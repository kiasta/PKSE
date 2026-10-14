#ifndef UI_TRAINER_VIEW_SCREEN_H
#define UI_TRAINER_VIEW_SCREEN_H

#include <cstddef> // std::byte (details.editSnapshot)
#include <memory>
#include <string>
#include <vector>

#include <switch.h>

#include "Globals.h" // g_injectToGameSave gates the save-destination picker
#include "UI/UIScreen.h"
#include "UI/NavigationRepeat.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ListSearch.h" // the shared search box every list uses
#include "Trainer/Bank.h"
#include "Panels/PartyPokemonPanel.h"
#include "Panels/ItemsPanel.h"
#include "Dialogs/ItemEditDialog.h"
#include "Dialogs/SaveConfirmDialog.h"

// Forward-declared rather than included: only a const reference is needed here, and Pokemon/Trade.h
// has no business in a header this widely included (CODING_STANDARDS 6.5).
namespace Pokemon
{
    struct TradePartner;
}
#include "Dialogs/StatEditDialog.h"
#include "Dialogs/PickerDialog.h"
#include "Dialogs/FileBrowserDialog.h"
#include "Dialogs/PKSMImportDialog.h"
#include "Modals/PokemonDetailsModal.h"

// Forward declaration
namespace Trainer
{
    class Trainer;
}

namespace UI
{
    class TrainerViewScreen : public UIScreen
    {
    public:
        enum class ViewMode
        {
            Party,
            Boxes,
            Items,
            Storage, // HOME-style dual-pane: save boxes (left) <-> bank (right)
            Trainer, // trainer info card (reached from the HOME main menu)
            Settings // settings screen (auto-backup + theme)
        };

        // Cursor modes for the Storage view (cycled with Y). Colors: red / blue / green -- the same
        // three-arrow scheme HOME uses, and the cursor arrow is drawn in the active mode's color.
        enum class CursorMode
        {
            Menu, // red:   A opens a per-Pokemon menu (Move / Edit / Release)
            Move, // blue:  A directly picks up / places / swaps one Pokemon
            Multi // green: A anchors a rectangle, moving expands it, A again grabs the whole block
        };
        // Where the details editor's target Pokemon lives.
        enum class EditSource
        {
            Party,
            Box,
            Bank
        };

        TrainerViewScreen(Trainer::Trainer &trainer, const std::string &titleName, const std::string &backupDir,
                          u64 titleId, AccountUid userUid, bool loadedFromCart);
        void update(const PadState &pad, const TouchInput &touch) override;
        void draw(PKSEFramebuffer &framebuffer) override;
        bool shouldExit() const override { return goBack; }
        bool hasRequestedExit() const { return exitRequested; }

        // Storage (bank) view input + helpers (Phase 3.3b). Called from update().
        void handleStorageInput(u64 buttonsDown);
        void returnHeldToOrigin();
        std::unique_ptr<Pokemon::Pokemon> &storageSlot(int pane, int box, int slot); // pane 0=save,1=bank
        bool storageSlotLocked(int pane, int box, int slot); // LGPE party members (save pane) are locked
        bool convertForPane(std::unique_ptr<Pokemon::Pokemon> &pokemon, int destPane);
        void buildAbilityPickerOrder(
            uint16_t species, uint8_t form, Enums::GameVersion group,
            uint16_t current); // the species' legal ability slots (all ids too, when illegal edits are allowed)
        void buildCreatorSpeciesOrder(); // creator: filter the species picker to the open game's dex
        /// Species the given game has an entry for, with `current` pre-selected (0 for none).
        void buildSpeciesPickerOrder(Enums::GameVersion group, uint16_t current);
        // learnable moves first
        void buildMovePickerOrder(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t current);
        void buildFormPickerOrder(
            uint16_t species, uint8_t current,
            Enums::GameVersion group); // storable forms only (drops battle-only ones + the ones this game doesn't have)
        // only the genders the species can be (one row when fixed-gender)
        void buildGenderPickerOrder(uint16_t species, uint8_t form, uint8_t current);
        // false -> the Gender row is read-only (nothing to change it TO)
        bool genderEditable(const Pokemon::Pokemon &p) const;
        void openStorageEditor(int pane, int box, int slot); // open the details modal on a storage slot

        // HOME-style rectangle select + block carry (see moveMon below)
        int paneCols(int pane) const;    // grid columns for a pane (see UI::boxGridColumns)
        int paneSlots(int pane) const;   // slots per box in that pane
        int paneBoxes(int pane) const;   // box count in that pane
        void storagePickup();            // the A press: anchor / grab / put down, whichever applies
        void pickupSingle();             // Move mode: lift the slot under the cursor as a 1x1 block
        void pickupMulti();              // Multi mode: anchor the rectangle, or grab it if already anchoring
        void grabSelection(bool remove); // lift (or copy, when !remove) the anchored rectangle into moveMon
        void scrunchSelection();         // trim all-empty edge rows/columns off the grabbed block
        void postPickup();               // drop an all-empty block so the hands read as free
        bool checkPutDownBounds() const; // does the block fit in the focused pane from the cursor cell?
        void putDownBlock();             // exact-slot placement: cell (x,y) -> cursor slot + x + y*cols
        void cancelSelection();          // abandon an in-progress rectangle

        // would placing pk into destPane run an LGPE (AV/EV-reset) conversion?
        bool lgpeConversionInvolved(int destPane, const Pokemon::Pokemon *pokemon) const;
        // does any carried pokemon trigger an LGPE conversion into destPane?
        bool blockInvolvesLgpe(int destPane) const;
        // would placing pk into destPane convert it DOWN into Gen 3 (destructive PID rebuild)?
        bool gen3DowngradeInvolved(int destPane, const Pokemon::Pokemon *pokemon) const;
        // does any carried pokemon trigger a Gen 3 downgrade into destPane?
        bool blockInvolvesGen3Downgrade(int destPane) const;
        // would placing pk into destPane run it through Poke Transporter (Gen 1/2 -> later)?
        bool virtualConsoleTransferInvolved(int destPane, const Pokemon::Pokemon *pokemon) const;
        // does any carried pokemon trigger a Poke Transporter transfer into destPane?
        bool blockInvolvesVirtualConsoleTransfer(int destPane) const;
        // the Gen 1/2 title such a transfer will stamp (Invalid when none is pending)
        Enums::GameVersion pendingTransferOriginVersion() const;
        /// True when the pending Gen 3 move would land a Pokemon whose OT name the destination
        /// cannot spell, so the confirm dialog can say so. See the implementation.
        bool pendingMoveDropsOriginalTrainerName() const;
        bool pendingTransferOriginIsGuess() const; // ...and whether that title is a representative rather than a fact
        Pokemon::Pokemon *detailsTargetPokemon();  // resolve the editor's current target (party/box/bank)
        void mirrorEditedPartyMember(); // after an edit, keep an LGPE party member's box/party copies in sync
        void snapshotEditTarget(); // capture the target's bytes as the save baseline (modal open + X = Save); revert
                                   // restores to it
        // has the details target changed vs the snapshot? (drives the "Unsaved changes" marker)
        bool pokemonEditDirty();
        void restoreEditTarget(); // roll the details target back to the snapshot -- discards edits when the page closes
                                  // without Save
        void closeDetailsModal(); // reset all details-modal / edit state and close it
        std::vector<int> visibleItemIndices() const; // raw indices in the current pouch worth showing (count > 0)
        int currentPouchCapacity() const;            // slot limit of the current pouch; 0 = appending unsupported
        void sortStorageBox(int pane, int box);      // pack + order one box, pinning party-linked slots
        int currentItemMaxCount() const;             // per-stack ceiling for the current pouch (never 0)

        // Public state - accessible by UI components (Panels, Dialogs, Modals)
        Trainer::Trainer &trainer;
        std::string titleName;
        std::string backupDir;
        std::string gameVersion; // Actual game version from NACP (e.g., "1.0.1", "1.3.2")
        u64 titleId;
        AccountUid userUid;
        bool goBack = false;
        bool exitRequested = false; // True when user presses + to close app
        ControllerNavigation controllerNavigation;

        // This block is public + mutable BY DESIGN: the panels/dialogs/modals read and write it
        // directly (immediate-mode UI). The biggest cohesive clusters are grouped into nested structs
        // (statEdit / creator / details); the rest are flat single-purpose flags + indices, each
        // default-initialised in place so the constructor only wires up the ctor arguments.

        ViewMode selectedMode = ViewMode::Party;
        int currentPage = 0;
        int selectedCategory = 0;      // For Items mode: selected PouchType
        // WHICH SAVE BOX IS OPEN. ONE cursor, shared by the Boxes view and the Storage view's save pane,
        // because it is one question: "which box of this save is the user looking at". A second copy reads
        // as intermittent -- change box in one view and the other stays where it was. Seeded in the
        // constructor from the game's own persisted current box and written back to it in update(), so it
        // also survives closing the editor.
        int selectedBoxIndex = 0;
        int selectedPartyIndex = 0;    // For Party mode: selected party Pokemon (0-5)
        bool detailViewActive = false; // True when detail panel is active (for Items/Boxes)
        int selectedItemIndex = 0;     // Selected item/pokemon index in detail view (item for Items, slot for Boxes)

        // HOME main menu focus (shown when NOT entered). 0 Pokemon(Boxes), 1 Party, 2 Storage (pills); 3
        // Items, 4 Trainer, 5 Settings (circular icons).
        int homeMenuIndex = 0;

        // Selected row in the Settings view (0-4); reached from the menu's Settings icon.
        int settingsSelectedRow = 0;

        // Trainer info view: the focused editable row (0 Name, 1 Money) and a
        // one-frame-deferred edit request. Name/Money open the blocking swkbd; deferring one frame lets
        // the row highlight render before the applet suspends the app (same trick as pendingHeaderRename).
        // -1 = nothing pending.
        int trainerSelectedRow = 0;
        int pendingTrainerEdit = -1;
        void editTrainerName();  // swkbd edit of the OT name (charset-validated, per-game length cap)
        void editTrainerMoney(); // numpad edit of money (clamped to the game's max)
        // After a name edit, re-stamp the trainer identity your Pokemon store so they stay
        // recognized as yours (party + boxes; NOT the cross-game bank). Both the OT name AND the
        // Gen 7+ handler (HT) name are part of the identity the games match on, so a bare trainer
        // edit would make your caught mons read as traded (obedience / traded-EXP) and your traded-in
        // mons lose you as their handler. Matches OT by ID32 + the carried OT name, and HT by the carried
        // HT name (the HT format has no TID/SID). `caughtName` is the pre-rename name for a rename, the
        // unchanged name otherwise; genuinely foreign stamps are untouched. Returns count changed.
        int restampCaughtPokemonIdentity(const std::u16string &caughtName);

        // Box swap (Phase 3 3.1): press Y to grab the slot under the cursor, then Y on another
        // occupied slot to swap them. swapSourceBox/Slot record the grabbed slot.
        bool swapActive = false;
        int swapSourceBox = -1;
        int swapSourceSlot = -1;

        // A tap on a box-name pill sets this instead of opening the rename immediately, so the header
        // highlight draws one frame BEFORE the blocking keyboard applet (otherwise the selection only
        // appears after the dialog closes). Consumed at the top of the next update().
        bool pendingHeaderRename = false;

        std::unique_ptr<Trainer::Bank> bank;      // created in the ctor
        CursorMode cursorMode = CursorMode::Menu; // default red (menu); cycled with Y
        int storageFocusPane = 0;                 // 0 = save (left), 1 = bank (right)
        int stSaveSlot = 0;                       // cursor slot in the save pane (its BOX is selectedBoxIndex)
        int stBankBox = 0, stBankSlot = 0;        // cursor in the bank pane; the box mirrors Bank::currentBox
        std::string storageStatus;                // transient status line (e.g. a refused cross-game drop)
        int storageStatusFrames = 0;              // frames left to show storageStatus (counts down)
        // Post a message to that line. This is PKSE's only way to tell the user anything: there is no
        // console on the Switch, and logErrorToFile writes somewhere they cannot read while the app is
        // running. Anything a user needs to know about MUST come through here, not just the log.
        void postStatus(const std::string &message, int frames = 240)
        {
            storageStatus = message;
            storageStatusFrames = frames;
        }
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
        bool currentlySelecting = false;   // Multi mode: a rectangle is being dragged out
        int selectPane = 0, selectBox = 0; // which pane/box the in-progress rectangle lives in
        // Origin of the block's TOP-LEFT cell, so B can put the whole thing back where it came from.
        int heldPane = 0, heldFromBox = 0, heldFromSlot = 0;
        bool carrying() const { return !moveMon.empty(); }
        int carriedCount() const;                     // non-null cells in moveMon
        const Pokemon::Pokemon *firstCarried() const; // first non-null cell, or nullptr

        // Red per-Pokemon action menu. These rows are the ONE list of it: the panel draws
        // STORAGE_MENU_LABELS, and the action handler, the greyed-row mask and the starting row all
        // name a row by its constant. A second list of row numbers is what lets a new row leave the
        // handler acting on its neighbour.
        static constexpr int STORAGE_MENU_ROW_MOVE = 0;
        static constexpr int STORAGE_MENU_ROW_EDIT = 1;
        static constexpr int STORAGE_MENU_ROW_CLONE = 2;
        static constexpr int STORAGE_MENU_ROW_EXPORT = 3;
        static constexpr int STORAGE_MENU_ROW_RELEASE = 4;
        static constexpr int STORAGE_MENU_ROW_FIND = 5;
        static constexpr int STORAGE_MENU_ROW_CANCEL = 6;
        static constexpr int STORAGE_MENU_ROW_COUNT = 7;
        static constexpr const char *STORAGE_MENU_LABELS[STORAGE_MENU_ROW_COUNT] = {
            "Move", "Edit", "Clone", "Export", "Release", "Find\u2026", "Cancel"};
        /// Rows a party-linked (Let's Go) slot cannot use: it can be edited, cloned and exported,
        /// but not moved or released.
        static constexpr bool storageMenuRowNeedsUnlockedSlot(int menuRow)
        {
            return menuRow == STORAGE_MENU_ROW_MOVE || menuRow == STORAGE_MENU_ROW_RELEASE;
        }
        bool storageMenuActive = false;
        int storageMenuIndex = 0;
        int menuPane = 0, menuBox = 0, menuSlot = 0; // the slot the menu acts on
        // Group menu for the carried block (Release all / Return to origin / Cancel), opened with Minus.
        bool groupMenuActive = false;
        int groupMenuIndex = 0;
        // Release confirmation (single slot, or the whole carried block when releaseGroup).
        bool releaseConfirmActive = false;
        int releasePane = 0, releaseBox = 0, releaseSlot = 0;
        bool releaseGroup = false;
        // Leaving the storage view with unsaved bank changes: prompt Save / Discard / Cancel (HOME-style).
        bool storageExitConfirmActive = false;
        int storageExitConfirmIndex = 0; // 0=Save & Exit, 1=Discard & Exit, 2=Cancel
        // Set when + (exit app) raised that prompt instead of B. Closing the app is not an answer to
        // "save the bank?", so + asks first and the exit resumes once Save or Discard has been picked.
        bool exitAfterBankChoice = false;
        void beginAppExit(); // the + handler's tail: prompt about the game save, else leave
        // Touchable storage slot rects, captured during draw for tap hit-testing next frame.
        struct TouchTarget
        {
            int pane, box, slot, hitX, hitY, hitWidth, hitHeight;
        };
        std::vector<TouchTarget> storageTouchTargets;
        // Touchable popup/dialog buttons (id = menu item index, or a dialog-specific id), captured
        // during draw and hit-tested next frame. Only the active overlay populates this.
        // Defined in UI/Common.h; the name is kept here so existing call sites read unchanged.
        using TouchButton = UI::TouchButton;
        std::vector<TouchButton> touchButtons;
        int touchedButtonId(const TouchInput &touch) const; // id of a tapped button, or -1
        void renameBox(int boxIndex);                       // swkbd rename of a SAVE box; no-op where unsupported
        void renameBankBox(int box);                        // swkbd rename of a BANK box (default label is "Bank N")

        // A three-step flow, each step owning input while it is up: browse for the .bnk, review what
        // it holds, then commit. The import lands in the IN-MEMORY bank only, so the existing
        // Save/Discard prompt on leaving Storage is what makes it permanent -- and Discard undoes
        // the whole import, which is why it needs no undo of its own.
        //
        // `fileBrowser` is declared below with the other file-browser state (it predates this and is
        // shared with the loose-save path); only the PKSM half is declared here.
        Dialogs::PKSMImportState pksmImport;
        void openPKSMImportBrowser();                                   // Minus: start at PKSM's usual folders
        void handlePKSMImportInput(u64 buttonsDown, const TouchInput &touch); // owns input while any step is up
        void scanChosenPKSMBank(const std::string &path); // read + parse the picked file, raise the next dialog
        void commitPKSMImport();                                        // move the staged Pokemon into the bank
        /// Completely empty bank boxes. The import fills whole boxes, so this is what decides
        /// whether a PKSM layout fits -- the preview warns with it before anything is written.
        size_t emptyBankBoxCount() const;
        /// True while any import overlay owns input (browser, preview or result).
        bool pksmImportActive() const noexcept
        {
            return fileBrowser.active || pksmImport.previewActive || pksmImport.resultActive;
        }

        // Picking a save file off the SD card. PKSE's normal flow finds saves by Switch title id,
        // which cannot reach a Game Boy .sav sitting in an emulator's folder, so opening one means
        // browsing to it. Generic on purpose: every pre-Switch generation needs the same thing.
        /// WHAT THE FILE BROWSER WAS OPENED FOR. One browser, two callers -- the PKSM bank import
        /// and the trade-partner picker -- and the activate handler has to know which, or picking a
        /// save would try to parse it as a bank.
        enum class FileBrowserPurpose : uint8_t
        {
            PKSMBank = 0,
            TradePartner,
        };
        FileBrowserPurpose fileBrowserPurpose = FileBrowserPurpose::PKSMBank;

        /// The last trade-partner pick, for the details row to report. Empty text means the row
        /// shows the handler the Pokemon actually carries.
        struct TradePartnerPickState
        {
            std::string refusalText;  ///< short reason the chosen save was refused, or empty
            std::string acceptedName; ///< the trainer just stamped, for one frame of feedback
        };
        TradePartnerPickState tradePartnerPick;

        /**
         * A save on this console the trade-partner picker may offer.
         *
         * HANDED IN RATHER THAN ENUMERATED HERE. Listing save data needs fs/ns and is the save
         * picker's job; it has already done it by the time the editor opens, so UIManager passes
         * the result along instead of a second screen learning to walk the console.
         */
        struct ConsoleSaveEntry
        {
            u64 titleId = 0;
            AccountUid accountUid{};
            std::string label;    ///< "Shield"
            std::string userName; ///< whose save it is -- two users may own the same title
            std::string folder;   ///< the backup directory name, which carries the title id
            Enums::GameVersion gameVersion = Enums::GameVersion::Invalid;
        };
        void setConsoleSaves(std::vector<ConsoleSaveEntry> saves) { consoleSaves = std::move(saves); }

        /// The trade-partner title picker: the console saves that could be the other side of a
        /// trade for the Pokemon being evolved, filtered by Trainer::titleCanTradeWith.
        struct TradePartnerTitleState
        {
            bool active = false;
            int selectedIndex = 0;
            int scroll = 0;
            std::vector<ConsoleSaveEntry> candidates;
        };
        TradePartnerTitleState tradePartnerTitles;

        void openTradePartnerTitlePicker();
        void tradeEvolveWithChosenTitle(const ConsoleSaveEntry &entry);

        /// "Trade Evolve" asked, and the format keeps a handling trainer -- so the choice between a
        /// real save and a generated trainer is open. False for Gens 1-5, which never ask.
        bool tradeEvolveConfirmActive = false;

        /// "Trade Evolve" asked, and the record does not say which of two destinations it means --
        /// Clamperl holding neither of its items, and nothing else in any game.
        bool tradeEvolveChoiceActive = false;
        /// The highlighted row of that choice.
        int tradeEvolveChoiceIndex = 0;
        /// Which candidate of the offer the open trade-evolve flow is performing. The offer is
        /// re-read from the record at every step, so the INDEX is all that has to survive between
        /// the question being settled and the evolution being applied.
        int tradeEvolveCandidateIndex = 0;
        /// Settles a Trade Evolve press once the destination is known: raises the handler choice
        /// for a format that keeps one, and evolves outright for a format that does not.
        void beginTradeEvolve(int candidateIndex);

        void openTradePartnerBrowser();
        void tradeEvolveWithChosenSave(const std::string &path);
        void tradeEvolveWithGeneratedTrainer();
        /// Trade first, THEN evolve -- the order the games do it in, and the order that decides
        /// whose base friendship the new handler starts on.
        void finishTradeEvolve(Pokemon::Pokemon &pokemon, const Pokemon::TradePartner &partner);

        std::vector<ConsoleSaveEntry> consoleSaves;
        Dialogs::FileBrowserState fileBrowser;

        bool itemEditDialogActive = false;   // True when editing an item's amount
        int itemEditDialogValue = 0;         // Current value being edited
        int itemEditDialogOriginalValue = 0; // Original value before editing
        // Items list: Y asks before removing the selected item. A in this dialog does the delete,
        // B cancels. Only reachable from the Items view, so it never lets X-to-save fire (home-menu only).
        bool itemRemoveConfirmActive = false;

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
        /// Which item-id space the pouch picker should offer. Asked in one place because the
        /// answer is per generation and there are now three of them -- a ternary at each call site
        /// was already the wrong shape, and the id spaces name each other's items without erroring.
        Dialogs::PickerKind pouchPickerKind() const; // defined in the .cpp: Trainer is incomplete here

        enum SaveDestination
        {
            DestinationThisBackup = 0,
            DestinationNewBackup = 1,
            DestinationGameSave = 2,
            DestinationExternalFile = 3
        };
        bool loadedFromCart = false;
        /// True when this session came from a loose file the user browsed to rather than an
        /// installed title's save. The three normal destinations are all meaningless then -- there
        /// is no backup tree, no live game save, and nothing to inject into -- so the picker
        /// collapses to the only answer there is: write the file back where it came from.
        bool externalFileSession = false;

        /// Cursor into the VISIBLE rows, not a SaveDestination -- the two stopped being interchangeable
        /// once a title session dropped a row from the middle of the enum. Map with saveDestinationAt().
        int saveDestinationIndex = 0;
        int saveDestinationCount() const
        {
            return externalFileSession ? 1 : (loadedFromCart ? 2 : (g_injectToGameSave ? 3 : 2));
        }
        /// Which destination a visible row means. Title sessions lead with the game save because
        /// it is both the default and the point of the session; backup sessions keep the original
        /// order, where row 0 is the backup already open.
        SaveDestination saveDestinationAt(int row) const
        {
            if (externalFileSession)
                return DestinationExternalFile;
            if (loadedFromCart)
                return row == 0 ? DestinationGameSave : DestinationNewBackup;
            return static_cast<SaveDestination>(row);
        }
        /// The row the save dialog opens on -- the first one, in both modes.
        int defaultSaveDestinationRow() const { return 0; }

        /// Set once a value outside the games' own limits has actually been committed this session
        /// (EV > 252, AV > 200, or an EV total over 510 — only reachable with "Allow illegal values"
        /// on). The save dialog then carries a warning line. Entirely separate from the destination
        /// logic: it is about what is being written, not where.
        bool illegalDataWritten = false;

        // Create sdmc:/PKSE/{title}/{name}/ seeded with a copy of the current backup, suffixing
        // -2, -3... if the name is taken. Returns the new path, or "" on failure.
        std::string createNamedBackupDir(const std::string &name);
        // Last gate before overwriting the player's real save data. Reached only by choosing the
        // "Game save" destination, which is itself only offered when the Settings lock is on.
        bool saveInjectConfirmActive = false;
        // Write to destDir, optionally injecting into the game. Shared by every destination so the
        // success/failure handling can't drift between them.
        void performSave(const std::string &destDir, bool injectToTitle);
        bool hasUnsavedChanges = false;
        bool exitingWithUnsavedChanges = false;
        bool exitingViaPlus = false; // True when exiting via + button (exit app) vs B button (go back)

        // Stat editor (IV / EV / AV + shiny). Original* is the value on dialog entry; Current* is the
        // in-progress edit, preserved when switching between the IV/EV/AV modes.
        struct StatEditState
        {
            bool dialogActive = false;
            int selectedStat = 0; // 0-5: HP, ATK, DEF, SPE, SPA, SPD
            Dialogs::StatEditMode mode = Dialogs::StatEditMode::IV;
            int value = 0;                                      // current edit (the active IV/EV/AV)
            int originalIV = 0, originalEV = 0, originalAV = 0; // on entry (AV = Let's Go)
            int currentIV = 0, currentEV = 0, currentAV = 0;    // edits, kept across mode switches
        };
        StatEditState statEdit;

        // Pokemon details modal (the HOME "Check Summary" editor page): open/target, cursor, overlays,
        // hexagon mode, the left-pane scroll + nav list, and the edit baseline that drives the top-bar
        // "Unsaved changes" marker (snapshot on open, re-taken by X = Save; empty when not editing).
        struct DetailsState
        {
            bool active = false;
            EditSource source = EditSource::Box; // where the edited Pokemon lives
            int bankBox = 0, bankSlot = 0;       // bank target (EditSource::Bank)
            int partyIndex = 0;                  // party slot (0-5) when editing a party pokemon
            int category = 0;                    // 0=Main,1=Met,2=Stats,3=Moves,4=Cosmetic,5=OT/Misc
            int selectedStat = 0;                // Stats category: which stat is selected
            int selectedField = 0;               // center column: selected field (see the modal draw)
            bool editing = false;                // editing a stat value / main field
            int hexMode = 0;                     // hexagon (Y-cycled): 0 Summary, 1 Base Points (EV/AV), 2 Judge (IVs)
            bool legalityOverlay = false;        // full legality issue list (Y / tap)
            bool ribbonOverlay = false;          // full ribbon/mark list (X / tap)
            /// "You have unsaved changes" prompt, raised when the page is closed while pokemonEditDirty().
            /// Rolling straight back to the snapshot on close loses the edits with nothing said -- the top-
            /// bar marker is the only clue, and it disappears along with the page.
            bool discardConfirmActive = false;
            int lastCenterField = 0;             // remembered center-column row when hopping to/from moves
            int leftScroll = 0; // left-pane vertical scroll (px); auto-follows selection, reset on (re)open
            std::vector<int> leftOrder;          // left-pane editable field ids in DRAW order (rebuilt each draw)
            std::vector<std::byte> editSnapshot; // baseline bytes for the "Unsaved changes" marker + revert
        };
        DetailsState details;
        // Creator: the Species picker was opened in "create a new pokemon" mode; where the built pokemon lands,
        // plus the not-yet-accepted-pokemon editing flow (Keep/Discard on exit).
        struct CreatorState
        {
            bool active = false;             // Species picker is in create-a-new-pokemon mode
            int pane = 0, box = 0, slot = 0; // where the built pokemon is dropped
            bool editing = false;            // details modal is on a just-created, not-yet-accepted pokemon
            bool keepConfirmActive = false;  // "Keep this new Pokemon?" prompt shown on exit
            int keepConfirmIndex = 1;        // cursor: 0 = Discard, 1 = Keep (default)
        };
        CreatorState creator;
        // Lossy-move acknowledgements. THREE independent warnings about three unrelated losses, kept
        // apart because they are not the same question, but ALL gated by the one Move warning
        // setting -- it covers every bank transfer warning in every generation:
        //
        //   Gen 3 down-convert  -- rebuilds the PID to preserve nature, and drops nickname/ribbons/held
        //                          item. Destructive and irreversible.
        //   Transporter run     -- a Gen 1/2 pokemon leaving for a later generation. Rewrites IVs, PID,
        //                          EXP, ability and sometimes the nickname, and cannot be undone.
        //   Let's Go transfer   -- resets AV/EV training to 0 and drops unlearnable moves. Recoverable
        //                          by re-training.
        //
        // Exempting the destructive two would make the setting say less than its label: someone who had
        // turned warnings off would still be stopped on every FireRed drop.
        //
        // A move can match more than one (an LGPE pokemon into FireRed). The most severe notice
        // supersedes -- stacking dialogs on one action reads as a bug.
        enum class PendingMove
        {
            None,
            PlaceHeld
        };
        bool gen3ConvertConfirmActive = false;            // "Convert to Gen 3?"        -- gated by g_moveWarn
        bool virtualConsoleTransferConfirmActive = false; // "Transfer out of Gen 1/2?" -- gated by g_moveWarn
        bool lgpeTransferConfirmActive = false;           // "Let's Go transfer"        -- gated by g_moveWarn
        int moveConfirmIndex = 1;                         // cursor: 0 = Cancel, 1 = Continue (default)
        // The action all three warnings guard is the same one, so the stash they resume is shared.
        PendingMove pendingMove = PendingMove::None;
        int pendingMovePane = 0, pendingMoveBox = 0, pendingMoveSlot = 0;
        bool moveConfirmActive() const noexcept
        {
            return gen3ConvertConfirmActive || virtualConsoleTransferConfirmActive || lgpeTransferConfirmActive;
        }

        /// The nav-bar hints for whichever OVERLAY owns input right now, or "" when none does.
        ///
        /// Two things draw the nav bar -- this screen, and the full-screen details page, which paints
        /// over it -- so the hints for a dialog that can appear over either have to live in one place
        /// or the two disagree. They did: a picker opened from the details page left the page's own
        /// controls on the bar while the picker had input, and the picker printed a second strip
        /// inside its card to compensate. Both callers ask this first now.
        std::string overlayNavHint() const;

        // Reusable selection panel (picker) for choosing a value from a list — nature, gender, move.
        bool pickerActive = false;
        Dialogs::PickerKind pickerKind = Dialogs::PickerKind::Nature;
        int pickerSlot = 0;  // which of the 4 move slots (when pickerKind == Move)
        int pickerSel = 0;   // highlighted option index
        int pickerCount = 0; // number of options in the list
        // A pouch-item picker opened to CHANGE an existing item's type (Potion -> Super Potion),
        // not to add a new one. Changes the confirm behavior + the picker title; reuses PouchItem.
        bool itemPickerReplace = false;
        // Ability picker: reordered option list so the species' legal abilities sort to the top
        // and render green. pickerOrder[row] = ability id at that row; rows 0..pickerLegalCount-1 are
        // the legal abilities. Empty for every other picker kind (which stay identity-indexed: row == value).
        std::vector<int> pickerOrder;
        int pickerLegalCount = 0;
        // Met-location picker: the origin version whose location table is shown, so the picker can
        // resolve each id in pickerOrder to a name (a location id names a different place per game).
        uint8_t pickerMetVersion = 0;
        // Form picker: the species whose forms are being listed, so the picker can name each form.
        uint16_t pickerFormSpecies = 0;
        // Location picker mode: true routes the pick to the egg-met location, false to the met location.
        bool pickerMetIsEgg = false;

        // A tap records the button it stands for and RETURNS. The frames that follow draw the
        // selection, and the press fires once it has actually been on screen. Setting the selection and
        // synthesising A in the same frame runs the action before any frame is drawn with the row
        // highlighted: the user gets no confirmation of what they hit, and on a glyph confirm (Cancel /
        // Release) nothing moves at all -- the dialog simply vanishes. One shape for every dialog; the
        // same deferral the keyboard-backed edits use.
        u64 pendingTapButton = 0;
        /// The touch-button id the tap landed on, so a confirm can draw it held while it waits.
        /// -1 when nothing is pending. drawEditChoiceButton() reads this.
        int pendingTapButtonId = -1;
        /// Frames left before the pending press fires.
        int pendingTapFrames = 0;
        /// How long a tapped button is drawn HELD before its action runs. One frame is ~16ms, which
        /// is under the threshold where a change reads as a change rather than a flicker -- the
        /// user would still be told nothing about what they hit. Four frames (~66ms) is long enough
        /// to see and short enough that the dialog does not feel like it is lagging the touch.
        static constexpr int TAP_PRESS_FRAMES = 4;

        /// Records a tap to fire once its press has been seen. Returns true so a handler can read
        /// `if (armTap(...)) return;`.
        bool armTap(int buttonId, u64 button);

        //
        // `pickerSel` indexes the VISIBLE rows, which are the filtered ones whenever a query is
        // set. Nothing may read it as an index into the full list: with "nurse" typed, visible row
        // 0 is some row far down the real one, and applying `pickerSel` directly would write
        // whatever value happens to sit at position 0 instead. Every apply site goes through
        // pickerSelectedValue().
        ListSearch pickerSearch;

        /// The items view's search. Applied inside visibleItemIndices(), so every
        /// consumer of the item list sees the same filtered rows.
        ListSearch itemSearch;

        /// The Box/Bank search.
        ///
        /// A box is a GRID, not a list, so this does not filter -- hiding slots would move every
        /// Pokemon the user arranged deliberately, and storage here is positional. It HIGHLIGHTS
        /// instead: matching slots keep an accent ring, the rest dim, and the cursor jumps to the
        /// first match. Nothing in the save changes either way.
        ListSearch storageSearch;

        /// Does the Pokemon in this slot match the storage query? False for an empty slot, and
        /// true for everything when no query is set. Non-const because storageSlot() hands out a
        /// mutable reference; nothing here writes through it.
        bool storageSlotMatchesSearch(int pane, int box, int slot);
        /// Moves the cursor to the first matching slot, searching the focused pane from the
        /// current box onward and then the other pane. Returns false when nothing matches.
        bool jumpToFirstStorageMatch();
        /// Unfiltered row indices that survive the query, in order. Empty and unused when the
        /// query is empty -- "no filter" then costs nothing and takes the same code path.
        std::vector<int> pickerFilteredRows;

        /// Recomputes pickerFilteredRows from the current query and re-clamps pickerSel, keeping
        /// the previously selected VALUE selected when it survives the filter.
        void rebuildPickerFilter(int previouslySelectedValue = -1);
        /// Rows the user can currently see and move between.
        int pickerVisibleCount() const;
        /// Visible row -> row in the unfiltered list.
        int pickerRowForVisibleIndex(int visibleIndex) const;
        /// The VALUE the cursor is on, with pickerOrder applied where the kind reorders.
        int pickerSelectedValue() const;
        /// True for the kinds whose rows are indirected through pickerOrder.
        bool pickerUsesOrder() const;
        /// Drops any query, so each picker opens unfiltered. Called wherever a picker is opened.
        void clearPickerSearch();
    };
}

#endif
