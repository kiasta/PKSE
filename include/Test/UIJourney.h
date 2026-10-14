/**
 * WHY THIS EXISTS. Every other suite calls functions directly; none of them presses a button. A view
 * you can enter but not leave, a cursor that stopped moving, a panel that crashes while drawing on a
 * console, an "innocent" navigation keypress that quietly rewrites a Pokemon -- none of that is
 * reachable by a test that never runs update() and draw().
 *
 * HOW IT WORKS. A screen receives input as `const PadState&` handed to it by the frame loop, and
 * libnx's button readers are plain inline reads over that struct. So a PadState built in memory is
 * not a mock: it is the same bytes the console hands over, and the screen cannot tell the difference.
 * The journey builds one, calls the real update(), then the real draw() against the LIVE
 * framebuffer, so the rendering path -- ROMFS sprite loads, NanoVG text, GL -- genuinely executes.
 *
 * THE CENTRAL IDEA: EVERY CHANGE IS DECLARED. Before each action the journey snapshots the app's
 * whole observable state -- view, every cursor, every modal flag, and fingerprints of the trainer,
 * party, boxes, items and box names. After the action it snapshots again and diffs. The action
 * declares which fields it is ALLOWED to change; anything else that moved is a finding.
 *
 * That inverts the usual bargain. A conventional UI test asserts the handful of things its author
 * thought to check and is blind to everything else, so a keypress that also corrupted a box would
 * pass. Here silence is meaningful: navigating the box view is declared to change navigation state
 * ONLY, so if it touches a single byte of Pokemon data the run says so without anyone having
 * predicted that particular bug.
 *
 * Every action's diff is written to the report's TRACE section (and to sdmc:/PKSE/trace.log when
 * debug logging is on), so the change log can be read directly rather than inferred from the
 * absence of failures.
 *
 * TOUCH is not driven. TouchInput reads the panel inside update() and its state is private, so a
 * synthetic tap would mean a test hook in shipped code. Everything in PKSE is reachable by button,
 * so the journeys drive buttons and the touch paths remain a manual check.
 */
#ifndef TEST_UI_JOURNEY_H
#define TEST_UI_JOURNEY_H

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "Test/TestReport.h"
#include "UI/TouchInput.h"
#include "UI/UIScreen.h"
#include "Utils/NXTypes.h"

namespace UI
{
    class TrainerViewScreen;
}
namespace Trainer
{
    class Trainer;
}

namespace Test
{

    /// Everything about the app a user could observe, in one comparable value.
    ///
    /// Navigation fields are stored individually because a finding has to NAME the field that moved.
    /// Data is stored as fingerprints instead: the point is only ever "did this change", and hashing
    /// a box is the difference between a snapshot costing a few hundred bytes and costing megabytes
    /// on a machine with 4 MB of heap to spare.
    struct UISnapshot
    {
        int mode = 0, homeIndex = 0, page = 0, category = 0;
        int box = 0, slot = 0, party = 0, settingsRow = 0, trainerRow = 0;
        bool detailView = false, detailsModal = false, saveConfirm = false;
        bool swap = false, storageMenu = false;
        int cursorMode = 0, focusPane = 0;
        // No stSaveBox: the Storage view's save pane and the Boxes view share one box cursor, which
        // `box` above already carries. Snapshotting it twice would report one move as two findings.
        int stSaveSlot = 0, stBankBox = 0, stBankSlot = 0;
        bool goBack = false, exitRequested = false, unsaved = false;
        uint64_t trainerFp = 0; // OT name + money
        uint64_t partyFp = 0;   // every party slot's bytes
        uint64_t boxFp = 0;     // every box slot's bytes
        uint64_t itemFp = 0;    // every pouch, every id + count
        uint64_t boxNameFp = 0; // all box names

        static UISnapshot take(const UI::TrainerViewScreen &screen, const Trainer::Trainer &trainer);
        /// Names of the fields that differ, in a fixed order so a diff reads the same every run.
        std::vector<const char *> diff(const UISnapshot &other) const;
    };

