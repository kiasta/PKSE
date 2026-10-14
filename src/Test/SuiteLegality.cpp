/**
 * The checker is informational and deliberately conservative, so it has no "correct" output to
 * compare against. What it does have is a property worth testing: run against Pokemon that were
 * caught on real hardware, in the game they came from, it should find nothing Invalid. Every
 * Invalid on a legitimately-played save is either a real illegality the user already knew about, or
 * a bug in the checker -- and the second kind is exactly the kind that is impossible to notice by
 * hand, because a checker that flags too much looks like it is working hard.
 *
 * The Gen 1 case is the worked example: the analyzer read Stat Experience through the 8-bit EV
 * accessors, which SATURATE at 255, and reported seven Invalid findings on a legitimately trained
 * Pokemon. Nothing crashed, nothing looked broken, and the only way to see it was to look at a real
 * pokemon and disagree with the verdict. This suite is that look, done for every Pokemon in the save.
 *
 * Findings here are WARNINGS, not failures: PKSE can edit saves, so a user's own box may genuinely
 * contain something illegal, and calling that a test failure would be wrong. The value is in the
 * list and the rate -- one Invalid in a box of thirty is a pokemon; thirty Invalids in a box of thirty
 * is a bug.
 *
 * A checker that never fires would pass all of that, so the suite also PLANTS defects: a blank
 * Pokemon with one deliberate illegality must be flagged for exactly that, and the same Pokemon
 * without it must not be. Those are failures, because there the right answer is known.
 */
#include <string>

#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Legality/Legality.h"

namespace Test
{

    namespace
    {
        constexpr int PartyChunk = 0;

        /// Analyze one Pokemon and report anything Invalid. `deep` additionally re-runs the analyzer
        /// to prove it is deterministic -- an analyzer whose verdict depends on how many times it
        /// has run has state it should not have, and would give the user a different answer each
        /// time they opened the same pokemon.
        void analyzeOne(Context &context, const Pokemon::Pokemon &pokemon, const std::string &where, bool deep)
        {
            const std::string whereLabel = where + "  " + monLabel(pokemon);

            Legality::Report legalityReport = Legality::analyze(pokemon, context.group);

            int invalid = 0, warnings = 0;
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == Legality::Severity::Invalid)
                {
                    ++invalid;
                    context.warn(false, "legality analyzer reports INVALID on a stored Pokemon",
                             whereLabel + " issue=\"" + issue.text + "\"");
                }
                else if (issue.severity == Legality::Severity::Warning)
                {
                    ++warnings;
                }
            }

            // problemCount() must agree with what the issue list actually contains; the UI gates its
            // badge on that number rather than on the list.
            context.check(legalityReport.problemCount() == invalid + warnings,
                      "legality report's problem count disagrees with its own issue list",
                      whereLabel + " problemCount=" + std::to_string(legalityReport.problemCount()) +
                          " invalid=" + std::to_string(invalid) + " warnings=" + std::to_string(warnings));
            context.check(legalityReport.ok() == (legalityReport.problemCount() == 0),
                          "legality report's ok() disagrees with its count", whereLabel);

