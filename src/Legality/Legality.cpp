/**
 * See Legality.h. Every check reads the base Pokemon interface polymorphically;
 * per-generation behavior branches on the game group / capability flags, never on type
 * (RTTI is disabled). Optional fields that are unwired for a format read 0 and are
 * treated as "not applicable -> skip" so unsupported formats produce no false flags.
 */

#include "Legality/Legality.h"

#include <optional>
#include <string>

#include "Legality/AnalysisContext.h"
#include "Legality/Encounters/EncounterFinder.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Enums/LanguageID.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/FormInfo.h"    // isFormGenderSpecific, getGenderRatio
#include "Trainer/Trainer.h" // Trainer::getSpeciesName / getItemName (name-table sentinels)

namespace Legality
{

    namespace
    {
        void add(Report &report, Severity severity, CheckIdentifier identifier, ResultCode code, std::string text)
        {
            report.issues.push_back(Issue{severity, identifier, code, std::move(text)});
        }
        /// Generation of a save-format group (1-9), 0 when it is not one.
        int groupGeneration(Enums::GameVersion group)
        {
            return Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
        }
    }

    namespace
    {
        /// PKHeX LegalityAnalysis.ParsePK1 / ParsePK3 ... ParsePK9 and UpdateChecks: which verifiers
        /// run, in which order, behind which format gates.
        void runVerifiers(const AnalysisContext &context)
        {
            const int formatGeneration = context.formatGeneration;
            if (formatGeneration < 3)
            {
                verifyNickname(context);
                verifyLevel(context);
                verifyTrainerName(context);
                verifyMisc(context);
                verifyMovePP(context);
                verifyEffortValues(context);
                if (formatGeneration == 2)
                {
                    verifyHeldItem(context);
                }
                return;
            }

            verifyPID(context);
            verifyLanguage(context);
            verifyNickname(context);
            verifyTrainerName(context);
            verifyTrainerId(context);
            verifyIndividualValues(context);
            verifyEffortValues(context);
            verifyLevel(context);
            verifyRibbons(context);
            verifyAbility(context);
            verifyBall(context);
            verifyForm(context);
            verifyMisc(context);
            verifyMovePP(context);
            verifyGender(context);
            verifyHeldItem(context);
            verifyContestStats(context);
            verifyMarkings(context);
            // Gen 6 -> 7 transfer removes the ground tile.
            if (formatGeneration >= 4 && formatGeneration <= 6)
            {
                verifyGroundTile(context);
            }
            verifySlotType(context);
            if (formatGeneration >= 6)
            {
                verifyHistory(context);
                // Gen 7 -> 8 transfer removes the console region.
                if (formatGeneration < 8)
                {
                    verifyConsoleRegion(context);
                }
                verifyMemories(context);
                verifyMedals(context);
                if (formatGeneration >= 7)
                {
                    verifyHyperTraining(context);
                    verifyTrashBytes(context);
                    if (formatGeneration >= 8)
                    {
                        verifyMarks(context);
                    }
                }
            }
            verifyTransfer(context);
            verifyColosseumXD(context);
            verifyAwakeningValues(context);
        }
    }

    std::string getResultText(ResultCode code, const std::string &firstArgument, const std::string &secondArgument)
    {
        std::string resultText = getResultCodeTemplate(code);
        const std::string placeholders[2] = {"{0}", "{1}"};
        const std::string *arguments[2] = {&firstArgument, &secondArgument};
        for (int placeholderIndex = 0; placeholderIndex < 2; ++placeholderIndex)
        {
            for (size_t placeholderOffset = resultText.find(placeholders[placeholderIndex]); placeholderOffset != std::string::npos; placeholderOffset = resultText.find(placeholders[placeholderIndex], placeholderOffset + arguments[placeholderIndex]->size()))
            {
                resultText.replace(placeholderOffset, placeholders[placeholderIndex].size(), *arguments[placeholderIndex]);
            }
        }
        return resultText;
    }

    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup)
    {
        return analyze(pokemon, originGroup, AnalysisOptions{});
    }

    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup, const AnalysisOptions &options)
    {
        Report report;
        const uint16_t species = pokemon.speciesID();
        // empty slot — nothing to validate
        if (species == 0) return report;

        const int formatGeneration = groupGeneration(originGroup);

        if (std::string(Trainer::getSpeciesName(species)) == "Unknown")
            add(report, Severity::Invalid, CheckIdentifier::Misc, ResultCode::Error, "Unknown species id " + std::to_string(species));

        if (!pokemon.checksumValid())
            add(report, Severity::Warning, CheckIdentifier::Misc, ResultCode::Error, "Stored checksum is invalid");

        // The encounter search runs first: every verifier reads what it established.
        AnalysisContext context{pokemon, originGroup, formatGeneration, options, report, EncounterInfo{}};
        Encounters::LegalityInfo legalityInfo(pokemon, options);
        Encounters::EncounterFinder::findVerifiedEncounter(pokemon, legalityInfo);
        Encounters::EncounterFinder::updateVirtualConsoleTransferInfo(pokemon, legalityInfo);
        context.encounter = Encounters::makeEncounterInfo(legalityInfo, pokemon);
        context.legalityInfo = &legalityInfo;
        legalityInfo.reportParse(context);
        // PKHeX's report prints the per-move verdicts right after the encounter.
        verifyMoves(context);
        runVerifiers(context);
        return report;
    }
}