    /// The set of fields an action is permitted to change. Anything outside it is a finding.
    ///
    /// Groups rather than bare field names, because listing eleven storage cursor fields at every
    /// call site would make the journeys unreadable and the omissions invisible.
    enum Allow : uint32_t
    {
        AllowNothing = 0,
        AllowHome = 1u << 0,    // homeIndex + the mode it implies
        AllowView = 1u << 1,    // detailView, mode
        AllowCursor = 1u << 2,  // box/slot/party/page/category/settingsRow/trainerRow
        AllowStorage = 1u << 3, // the storage pane cursors, focus pane, cursor mode, its menu
        AllowModal = 1u << 4,   // details modal, save-confirm
        AllowSwap = 1u << 5,
        AllowExit = 1u << 6,    // goBack / exitRequested
        AllowTrainer = 1u << 7, // OT name / money
        AllowParty = 1u << 8,
        AllowBoxes = 1u << 9,
        AllowItems = 1u << 10,
        AllowBoxNames = 1u << 11,
        AllowUnsaved = 1u << 12, // the dirty flag (any real edit sets it)
        /// Any data change at all -- for the deliberately destructive steps at the end of a journey.
        AllowData = AllowTrainer | AllowParty | AllowBoxes | AllowItems | AllowBoxNames | AllowUnsaved,
        AllowNav = AllowHome | AllowView | AllowCursor | AllowStorage | AllowModal,
    };

    /// Drives one screen and holds it to its declared changes.
    class Journey
    {
    public:
        Journey(Context &context, UI::UIScreen &screen, const UI::TrainerViewScreen *tvs, const Trainer::Trainer *trainer, std::string name);

        /// One user action: press `button`, run a frame, then verify nothing outside `allow` moved.
        /// `what` is the human description that lands in the trace and in any finding.
        Journey &act(const char *what, u64 button, uint32_t allow, int presses = 1);
        /// A frame with no input -- for the one-frame-deferred edits the editor uses so a highlight
        /// renders before a blocking applet opens. Verifies and traces like act() does.
        Journey &settle(const char *what, uint32_t allow, int frames = 1);

        /// Pump frames WITHOUT snapshotting, verifying or tracing.
        ///
        /// For the holding frames between steps of a paced walkthrough. They exist so the previous
        /// action stays on screen long enough to read; verifying them would add a trace line and a
        /// full re-hash of every box in the save sixty times a second, burying the log under
        /// "(hold) -> changed: nothing" and doing real work to learn nothing.
        Journey &idle(int frames = 1);

        /// A plain assertion, reported with the journey and step for context.
        bool expect(bool passed, const std::string &what, const std::string &detail = std::string());

        int steps() const noexcept { return stepCount; }
        int findings() const noexcept { return findingCount; }

        /// Carry the "state before" across chunks.
        ///
        /// A paced walkthrough performs ONE action per frame, and the runner rebuilds the Context
        /// for every chunk -- so the Journey cannot outlive a chunk, but the snapshot it diffs
        /// against has to. The suite keeps the snapshot between chunks and hands it back. The pad
        /// deliberately does NOT need carrying: every action is a press frame followed by a release
        /// frame, so its edges are self-contained.
        const UISnapshot &snapshot() const noexcept { return before; }
        void restore(const UISnapshot &s)
        {
            before = s;
            haveBefore = true;
        }

        /// What the last verified action changed, and whether any of it was undeclared -- for the
        /// on-screen caption, so a watcher sees the diff as it happens rather than only in the report.
        const std::string &lastChanged() const noexcept { return lastChangedText; }
        /// Whether the last verified action changed `field` (a UISnapshot::diff name). The allow
        /// mask says what an action MAY change; this is how a step says what it MUST, so a button
        /// that does nothing at all cannot pass.
        bool lastChangedField(const char *field) const noexcept;
        bool lastWasBad() const noexcept { return lastBad; }

    private:
        void pump(u64 held);
        void verify(const char *what, uint32_t allow);
        std::string where() const;

        Context &context;
        UI::UIScreen &screen;
        const UI::TrainerViewScreen *tvs; // null when driving a screen with no snapshot support
        const Trainer::Trainer *trainer;
        std::string name;
        PadState pad{};
        UI::TouchInput touch;
        UISnapshot before;
        bool haveBefore = false;
        int stepCount = 0, findingCount = 0;
        std::string lastChangedText;
        std::vector<const char *> lastChangedFields;
        bool lastBad = false;
    };
}

#endif
