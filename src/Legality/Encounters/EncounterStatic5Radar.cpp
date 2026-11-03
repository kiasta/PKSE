#include "Legality/Encounters/EncounterStatic5Radar.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX EncounterStatic5Radar.IsMatchExact
        bool EncounterStatic5Radar::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (pokemon.metLocation() != LOCATION_DREAM_RADAR5)
            {
                return false;
            }
            if (!isMatchLevel(pokemon.metLevel()))
            {
                return false;
            }
            if (radarFormId != evolution.formId && !isFormChangeable(radarSpeciesId, radarFormId, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }
    }
}
