#include "Legality/Encounters/EvolutionVerifier.h"

#include <string>

#include "Legality/Encounters/EncounterMoveSeam.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX IsValidEvolution: the current species is reachable from the encounter species.
            bool isValidEvolution(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                const Encounterable &encounter = *info.encounterOriginal();
                const uint16_t encounterSpecies = encounter.speciesId();
                const uint16_t currentSpecies = pokemon.speciesID();
                if (currentSpecies == encounterSpecies)
                {
                    return true; // never evolved
                }
                const Evolutions::EvolutionHistory &history = info.evolutionChainsAllGens();
                if (!history.hasVisitedAs(Evolutions::getEntityContextGroup(info.entity), currentSpecies))
                {
                    return false; // cannot exist as the current species
                }
                // The encounter must also have been able to exist as the encounter species.
                return history.hasVisitedAs(encounter.contextGroup(), encounterSpecies);
            }
        }

        CheckResult EvolutionVerifier::verifyEvolution(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
        {
            if (!isValidEvolution(pokemon, info))
            {
                return CheckResult::get(Judgement::Invalid, CheckIdentifier::Evolution, ResultCode::EvolutionInvalid);
            }
            if (!isValidEvolutionWithMove(info, pokemon))
            {
                return CheckResult::get(Judgement::Invalid, CheckIdentifier::Evolution, ResultCode::MoveEvolutionFCombination, Trainer::getSpeciesName(pokemon.speciesID()));
            }
            return CheckResult::getValid(CheckIdentifier::Evolution);
        }
    }
}
