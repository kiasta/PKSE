#include "Legality/Encounters/Encounterable.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX IEncounterTemplate.IsWithinEncounterRange
        bool Encounterable::isWithinEncounterRange(const Pokemon::Pokemon &pokemon) const
        {
            const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
            if (!hasOriginalMetLevel(pokemon))
            {
                return isLevelWithinRange(currentLevel);
            }
            if (isEgg())
            {
                return currentLevel == levelMin();
            }
            if (hasMetLevel())
            {
                return isLevelWithinRange(currentLevel);
            }
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel != 0)
            {
                return currentLevel == metLevel;
            }
            return isLevelWithinRange(currentLevel);
        }
    }
}
