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
        constexpr int PARTY_CHUNK = 0;

        /// Analyze one Pokemon and report anything Invalid. `checkDeterminism` additionally re-runs the analyzer
        /// to prove it is deterministic -- an analyzer whose verdict depends on how many times it
        /// has run has state it should not have, and would give the user a different answer each
        /// time they opened the same pokemon.
        void analyzeOne(Context &context, const Pokemon::Pokemon &pokemon, const std::string &slotLabel, bool checkDeterminism)
        {
            const std::string findingLabel = slotLabel + "  " + pokemonLabel(pokemon);

            Legality::Report legalityReport = Legality::analyze(pokemon, context.group);

            int invalidCount = 0;
            int warningCount = 0;
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == Legality::Severity::Invalid)
                {
                    ++invalidCount;
                    context.warn(false, "legality analyzer reports INVALID on a stored Pokemon", findingLabel + " issue=\"" + issue.text + "\"");
                }
                else if (issue.severity == Legality::Severity::Warning)
                {
                    ++warningCount;
                }
            }

            // problemCount() must agree with what the issue list actually contains; the UI gates its
            // badge on that number rather than on the list.
            context.check(legalityReport.problemCount() == invalidCount + warningCount, "legality report's problem count disagrees with its own issue list", findingLabel + " problemCount=" + std::to_string(legalityReport.problemCount()) + " invalid=" + std::to_string(invalidCount) + " warnings=" + std::to_string(warningCount));
            context.check(legalityReport.ok() == (legalityReport.problemCount() == 0), "legality report's ok() disagrees with its count", findingLabel);

            if (checkDeterminism)
            {
                // The whole verdict, not just its length: two runs that found the same NUMBER of
                // different problems disagree just as much.
                Legality::Report secondReport = Legality::analyze(pokemon, context.group);
                bool verdictsMatch = secondReport.issues.size() == legalityReport.issues.size();
                for (size_t issueIndex = 0; verdictsMatch && issueIndex < secondReport.issues.size(); ++issueIndex)
                {
                    verdictsMatch = secondReport.issues[issueIndex].text == legalityReport.issues[issueIndex].text && secondReport.issues[issueIndex].severity == legalityReport.issues[issueIndex].severity;
                }
                context.check(verdictsMatch, "legality analyzer is not deterministic: two runs disagree", findingLabel + " first=" + std::to_string(legalityReport.issues.size()) + " second=" + std::to_string(secondReport.issues.size()));
            }
        }
    }

    namespace
    {
        bool reportsInvalid(const Legality::Report &legalityReport, const std::string &textPrefix)
        {
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == Legality::Severity::Invalid && issue.text.rfind(textPrefix, 0) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// One planted defect: `control` is a Pokemon without it, `defective` the same Pokemon with
        /// it. The checker must flag the second for exactly this and must not flag the first.
        void expectFlagged(Context &context, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const std::string &textPrefix)
        {
            const std::string defectLabel = "planted defect \"" + textPrefix + "\" on a blank " + Enums::getGameVersionName(context.group) + " entity";
            context.check(!reportsInvalid(Legality::analyze(control, context.group), textPrefix), "legality analyzer flags a planted defect on the control Pokemon that does not have it", defectLabel);
            context.check(reportsInvalid(Legality::analyze(defective, context.group), textPrefix), "legality analyzer did not flag a planted defect", defectLabel);
        }

        void plantedDefects(Context &context, Trainer::Trainer &trainer)
        {
            auto control = trainer.createBlankPokemon();
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing")) return;
            control->setSpecies(25);  // Pikachu, in every game's dex
            control->setMove(0, 33);  // Tackle
            control->setMove(1, 85);  // Thunderbolt -- both exist from Gen 1 on
            control->refreshChecksum();

            // Every format holds four moves, and the same move twice is never legal.
            auto duplicate = control->clone();
            if (!context.check(duplicate != nullptr, "clone() returned nothing for this format")) return;
            duplicate->setMove(1, 33);
            duplicate->refreshChecksum();
            expectFlagged(context, *control, *duplicate, "Duplicate move");

            // The 510 EV cap is Gen 3 onward. Gens 1 and 2 keep uncapped Stat Experience, and Let's Go
            // stores no EVs at all, so there is nothing to plant there.
            const Enums::GameVersion group = context.group;
            if (group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC && group != Enums::GameVersion::GG)
            {
                auto overTrained = control->clone();
                if (!overTrained) return;
                for (int statIndex = 0; statIndex < 3; ++statIndex)
                {
                    overTrained->setEVWide(statIndex, 252);
                }
                overTrained->refreshChecksum();
                expectFlagged(context, *control, *overTrained, "EV total over 510");
            }
        }
    }

    int legalityPlan(Context &context)
    {
        if (!context.trainer) return 0;
        return 1 + (int)context.trainer->getBoxCount();
    }

    void legalityRun(Context &context, int chunk)
    {
        Trainer::Trainer *trainer = context.trainer;
        if (!trainer) return;

        if (chunk == PARTY_CHUNK)
        {
            // The documented contract for an empty slot: species 0 analyses to an EMPTY report, not
            // to a pile of complaints about a Pokemon that is not there. The storage view runs the
            // analyzer over whatever the cursor is on, empty slots included.
            plantedDefects(context, *trainer);

            if (auto blankPokemon = trainer->createBlankPokemon())
            {
                Legality::Report legalityReport = Legality::analyze(*blankPokemon, context.group);
                context.check(legalityReport.empty(), "legality analyzer returns findings for an EMPTY slot (species 0)", "issues=" + std::to_string(legalityReport.issues.size()) + (legalityReport.issues.empty() ? "" : " first=\"" + legalityReport.issues[0].text + "\""));
            }

            bool checkDeterminism = true;
            for (size_t partySlotIndex = 0; partySlotIndex < trainer->party.size(); ++partySlotIndex)
            {
                if (isEmptySlot(trainer->party[partySlotIndex].get()))
                {
                    continue;
                }
                analyzeOne(context, *trainer->party[partySlotIndex], partySlot((int)partySlotIndex), checkDeterminism);
                checkDeterminism = false; // the determinism re-run costs a full analysis; once per chunk is enough
            }
            return;
        }

        const size_t boxIndex = (size_t)(chunk - 1);
        if (boxIndex >= trainer->boxes.size()) return;

        const size_t slotsPerBox = trainer->getSlotsPerBox();
        bool checkDeterminism = true;
        int analyzedCount = 0;
        for (size_t slotIndex = 0; slotIndex < slotsPerBox && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
        {
            if (isEmptySlot(trainer->boxes[boxIndex][slotIndex].get()))
            {
                continue;
            }
            analyzeOne(context, *trainer->boxes[boxIndex][slotIndex], slot((int)boxIndex, (int)slotIndex), checkDeterminism);
            checkDeterminism = false;
            ++analyzedCount;
        }
        if (analyzedCount > 0)
        {
            context.note("Pokemon put through the legality analyzer", "box=" + std::to_string(boxIndex + 1) + " count=" + std::to_string(analyzedCount));
        }
    }
}
