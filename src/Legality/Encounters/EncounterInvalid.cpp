#include "Legality/Encounters/EncounterInvalid.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterInvalid::EncounterInvalid(const Pokemon::Pokemon &pokemon)
        {
            const Evolutions::EvolutionEntity entity = Evolutions::getEvolutionEntity(pokemon);
            invalidSpeciesId = pokemon.speciesID();
            invalidFormId = pokemon.form();
            invalidLevelMin = pokemon.metLevel();
            invalidLevelMax = Evolutions::getEntityCurrentLevel(pokemon);
            wasEgg = Encounters::wasEgg(pokemon);
            invalidGeneration = static_cast<uint8_t>(entity.originGeneration);
            invalidVersion = static_cast<Enums::GameVersion>(entity.originVersion);
            invalidContextGroup = Evolutions::getEntityContextGroup(entity);
        }
    }
}
