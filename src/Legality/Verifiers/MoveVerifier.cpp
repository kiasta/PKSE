// Reports PKHeX's per-move verdicts (LegalInfo.Moves and LegalInfo.Relearn, which PKHeX's report prints
// through LegalityFormatting.AddMoves / AddRelearn): one finding per invalid move slot.
#include "Legality/Verifiers/Verifiers.h"

#include <span>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/LearnSource/LearnVerifier.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        using LearnSource::MoveResult;

        constexpr int MOVE_SLOT_COUNT = 4;

        void reportResults(const AnalysisContext &context, std::span<const MoveResult, 4> results, bool hasUnknownMoves, CheckIdentifier identifier, const char *label)
        {
            const Enums::GameVersion currentContext = Evolutions::getEntityContextGroup(Evolutions::getEvolutionEntity(context.pokemon));
            for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
            {
                const MoveResult &result = results[slotIndex];
                // Not parsed is invalid for PKHeX (MoveResult.Valid), except where the template's moves
                // are unknown or the verifier only ran its template-free checks.
                if (result.isValid() || (hasUnknownMoves && result.info.method == LearnSource::LearnMethod::None))
                {
                    continue;
                }
                // PKHeX LegalityLocalizationContext.FormatMove / FormatRelearn (English).
                std::string text = std::string("Invalid ") + label + " " + std::to_string(slotIndex + 1) + ": " + LearnSource::summarizeResult(result, "");
                if (identifier == CheckIdentifier::CurrentMove && result.contextGroup != Enums::GameVersion::Invalid && result.contextGroup != currentContext)
                {
                    text += std::string(" [") + Evolutions::getEvolutionContextSourceName(result.contextGroup) + "]";
                }
                context.report.issues.push_back(Issue{Severity::Invalid, identifier, ResultCode::Error, std::move(text), IssueSource::Move});
            }
        }
    }

    void verifyMoves(const AnalysisContext &context)
    {
        // The verdicts are the encounter engine's (LegalityInfo::moves / relearn); without its match
        // there is no template to judge the moves against.
        if (context.legalityInfo == nullptr)
        {
            return;
        }
        const Encounters::LegalityInfo &legalityInfo = *context.legalityInfo;
        reportResults(context, legalityInfo.moves, legalityInfo.hasUnknownMoves, CheckIdentifier::CurrentMove, "Move");
        // PKHeX's report lists relearn moves only for Gen 6+ formats.
        if (context.formatGeneration >= 6)
        {
            reportResults(context, legalityInfo.relearn, legalityInfo.hasUnknownRelearnMoves, CheckIdentifier::RelearnMove, "Relearn Move");
        }
    }
}
