#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <errno.h>

#include "Test/TestReport.h"
#include "Utils/EventLog.h"
#include "Utils/Logger.h"
#include "Pokemon/Pokemon.h"

namespace Test
{

    const char *severityName(Severity severity)
    {
        switch (severity)
        {
        case Severity::Fail:
            return "FAIL";
        case Severity::Warn:
            return "WARN";
        case Severity::Skip:
            return "SKIP";
        default:
            return "INFO";
        }
    }

    void Report::reset()
    {
        scopeTarget.clear();
        scopeSuite.clear();
        checkCount = 0;
        failCount = 0;
        warnCount = 0;
        skipCount = 0;
        infoCount = 0;
        findingGroups.clear();
        tallyList.clear();
        recentLines.clear();
        traceLines.clear();
        traceTruncated = false;
    }

    void Report::addTrace(const std::string &line)
    {
        // Drops lines past the cap rather than evicting old ones: a journey's VALUE is in its order,
        // and a trace that silently lost its beginning would be read as though it started there.
        if ((int)traceLines.size() >= MAX_TRACE_LINES)
        {
            traceTruncated = true;
            return;
        }
        traceLines.push_back(line);
    }

    void Report::setScope(const std::string &target, const std::string &suite)
    {
        scopeTarget = target;
        scopeSuite = suite;
    }

    Tally &Report::currentTally()
    {
        for (auto &tally : tallyList)
        {
            if (tally.target == scopeTarget && tally.suite == scopeSuite)
            {
                return tally;
            }
        }
        tallyList.push_back(Tally{scopeTarget, scopeSuite, 0, 0, 0, 0});
        return tallyList.back();
    }

    void Report::pass()
    {
        ++checkCount;
        ++currentTally().checks;
    }

    void Report::record(Severity severity, const std::string &what, const std::string &detail)
    {
        // A Fail or Warn is also a check that RAN, so it counts. A Skip or an Info note is not a
        // check -- counting either would inflate the "N checks" figure with things that could never
        // fail.
        Tally &tally = currentTally();
        if (severity == Severity::Fail || severity == Severity::Warn)
        {
            ++checkCount;
            ++tally.checks;
        }
        if (severity == Severity::Fail)
        {
            ++failCount;
            ++tally.fails;
        }
        if (severity == Severity::Warn)
        {
            ++warnCount;
            ++tally.warns;
        }
        if (severity == Severity::Skip)
        {
            ++skipCount;
            ++tally.skips;
        }
        if (severity == Severity::Info)
        {
            ++infoCount;
        }

        FindingGroup *group = nullptr;
        for (auto &candidateGroup : findingGroups)
        {
            if (candidateGroup.suite == scopeSuite && candidateGroup.what == what && candidateGroup.severity == severity)
            {
                group = &candidateGroup;
                break;
            }
        }
        if (!group)
        {
            findingGroups.push_back(FindingGroup{severity, scopeSuite, what, 0, {}});
            group = &findingGroups.back();
        }
        ++group->occurrenceCount;

        std::string sampleLine = scopeTarget.empty() ? std::string("-") : scopeTarget;
        if (!detail.empty())
        {
            sampleLine += "  " + detail;
        }
        if ((int)group->samples.size() < MAX_SAMPLE_COUNT)
        {
            group->samples.push_back(sampleLine);
        }

        // On-screen ticker. Only problems go here -- the point of the live view is to see something
        // going wrong as it happens, and Info notes and skips would bury it.
        if (severity == Severity::Fail || severity == Severity::Warn)
        {
            recentLines.push_back(std::string(severityName(severity)) + "  " + scopeSuite + ": " + what + "  [" + sampleLine + "]");
            if ((int)recentLines.size() > RECENT_LINE_COUNT)
            {
                recentLines.erase(recentLines.begin());
            }
        }
    }

