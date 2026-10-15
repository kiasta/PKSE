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
        checkCount = failCount = warnCount = skipCount = infoCount = 0;
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
            if (tally.target == scopeTarget && tally.suite == scopeSuite)
                return tally;
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
        Tally &t = currentTally();
        if (severity == Severity::Fail || severity == Severity::Warn)
        {
            ++checkCount;
            ++t.checks;
        }
        if (severity == Severity::Fail)
        {
            ++failCount;
            ++t.fails;
        }
        if (severity == Severity::Warn)
        {
            ++warnCount;
            ++t.warns;
        }
        if (severity == Severity::Skip)
        {
            ++skipCount;
            ++t.skips;
        }
        if (severity == Severity::Info)
        {
            ++infoCount;
        }

        FindingGroup *group = nullptr;
        for (auto &g : findingGroups)
        {
            if (g.suite == scopeSuite && g.what == what && g.severity == severity)
            {
                group = &g;
                break;
            }
        }
        if (!group)
        {
            findingGroups.push_back(FindingGroup{severity, scopeSuite, what, 0, {}});
            group = &findingGroups.back();
        }
        ++group->count;

        std::string line = scopeTarget.empty() ? std::string("-") : scopeTarget;
        if (!detail.empty())
            line += "  " + detail;
        if ((int)group->samples.size() < MAX_SAMPLE_COUNT)
            group->samples.push_back(line);

        // On-screen ticker. Only problems go here -- the point of the live view is to see something
        // going wrong as it happens, and Info notes and skips would bury it.
        if (severity == Severity::Fail || severity == Severity::Warn)
        {
            recentLines.push_back(std::string(severityName(severity)) + "  " + scopeSuite + ": " + what + "  [" + line + "]");
            if ((int)recentLines.size() > RECENT_LINE_COUNT)
                recentLines.erase(recentLines.begin());
        }
    }

    bool Report::write(const std::string &path, const std::string &header) const
    {
        FILE *f = fopen(path.c_str(), "w");
        if (!f)
        {
            Utils::logErrorToFile("SelfTest: could not open report for writing", path.c_str());
            return false;
        }

        fprintf(f, "========================================================================\n");
        fprintf(f, "%s", header.c_str());
        fprintf(f, "========================================================================\n\n");

        fprintf(f, "SUMMARY  checks=%d failures=%d warnings=%d skipped=%d notes=%d\n\n", checkCount, failCount, warnCount, skipCount, infoCount);

        // anyone has to read a detail line.
        fprintf(f, "------------------------------------------------------------------------\n");
        fprintf(f, "COVERAGE\n");
        fprintf(f, "------------------------------------------------------------------------\n");
        fprintf(f, "%-40s %-12s %8s %6s %6s %6s\n", "TARGET", "SUITE", "CHECKS", "FAIL", "WARN", "SKIP");
        for (const auto &t : tallyList)
        {
            fprintf(f, "%-40s %-12s %8d %6d %6d %6d\n", (t.target.empty() ? "-" : t.target.c_str()), t.suite.c_str(), t.checks, t.fails, t.warns, t.skips);
        }
        fprintf(f, "\n");

        // failure and the note that explains it stay next to each other.
        const Severity order[4] = {Severity::Fail, Severity::Warn, Severity::Skip, Severity::Info};
        for (Severity severity : order)
        {
            bool wroteHeading = false;
            for (const auto &g : findingGroups)
            {
                if (g.severity != severity)
                    continue;
                if (!wroteHeading)
                {
                    fprintf(f, "------------------------------------------------------------------------\n");
                    fprintf(f, "%s\n", severity == Severity::Fail ? "FAILURES" : severity == Severity::Warn ? "WARNINGS" : severity == Severity::Skip ? "SKIPPED (not checked)" : "NOTES");
                    fprintf(f, "------------------------------------------------------------------------\n");
                    wroteHeading = true;
                }
                if (g.count > (int)g.samples.size())
                    fprintf(f, "\n[%s] %s: %s   (x%d, first %d shown)\n", severityName(g.severity), g.suite.c_str(), g.what.c_str(), g.count, (int)g.samples.size());
                else
                    fprintf(f, "\n[%s] %s: %s   (x%d)\n", severityName(g.severity), g.suite.c_str(), g.what.c_str(), g.count);
                for (const auto &s : g.samples)
                    fprintf(f, "    %s\n", s.c_str());
            }
            if (wroteHeading)
                fprintf(f, "\n");
        }

        if (failCount == 0 && warnCount == 0 && skipCount == 0)
            fprintf(f, "Nothing to report: every check passed and nothing was skipped.\n\n");

        if (!traceLines.empty())
        {
            fprintf(f, "------------------------------------------------------------------------\n");
            fprintf(f, "TRACE  (what the run DID, in order)\n");
            fprintf(f, "------------------------------------------------------------------------\n");
            for (const auto &t : traceLines)
                fprintf(f, "%s\n", t.c_str());
            if (traceTruncated)
                fprintf(f, "... trace truncated at %d lines; the full one is in sdmc:/PKSE/trace.log when debug logging is on\n", MAX_TRACE_LINES);
            fprintf(f, "\n");
        }

        fprintf(f, "========================================================================\n");
        fprintf(f, "END OF REPORT\n");
        fclose(f);
        return true;
    }

    bool Context::check(bool passed, const std::string &what, const std::string &detail)
    {
        if (!rep)
            return passed;
        if (passed)
            rep->pass();
        else
            rep->record(Severity::Fail, what, detail);
        return passed;
    }

    bool Context::warn(bool passed, const std::string &what, const std::string &detail)
    {
        if (!rep)
            return passed;
        if (passed)
            rep->pass();
        else
            rep->record(Severity::Warn, what, detail);
        return passed;
    }

    void Context::fail(const std::string &what, const std::string &detail)
    {
        if (rep)
            rep->record(Severity::Fail, what, detail);
    }

    void Context::note(const std::string &what, const std::string &detail)
    {
        if (rep)
            rep->record(Severity::Info, what, detail);
    }

    void Context::skip(const std::string &what, const std::string &detail)
    {
        if (rep)
            rep->record(Severity::Skip, what, detail);
    }

    std::string numberField(const char *key, long long value)
    {
        return std::string(key) + "=" + std::to_string(value);
    }

    std::string hex(const char *key, unsigned long long value, int digits)
    {
        char buffer[32];
        if (digits > 0)
            snprintf(buffer, sizeof(buffer), "0x%0*llX", digits, value);
        else
            snprintf(buffer, sizeof(buffer), "0x%llX", value);
        return std::string(key) + "=" + buffer;
    }

    std::string slot(int box, int slotIndex)
    {
        return "box=" + std::to_string(box + 1) + " slot=" + std::to_string(slotIndex + 1);
    }

    std::string partySlot(int index)
    {
        return "party=" + std::to_string(index + 1);
    }

    std::string monLabel(const Pokemon::Pokemon &pokemon)
    {
        // Utils::briefPokemon is the same spelling the [EVENT] action log uses for a pokemon in passing --
        // species / form / level / nickname. Sharing it means a self-test finding and the action log
        // that would have produced the same pokemon read identically and grep identically.
        return Utils::briefPokemon(pokemon);
    }
}
