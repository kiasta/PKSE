#include "Legality/Encounters/EncounterStatic5N.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_ZORUA = 570;
            constexpr uint8_t GENDER_RATIO_GENDERLESS = 255;
            constexpr uint8_t GENDER_MALE = 0;
            constexpr uint8_t GENDER_GENDERLESS = 2;
        }

        Enums::Ball EncounterStatic5N::fixedBall() const noexcept
        {
            // N's Zorua is given, not caught; the others can be caught in any ball.
            return properties.speciesId == SPECIES_ZORUA ? Enums::Ball::Poke : Enums::Ball::None;
        }

        // PKHeX EncounterStatic5N.IsMatchExact
        bool EncounterStatic5N::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (properties.PID != pokemon.pid())
            {
                return false;
            }
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
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            const Pokemon::PersonalRecord &personal = Pokemon::getPersonalRecord(Enums::GameVersion::B2W2, properties.speciesId, 0);
            const uint8_t expectedGender = personal.genderRatio == GENDER_RATIO_GENDERLESS ? GENDER_GENDERLESS : GENDER_MALE;
            if (pokemon.gender() != expectedGender)
            {
                return false;
            }
            if (pokemon.otGender() != 0)
            {
                return false;
            }
            if (pokemon.ivHP() != INDIVIDUAL_VALUE || pokemon.ivATK() != INDIVIDUAL_VALUE || pokemon.ivDEF() != INDIVIDUAL_VALUE || pokemon.ivSPA() != INDIVIDUAL_VALUE || pokemon.ivSPD() != INDIVIDUAL_VALUE || pokemon.ivSPE() != INDIVIDUAL_VALUE)
            {
                return false;
            }
            return pokemon.id32() == ID32;
        }

        // PKHeX EncounterStatic5N.IsTrainerMatch: N writes his name full-width in Japanese.
        bool EncounterStatic5N::isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &trainerName, int language) const
        {
            const char16_t *expected = language == static_cast<int>(Enums::LanguageID::Japanese) ? u"Ｎ" : u"N";
            return trainerName == expected;
        }
    }
}
