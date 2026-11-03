#include "Legality/Encounters/EncounterTrade4RanchGift.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        std::u16string EncounterTrade4RanchGift::getTrainerName(int language)
        {
            switch (language)
            {
            case 1:
                return u"ユカリ";
            case 2:
                return u"Hayley";
            case 3:
                return u"EULALIE";
            case 4:
                return u"GIULIA";
            case 5:
                return u"EUKALIA";
            case 7:
                return u"Eulalia";
            default:
                return std::u16string();
            }
        }

        bool EncounterTrade4RanchGift::isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &trainerName, int language) const
        {
            return trainerName == getTrainerName(language);
        }

        // PKHeX EncounterTrade4RanchGift.IsMatchExact and its helpers
        bool EncounterTrade4RanchGift::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isFormat4 = getFormatGeneration(pokemon) == 4;
            if (isFormat4 ? pokemon.metLevel() != properties.metLevel : evolution.levelMax < properties.level)
            {
                return false;
            }
            if (pokemon.id32() != ID32())
            {
                return false;
            }
            if (pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.isFatefulEncounter ? isShiny(pokemon) : properties.PID != pokemon.encryptionConstant())
            {
                return false;
            }
            if (isFormat4 && pokemon.metLocation() != properties.location)
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::DP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            if (pokemon.eggLocation() != properties.eggLocation)
            {
                return false;
            }
            return !pokemon.isEgg();
        }
    }
}
