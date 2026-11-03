#include "Legality/Encounters/EncounterGenerator1.h"

#include "Legality/Encounters/EncounterEnumerator1.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGenerator1::getEncounters(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChainGameBoy(info.entity, 1, Enums::GameVersion::RBY);
            if (chain.isEmpty())
            {
                co_return;
            }
            for (const MatchedEncounter &matched : EncounterEnumerator1::enumerate(pokemon, chain, info))
            {
                co_yield matched.encounter;
            }
        }
    }
}
