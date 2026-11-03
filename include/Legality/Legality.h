/**
 * analyze() is PKSE's port of PKHeX's LegalityAnalysis. It finds the verified encounter
 * (Legality/Encounters: PKHeX's EncounterFinder, with move verification from
 * Legality/LearnSource and evolution chains from Legality/Evolutions), then runs one
 * verifier per PKHeX verifier (Legality/Verifiers) in PKHeX's order behind PKHeX's format
 * gates. Every Issue carries PKHeX's CheckIdentifier and result code (ResultCode.h), and
 * severities match PKHeX: Invalid stays Invalid, Fishy becomes Warning. A Pokemon is legal
 * exactly when PKHeX would call it legal: no Invalid issue (Report::invalidCount() == 0).
 *
 * It stays INFORMATIONAL: PKSE never refuses or auto-fixes an edit because of a finding.
 * tools/harness/build-parity.sh compares every finding with PKHeX's own on real saves
 * and PKHeX's test files; see docs/manual/11-legality.md.
 *
 * No RTTI / no exceptions: every check calls base Pokemon virtuals; per-generation
 * differences branch on the game group / capability flags, never on concrete type.
 */

#ifndef LEGALITY_LEGALITY_H
#define LEGALITY_LEGALITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "Enums/GameVersion.h"
#include "Legality/ResultCode.h"

namespace Pokemon
{
    class Pokemon;
} // fwd decl — no heavy include

namespace Legality
{

    enum class Severity : uint8_t
    {
        Info,
        Warning,
        Invalid
    };

    /// One finding. `identifier` and `code` are PKHeX's CheckIdentifier and LegalityCheckResultCode
    /// for the same finding, which is what the parity harness compares; `text` is what the user reads.
    /// Where a finding comes from: a check, or one move slot's verdict (PKHeX keeps those apart, in
    /// LegalInfo.Moves / Relearn rather than its Results).
    enum class IssueSource : uint8_t
    {
        Check,
        Move,
    };

    struct Issue
    {
        Severity severity;
        CheckIdentifier identifier;
        ResultCode code;
        std::string text;
        IssueSource source = IssueSource::Check;
    };

    struct Report
    {
        std::vector<Issue> issues;

        /// Number of Warning+Invalid issues (Info notes are not counted as problems).
        int problemCount() const noexcept
        {
            int count = 0;
            for (const auto &i : issues)
                if (i.severity != Severity::Info)
                    ++count;
            return count;
        }
        bool ok() const noexcept { return problemCount() == 0; }

        /// Number of Invalid issues. PKHeX calls a Pokemon legal when this is 0: a Warning is its
        /// "Fishy", suspicious but still legal (LegalityAnalysis.Valid).
        int invalidCount() const noexcept
        {
            int invalidIssueCount = 0;
            for (const auto &issue : issues)
            {
                if (issue.severity == Severity::Invalid)
                {
                    ++invalidIssueCount;
                }
            }
            return invalidIssueCount;
        }

        /// Number of Warning issues, PKHeX's "Fishy" findings.
        int warningCount() const noexcept
        {
            int warningIssueCount = 0;
            for (const auto &issue : issues)
            {
                if (issue.severity == Severity::Warning)
                {
                    ++warningIssueCount;
                }
            }
            return warningIssueCount;
        }

        /// Is there anything at all to show? A Pokemon with no problems can still have
        /// Info notes worth reading -- which encounter it matched, or that a check could
        /// not run -- so the UI opens its issue list on this, not on ok().
        bool empty() const noexcept { return issues.empty(); }
    };

    /// PKHeX's English template for `code` with its {0} / {1} placeholders replaced.
    std::string getResultText(ResultCode code, const std::string &firstArgument = "", const std::string &secondArgument = "");

    struct AnalysisOptions;

    /// Analyze a decrypted Pokemon and return its legality issues (informational).
    /// originGroup = the Pokemon's own format group (Pokemon::getGameGroup()). Returns an
    /// empty report for an empty slot (species 0).
    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup);

    /// As above, with where the Pokemon is stored and the open save's trainer, which PKHeX's
    /// handler, trainer and slot checks compare against (see AnalysisContext.h).
    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup, const AnalysisOptions &options);
}

#endif
