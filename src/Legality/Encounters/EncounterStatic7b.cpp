#include "Legality/Encounters/EncounterStatic7b.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX EncounterStatic7b.IsMatchExact
        bool EncounterStatic7b::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (pokemon.metLocation() != properties.location)
            {
                return false;
            }
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::GG, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            return true;
        }
    }
}
