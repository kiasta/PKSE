#include "Legality/Encounters/EncounterGenerator8a.h"

#include "Legality/Encounters/EncounterEnumerator8a.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGenerator8a::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            for (const MatchedEncounter &matched : EncounterEnumerator8a::enumerate(pokemon, chain))
            {
                co_yield matched.encounter;
            }
        }
    }
}
