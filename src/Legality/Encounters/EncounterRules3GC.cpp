#include "Legality/Encounters/EncounterRules3GC.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterShadow3Colo.h"
#include "Legality/Encounters/EncounterShadow3XD.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        bool isMatchLevelGameCube(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution, uint8_t level)
        {
            if (getFormatGeneration(pokemon) != 3)
            {
                return evolution.levelMax >= level;
            }
            return pokemon.metLevel() == level;
        }

        bool isMatchLocationGameCube(const Pokemon::Pokemon &pokemon, uint16_t location)
        {
            if (getFormatGeneration(pokemon) != 3)
            {
                return true;
            }
            return pokemon.metLocation() == location;
        }

        bool isMatchExactGameCube(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution, uint16_t speciesId, uint8_t level, uint16_t location)
        {
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (!isMatchLocationGameCube(pokemon, location))
            {
                return false;
            }
            if (!isMatchLevelGameCube(pokemon, evolution, level))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(speciesId, 0, pokemon.formID(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        bool isShadowEncounter(const Encounterable &encounter) noexcept
        {
            return EncounterShadow3Colo::isTemplateType(encounter.templateType()) || EncounterShadow3XD::isTemplateType(encounter.templateType());
        }
    }
}
