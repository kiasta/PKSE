#include "Legality/Encounters/EncounterGenerator9a.h"

#include "Legality/Encounters/EncounterEnumerator9a.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGenerator9a::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            for (const MatchedEncounter &matched : EncounterEnumerator9a::enumerate(pokemon, chain))
            {
                co_yield matched.encounter;
            }
        }
    }
}