            if (deep)
            {
                // The whole verdict, not just its length: two runs that found the same NUMBER of
                // different problems disagree just as much.
                Legality::Report again = Legality::analyze(pokemon, context.group);
                bool same = again.issues.size() == legalityReport.issues.size();
                for (size_t issueIndex = 0; same && issueIndex < again.issues.size(); ++issueIndex)
                    same = again.issues[issueIndex].text == legalityReport.issues[issueIndex].text &&
                           again.issues[issueIndex].severity == legalityReport.issues[issueIndex].severity;
                context.check(same, "legality analyzer is not deterministic: two runs disagree",
                          whereLabel + " first=" + std::to_string(legalityReport.issues.size()) +
                              " second=" + std::to_string(again.issues.size()));
            }
        }
    }

    namespace
    {
        bool reportsInvalid(const Legality::Report &legalityReport, const std::string &textPrefix)
        {
            for (const auto &issue : legalityReport.issues)
                if (issue.severity == Legality::Severity::Invalid && issue.text.rfind(textPrefix, 0) == 0)
                    return true;
            return false;
        }

        /// One planted defect: `control` is a Pokemon without it, `defective` the same Pokemon with
        /// it. The checker must flag the second for exactly this and must not flag the first.
        void expectFlagged(Context &context, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective,
                           const std::string &textPrefix)
        {
            const std::string where =
                "planted defect \"" + textPrefix + "\" on a blank " + Enums::getGameVersionName(context.group) + " entity";
            context.check(!reportsInvalid(Legality::analyze(control, context.group), textPrefix),
                          "legality analyzer flags a planted defect on the control Pokemon that does not have it",
                          where);
            context.check(reportsInvalid(Legality::analyze(defective, context.group), textPrefix),
                          "legality analyzer did not flag a planted defect", where);
        }

        void plantedDefects(Context &context, Trainer::Trainer &tr)
        {
            auto control = tr.createBlankPokemon();
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing"))
                return;
            control->setSpecies(25);  // Pikachu, in every game's dex
            control->setMove(0, 33);  // Tackle
            control->setMove(1, 85);  // Thunderbolt -- both exist from Gen 1 on
            control->refreshChecksum();

            // Every format holds four moves, and the same move twice is never legal.
            auto duplicate = control->clone();
            if (!context.check(duplicate != nullptr, "clone() returned nothing for this format"))
                return;
            duplicate->setMove(1, 33);
            duplicate->refreshChecksum();
            expectFlagged(context, *control, *duplicate, "Duplicate move");

            // The 510 EV cap is Gen 3 onward. Gens 1 and 2 keep uncapped Stat Experience, and Let's Go
            // stores no EVs at all, so there is nothing to plant there.
            const Enums::GameVersion group = context.group;
            if (group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC &&
                group != Enums::GameVersion::GG)
            {
                auto overTrained = control->clone();
                if (!overTrained)
                    return;
                for (int statIndex = 0; statIndex < 3; ++statIndex)
                    overTrained->setEVWide(statIndex, 252);
                overTrained->refreshChecksum();
                expectFlagged(context, *control, *overTrained, "EV total over 510");
            }
        }
    }

    int legalityPlan(Context &context)
    {
        if (!context.trainer)
            return 0;
        return 1 + (int)context.trainer->getBoxCount();
    }

    void legalityRun(Context &context, int chunk)
    {
        Trainer::Trainer *tr = context.trainer;
        if (!tr)
            return;

        if (chunk == PartyChunk)
        {
            // The documented contract for an empty slot: species 0 analyses to an EMPTY report, not
            // to a pile of complaints about a Pokemon that is not there. The storage view runs the
            // analyzer over whatever the cursor is on, empty slots included.
            plantedDefects(context, *tr);

            if (auto blank = tr->createBlankPokemon())
            {
                Legality::Report legalityReport = Legality::analyze(*blank, context.group);
                context.check(
                    legalityReport.empty(), "legality analyzer returns findings for an EMPTY slot (species 0)",
                    "issues=" + std::to_string(legalityReport.issues.size()) +
                        (legalityReport.issues.empty() ? "" : " first=\"" + legalityReport.issues[0].text + "\""));
            }

            bool deep = true;
            for (size_t partySlotIndex = 0; partySlotIndex < tr->party.size(); ++partySlotIndex)
            {
                // a blank is not a Pokemon; see isEmptySlot
                if (isEmptySlot(tr->party[partySlotIndex].get())) continue;
                analyzeOne(context, *tr->party[partySlotIndex], partySlot((int)partySlotIndex), deep);
                deep = false; // the determinism re-run costs a full analysis; once per chunk is enough
            }
            return;
        }

        const size_t box = (size_t)(chunk - 1);
        if (box >= tr->boxes.size())
            return;

        const size_t slots = tr->getSlotsPerBox();
        bool deep = true;
        int analyzed = 0;
        for (size_t slotIndex = 0; slotIndex < slots && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
        {
            if (isEmptySlot(tr->boxes[box][slotIndex].get()))
                continue;
            analyzeOne(context, *tr->boxes[box][slotIndex], slot((int)box, (int)slotIndex), deep);
            deep = false;
            ++analyzed;
        }
        if (analyzed > 0)
            context.note("Pokemon put through the legality analyzer",
                     "box=" + std::to_string(box + 1) + " count=" + std::to_string(analyzed));
    }
}
