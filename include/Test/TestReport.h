/**
 * The self test exists because verifying PKSE by hand means booting the .nro, opening every save,
 * and clicking through party, boxes, items, trainer info, the details modal, the legality panel,
 * the bank and the storage view -- per game, after every change. That is hours, it is boring, and
 * boring is exactly when a wrong byte gets waved through. The framework does the clicking; a human
 * reads the report.
 *
 * Two things shape this file:
 *
 * 1. FINDINGS ARE GROUPED, NOT STREAMED. A single wrong table produces one failure per species, and
 *    a report that is 1025 near-identical lines is a report nobody reads. Occurrences of the same
 *    (suite, what) collapse into one FindingGroup carrying a count plus the first `MAX_SAMPLE_COUNT`
 *    detail lines. "entity/stored stats do not match recalculation x342 (first 25 shown)" says the
 *    same thing in one line and stays bounded when the build is badly broken -- which is precisely
 *    when the report has to survive being written.
 *
 * 2. A PASSING CHECK IS COUNTED, NOT LOGGED. "0 failures" only means something next to how much was
 *    actually checked, so every check increments a counter whether or not it fires. A suite that
 *    silently checked nothing shows up as a suspiciously small number rather than as a clean pass.
 *
 * The report file itself is written UNCONDITIONALLY, unlike every other sink in PKSE (which is gated
 * on g_debugLogging so a normal run leaves nothing on the card). Producing the report IS the action
 * the user asked for here -- suppressing it would be like a save that writes no file -- and it is
 * only ever written when the self test is deliberately started from the picker.
 */
#ifndef TEST_TEST_REPORT_H
#define TEST_TEST_REPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "Enums/GameVersion.h"
#include "Utils/NXTypes.h" // AccountUid/u64 -- <switch.h> on console, plain typedefs off it

namespace Trainer
{
    class Trainer;
}
namespace Pokemon
{
    class Pokemon;
}

namespace UI
{
    class PKSEFramebuffer;
    class UIScreen;
}

namespace Test
{

    /// Fail  = PKSE is wrong; something that should hold does not.
    /// Warn  = suspicious but not provably wrong.
    /// Skip  = a gap in what could be checked (a missing save, a field a format cannot hold, a suite
    ///         with nothing to run). Its own severity, NOT a kind of warning and NOT a check: a skip
    ///         counted as a check would let "everything passed" quietly mean "everything I bothered
    ///         to look at passed". Skips are listed in the report and on the results screen.
    /// Info  = a fact worth having in the report that is not a problem (which encounter matched,
    ///         how many bytes a save legitimately rewrote).
    enum class Severity : uint8_t
    {
        Info,
        Skip,
        Warn,
        Fail
    };

    const char *severityName(Severity severity);

    /// One CLASS of finding -- a (suite, what) pair -- with a bounded sample of the occurrences.
    struct FindingGroup
    {
        Severity severity = Severity::Info;
        std::string suite;
        std::string what;
        int count = 0;                    // every occurrence, including ones past the sample cap
        std::vector<std::string> samples; // "<target>  <detail>", capped at MAX_SAMPLE_COUNT
    };

    /// Per (target, suite) roll-up, so the report can open with a table saying which generation and
    /// which suite the failures are in before anyone reads a single detail line.
    struct Tally
    {
        std::string target;
        std::string suite;
        int checks = 0, fails = 0, warns = 0, skips = 0;
    };

    class Report
    {
    public:
        /// Detail lines kept per finding group. Past this the group still COUNTS occurrences, it
        /// just stops storing their text.
        static constexpr int MAX_SAMPLE_COUNT = 25;
        /// Finding lines mirrored for the on-screen ticker (a ring, oldest dropped).
        static constexpr int RECENT_LINE_COUNT = 14;

        void reset();

        /// Which (target, suite) the checks that follow belong to. Set by the runner before each
        /// chunk; everything recorded until the next call lands in this bucket.
        void setScope(const std::string &target, const std::string &suite);

        void pass(); // a check that held
        void record(Severity severity, const std::string &what, const std::string &detail);

        /// A line of narrative for the report's TRACE section: what the run DID, in order, as
        /// opposed to what it found. The UI suites use it to log every state change each simulated
        /// button press caused, so a reader can see that a change was expected rather than take the
        /// absence of a failure on trust. Written to the report unconditionally, like the rest of
        /// it; the unabridged version also goes to sdmc:/PKSE/trace.log when debug logging is on.
        void addTrace(const std::string &line);
        const std::vector<std::string> &traces() const noexcept { return traceLines; }
        /// Past this the section stops growing. A journey that logged every frame would bury the
        /// findings under itself, and the findings are what the report is for.
        static constexpr int MAX_TRACE_LINES = 500;

        int checks() const noexcept { return checkCount; }
        int failures() const noexcept { return failCount; }
        int warnings() const noexcept { return warnCount; }
        int skips() const noexcept { return skipCount; }
        int notes() const noexcept { return infoCount; }

