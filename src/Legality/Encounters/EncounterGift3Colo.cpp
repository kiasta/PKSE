#include "Legality/Encounters/EncounterGift3Colo.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules3GC.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PLUSLE = 311;
        }

        // PKHeX EncounterGift3Colo.IsMatchExact
        bool EncounterGift3Colo::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (getOriginVersion(pokemon) != static_cast<uint8_t>(properties.version))
            {
                return false;
            }
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            const bool isFormat3 = getFormatGeneration(pokemon) == 3;
            if (isFormat3 && pokemon.metLocation() != properties.location)
            {
                return false;
            }
            if (isFormat3 ? pokemon.metLevel() != properties.level : evolution.levelMax < properties.level)
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        EncounterMatchRating EncounterGift3Colo::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (isJapaneseBonusDisk() && pokemon.language() != static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterGift3Colo.IsTrainerMatch. A Colosseum record (CK3) is never matched here:
        // PKSE stores no Colosseum format.
        bool EncounterGift3Colo::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            if (language < 0 || language >= static_cast<int>(properties.trainerNames->size()))
            {
                return false;
            }
            const char16_t *expectedText = (*properties.trainerNames)[language];
            std::u16string expectedName = expectedText != nullptr ? std::u16string(expectedText) : std::u16string();
            const size_t maxLength = language == static_cast<int>(Enums::LanguageID::Japanese) ? 5 : 7;
            if (expectedName.size() > maxLength)
            {
                expectedName.resize(maxLength);
            }
            if (getEntityContextGroup(pokemon) == Enums::GameVersion::RSE)
            {
                return trainerName == expectedName;
            }
            if (language == static_cast<int>(Enums::LanguageID::Spanish) && properties.speciesId == SPECIES_PLUSLE)
            {
                return trainerName == TRAINER_NAME_DUKING_SPANISH4;
            }
            return trainerName == expectedName;
        }
    }
}