    bool Report::write(const std::string &path, const std::string &header) const
    {
        FILE *reportFile = fopen(path.c_str(), "w");
        if (!reportFile)
        {
            Utils::logErrorToFile("SelfTest: could not open report for writing", path.c_str());
            return false;
        }

        fprintf(reportFile, "========================================================================\n");
        fprintf(reportFile, "%s", header.c_str());
        fprintf(reportFile, "========================================================================\n\n");

        fprintf(reportFile, "SUMMARY  checks=%d failures=%d warnings=%d skipped=%d notes=%d\n\n", checkCount, failCount, warnCount, skipCount, infoCount);

        // The coverage table comes first, so the reader knows which target and suite the failures
        // are in before anyone has to read a detail line.
        fprintf(reportFile, "------------------------------------------------------------------------\n");
        fprintf(reportFile, "COVERAGE\n");
        fprintf(reportFile, "------------------------------------------------------------------------\n");
        fprintf(reportFile, "%-40s %-12s %8s %6s %6s %6s\n", "TARGET", "SUITE", "CHECKS", "FAIL", "WARN", "SKIP");
        for (const auto &tally : tallyList)
        {
            fprintf(reportFile, "%-40s %-12s %8d %6d %6d %6d\n", (tally.target.empty() ? "-" : tally.target.c_str()), tally.suite.c_str(), tally.checks, tally.fails, tally.warns, tally.skips);
        }
        fprintf(reportFile, "\n");

        // Most serious first, so the report opens on what needs fixing.
        const Severity severityOrder[4] = {Severity::Fail, Severity::Warn, Severity::Skip, Severity::Info};
        for (Severity severity : severityOrder)
        {
            bool wroteHeading = false;
            for (const auto &group : findingGroups)
            {
                if (group.severity != severity)
                {
                    continue;
                }
                if (!wroteHeading)
                {
                    fprintf(reportFile, "------------------------------------------------------------------------\n");
                    fprintf(reportFile, "%s\n", severity == Severity::Fail ? "FAILURES" : severity == Severity::Warn ? "WARNINGS" : severity == Severity::Skip ? "SKIPPED (not checked)" : "NOTES");
                    fprintf(reportFile, "------------------------------------------------------------------------\n");
                    wroteHeading = true;
                }
                if (group.occurrenceCount > (int)group.samples.size())
                {
                    fprintf(reportFile, "\n[%s] %s: %s   (x%d, first %d shown)\n", severityName(group.severity), group.suite.c_str(), group.what.c_str(), group.occurrenceCount, (int)group.samples.size());
                }
                else
                {
                    fprintf(reportFile, "\n[%s] %s: %s   (x%d)\n", severityName(group.severity), group.suite.c_str(), group.what.c_str(), group.occurrenceCount);
                }
                for (const auto &sample : group.samples)
                {
                    fprintf(reportFile, "    %s\n", sample.c_str());
                }
            }
            if (wroteHeading)
            {
                fprintf(reportFile, "\n");
            }
        }

        if (failCount == 0 && warnCount == 0 && skipCount == 0)
        {
            fprintf(reportFile, "Nothing to report: every check passed and nothing was skipped.\n\n");
        }

        if (!traceLines.empty())
        {
            fprintf(reportFile, "------------------------------------------------------------------------\n");
            fprintf(reportFile, "TRACE  (what the run DID, in order)\n");
            fprintf(reportFile, "------------------------------------------------------------------------\n");
            for (const auto &traceLine : traceLines)
            {
                fprintf(reportFile, "%s\n", traceLine.c_str());
            }
            if (traceTruncated)
            {
                fprintf(reportFile, "... trace truncated at %d lines; the full one is in sdmc:/PKSE/trace.log when debug logging is on\n", MAX_TRACE_LINES);
            }
            fprintf(reportFile, "\n");
        }

        fprintf(reportFile, "========================================================================\n");
        fprintf(reportFile, "END OF REPORT\n");
        fclose(reportFile);
        return true;
    }

    bool Context::check(bool passed, const std::string &what, const std::string &detail)
    {
        if (!report) return passed;
        if (passed)
        {
            report->pass();
        }
        else
        {
            report->record(Severity::Fail, what, detail);
        }
        return passed;
    }

    bool Context::warn(bool passed, const std::string &what, const std::string &detail)
    {
        if (!report) return passed;
        if (passed)
        {
            report->pass();
        }
        else
        {
            report->record(Severity::Warn, what, detail);
        }
        return passed;
    }

    void Context::fail(const std::string &what, const std::string &detail)
    {
        if (report)
        {
            report->record(Severity::Fail, what, detail);
        }
    }

    void Context::note(const std::string &what, const std::string &detail)
    {
        if (report)
        {
            report->record(Severity::Info, what, detail);
        }
    }

    void Context::skip(const std::string &what, const std::string &detail)
    {
        if (report)
        {
            report->record(Severity::Skip, what, detail);
        }
    }

    std::string numberField(const char *key, long long value)
    {
        return std::string(key) + "=" + std::to_string(value);
    }

    std::string hexField(const char *key, unsigned long long value, int digits)
    {
        char hexText[32];
        if (digits > 0)
        {
            snprintf(hexText, sizeof(hexText), "0x%0*llX", digits, value);
        }
        else
        {
            snprintf(hexText, sizeof(hexText), "0x%llX", value);
        }
        return std::string(key) + "=" + hexText;
    }

    std::string slot(int box, int slotIndex)
    {
        return "box=" + std::to_string(box + 1) + " slot=" + std::to_string(slotIndex + 1);
    }

    std::string partySlot(int partyIndex)
    {
        return "party=" + std::to_string(partyIndex + 1);
    }

    std::string pokemonLabel(const Pokemon::Pokemon &pokemon)
    {
        // Utils::briefPokemon is the same spelling the [EVENT] action log uses for a pokemon in passing --
        // species / form / level / nickname. Sharing it means a self-test finding and the action log
        // that would have produced the same pokemon read identically and grep identically.
        return Utils::briefPokemon(pokemon);
    }
}
