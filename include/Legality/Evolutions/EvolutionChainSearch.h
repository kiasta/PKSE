/**
 * Builds a Pokemon's evolution chains: per context (EvolutionHistory), and for the context it
 * originated in (the origin chain an encounter is matched against). Ported from PKHeX
 * PKHeX.Core/Legality/Evolutions/EvolutionChain.cs and EncounterOrigin.cs.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONCHAINSEARCH_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONCHAINSEARCH_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Evolutions/EvolutionOrigin.h"

namespace Legality
{
    namespace Evolutions
    {
        /// The IEncounterTemplate properties GetEvolutionChainsAllGens reads.
        struct EvolutionEncounter
        {
            uint16_t speciesId = 0;
            /// A context group (EVOLUTION_CONTEXT_GROUPS).
            Enums::GameVersion contextGroup = Enums::GameVersion::Invalid;
            uint8_t generation = 0;
            uint8_t levelMin = 0;
            /// PKHeX EncounterInvalid: no encounter explains the Pokemon.
            bool isInvalid = false;
        };

        /// PKHeX EncounterInvalid(pk): the template LegalInfo holds when no encounter matched.
        EvolutionEncounter getInvalidEvolutionEncounter(const EvolutionEntity &entity) noexcept;

        /// PKHeX EvolutionChain.GetEvolutionChainsAllGens (LegalInfo.EvoChainsAllGens).
        EvolutionHistory getEvolutionChainsAllGens(const EvolutionEntity &entity, const EvolutionEncounter &encounter) noexcept;

        /// PKHeX EvolutionChain.GetEvolutionChainsSearch. `encounterSpeciesId` 0 searches every
        /// species the Pokemon could have been encountered as.
        EvolutionHistory getEvolutionChainsSearch(const EvolutionEntity &entity, const EvolutionOrigin &origin, Enums::GameVersion contextGroup, uint16_t encounterSpeciesId = 0) noexcept;

        /// PKHeX EvolutionChain.GetOriginChain (the array form): the possible original states,
        /// loosened where a transfer lost the met level.
        EvolutionChain getOriginChain(const EvolutionEntity &entity, const EvolutionOrigin &origin, uint16_t encounterSpeciesId = 0, bool shouldDiscard = true) noexcept;

        /// PKHeX EncounterOrigin.GetOriginChain: the chain an encounter of `generation` in
        /// `contextGroup` is matched against, for a Pokemon from Generation 3 on.
        EvolutionChain getEncounterOriginChain(const EvolutionEntity &entity, uint8_t generation, Enums::GameVersion contextGroup) noexcept;

        /// PKHeX EncounterOrigin.GetOriginChain12: the same for a Generation 1 or 2 origin.
        EvolutionChain getEncounterOriginChainGameBoy(const EvolutionEntity &entity, uint8_t generation, Enums::GameVersion contextGroup) noexcept;
    }
}

#endif
