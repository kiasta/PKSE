/**
 * What an egg encounter allows, which the ability and ball verifiers both read: the species and form
 * the egg hatched as, and whether that species could be bred with its Hidden Ability. Ported from
 * PKHeX PKHeX.Core/Legality/Verifiers/Ability/AbilityBreedLegality.cs and the EncounterEgg* templates.
 */
#ifndef LEGALITY_VERIFIERS_BREEDINGRULES_H
#define LEGALITY_VERIFIERS_BREEDINGRULES_H

#include <cstdint>
#include <optional>

#include "Legality/AnalysisContext.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"

namespace Legality
{
    /// The species and form an egg encounter hatched as (PKHeX EncounterEgg*.Species/Form); empty
    /// when the encounter is not an egg.
    std::optional<Evolutions::EvolutionCriteria> getEggStage(const AnalysisContext &context);

    /// Generation of the game the matched encounter belongs to; the origin generation when no
    /// template was matched.
    int getEncounterGeneration(const AnalysisContext &context);

    /// PKHeX AbilityBreedLegality.IsHiddenPossible5/6/7/HOME: whether an egg bred in `generation` can
    /// have its Hidden Ability. Always false before Gen 5.
    bool isHiddenAbilityBreedable(int generation, uint16_t speciesId, uint8_t formId);
}

#endif