        const std::vector<FindingGroup> &groups() const noexcept { return findingGroups; }
        const std::vector<Tally> &tallies() const noexcept { return tallyList; }
        const std::vector<std::string> &recent() const noexcept { return recentLines; }

        /// Writes the full report. `header` is the run banner (time, version, chosen targets). The
        /// runner also calls it after every target, so a run that crashes part way still leaves
        /// the report up to the last finished target on the card. Returns false only if the file
        /// could not be created.
        bool write(const std::string &path, const std::string &header) const;

    private:
        Tally &currentTally();

        std::string scopeTarget, scopeSuite;
        int checkCount = 0, failCount = 0, warnCount = 0, skipCount = 0, infoCount = 0;
        std::vector<FindingGroup> findingGroups;
        std::vector<Tally> tallyList;
        std::vector<std::string> recentLines;
        std::vector<std::string> traceLines;
        bool traceTruncated = false;
    };

    /**
     * What a check function is handed: where it is, what it is looking at, and how to say what it
     * found. Deliberately a plain struct with a Report POINTER rather than a reference -- the runner
     * rebuilds one of these per chunk, and a reference member would make that an assignment error
     * for no benefit.
     */
    struct Context
    {
        Report *rep = nullptr;
        std::string suite;
        std::string target;                                       // "Sword"
        Enums::GameVersion version = Enums::GameVersion::Invalid; // the concrete title (SW, VL, YW)
        Enums::GameVersion group = Enums::GameVersion::Invalid;   // save-format group (SWSH, RBY)
        Trainer::Trainer *trainer = nullptr;                    // null for suites that need no save
        /// The run's writable scratch directory. For a per-target suite it holds the scratch
        /// COPY of the save; for a Global one, which has no save, it is the run's work root --
        /// somewhere a suite may create its own fixtures and clean them up again.
        std::string workDir;
        /// For a LOOSE target, the exact scratch file to read and write; empty for an installed
        /// title, whose save is addressed by `workDir` + title id instead.
        ///
        /// Carried rather than searched for: the largest regular file in `workDir` is not
        /// unambiguous, because Silver and Crystal share `work/gsc/`, Ruby and Emerald share
        /// `work/rse/`, and the bank suite drops a `bank.dat` in there too. The runner already
        /// knows the path it copied to, so it says.
        std::string savePath;
        /// The installed title this save came from, or 0 for a loose file. Needed because the save
        /// WRITE path dispatches on the title id, not on the trainer -- so a suite that re-saves has
        /// to carry it. The uid rides along for the same call's signature; it is only read when
        /// injecting into the live game save, which the self test never does.
        uint64_t titleId = 0;
        AccountUid accountUid{};
        /// The live framebuffer, for suites that DRIVE the UI and must let it actually render.
        /// Null when nothing is drawing (the host harness, or before the first frame), in which
        /// case a UI suite checks state only. Drawing matters on hardware for its own sake: it is
        /// where ROMFS sprite loads, NanoVG text and GL calls happen, so a screen that crashes in
        /// draw() on a console cannot be caught any other way.
        UI::PKSEFramebuffer *framebuffer = nullptr;

        /// Records a Fail when `ok` is false. RETURNS `ok`, so a check that guards later work reads
        /// as `if (!context.check(p != nullptr, "...")) return;` instead of testing the same thing twice.
        bool check(bool passed, const std::string &what, const std::string &detail = std::string());
        /// As check(), but a miss is a Warn -- for things that are suspicious rather than wrong.
        bool warn(bool passed, const std::string &what, const std::string &detail = std::string());
        void fail(const std::string &what, const std::string &detail = std::string());
        void note(const std::string &what, const std::string &detail = std::string());
        /// A COVERAGE gap: something this run could not check at all. Recorded as a Skip, which is
        /// listed beside the failures and warnings and never counted as a check -- an unchecked
        /// thing must never be mistaken for a checked one.
        void skip(const std::string &what, const std::string &detail = std::string());
    };

    // Detail lines are `KEY=value` pairs in the same spelling the [EVENT] log uses (Utils::logField),
    // so a finding and the action log that produced it read the same way and grep the same way.

    /// `key=<n>`
    std::string numberField(const char *key, long long value);
    /// `key=0xHH..` -- for offsets and ids, where decimal is the wrong base to read them in.
    std::string hex(const char *key, unsigned long long value, int digits = 0);
    /// `box=<b+1> slot=<s+1>` -- 1-based, matching what the UI shows. A report that counts from 0
    /// while the screen counts from 1 sends the reader to the wrong slot.
    std::string slot(int box, int slotIndex);
    /// `party=<i+1>`
    std::string partySlot(int index);
    /// Species / form / level / nickname for one pokemon, for the detail line of an entity finding.
    std::string monLabel(const Pokemon::Pokemon &pokemon);
}

#endif
