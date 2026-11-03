#include "Legality/Encounters/EncounterGeneratorGO.h"

#include "Legality/Encounters/EncounterEnumeratorGO.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGeneratorGO::getEncountersLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            for (const MatchedEncounter &matched : EncounterEnumeratorGO::enumerateLetsGo(pokemon, chain))
            {
                co_yield matched.encounter;
            }
        }

        EncounterCandidates EncounterGeneratorGO::getEncountersHome(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            for (const MatchedEncounter &matched : EncounterEnumeratorGO::enumerateHome(pokemon, chain))
            {
                co_yield matched.encounter;
            }
        }

        EncounterCandidates EncounterGeneratorGO::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == LOCATION_GO7)
            {
                return getEncountersLetsGo(pokemon, chain);
            }
            // A Let's Go record (PKHeX PB7) cannot hold a HOME transfer.
            if (metLocation == LOCATION_GO8 && pokemon.getGameGroup() != Enums::GameVersion::GG)
            {
                return getEncountersHome(pokemon, chain);
            }
            return EncounterCandidates();
        }
    }
}
