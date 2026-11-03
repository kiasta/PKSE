#include "Legality/Encounters/EncounterGift3JPN.h"

#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGift3.h"
#include "Legality/RNG/CommonEvent3.h"
#include "Legality/RNG/LCRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX Distribution3JPN.GetTrainerID.
            uint16_t getTrainerId(Distribution3JPN distribution) noexcept
            {
                switch (distribution)
                {
                case Distribution3JPN::First:
                    return 51126;
                case Distribution3JPN::Second:
                    return 51224;
                case Distribution3JPN::Third:
                    return 60114;
                case Distribution3JPN::Fourth:
                    return 60227;
                case Distribution3JPN::Fifth:
                    return 60321;
                case Distribution3JPN::Sixth:
                    return 60505;
                }
                return 0;
            }

            /// PKHeX Distribution3JPN.IsTrainerNameValid: the city of the Pokemon Center; Sapporo did
            /// not run the sixth distribution.
            bool isTrainerNameValid(Distribution3JPN distribution, const std::u16string &trainerName)
            {
                if (trainerName == u"トウキョー" || trainerName == u"ヨコハマ" || trainerName == u"ナゴヤ" || trainerName == u"オーサカ" || trainerName == u"フクオカ")
                {
                    return true;
                }
                return distribution != Distribution3JPN::Sixth && trainerName == u"サッポロ";
            }
        }

        // PKHeX EncounterGift3JPN.IsMatchExact
        bool EncounterGift3JPN::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // The Gen 3 version must be Ruby.
            if ((getVersion3Bit(getOriginVersion(pokemon)) & VERSION3_RUBY) == 0)
            {
                return false;
            }
            if (pokemon.isEgg())
            {
                return false;
            }
            if (pokemon.sid16() != 0)
            {
                return false;
            }
            if (pokemon.tid16() != getTrainerId(properties.distribution))
            {
                return false;
            }
            if (!isTrainerNameValid(properties.distribution, pokemon.otName()))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.language() != static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return false;
            }
            return isMatchDistributionMetData3(pokemon, LEVEL, LOCATION);
        }

        // PKHeX EncounterGift3JPN.IsCompatibleReviseReset: the fifth call after the origin seed picks
        // the OT gender; the gift's pick from the table is a separate RNG call.
        RandomCorrelationRating EncounterGift3JPN::isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const
        {
            const RNG::PIDType previousType = pidiv.mutated();
            const RNG::PIDType type = previousType == RNG::PIDType::None ? pidiv.type : previousType;
            if (type != RNG::PIDType::BACD && type != RNG::PIDType::BACDRestricted)
            {
                return RandomCorrelationRating::Mismatch;
            }
            const uint32_t random16 = RNG::LCRNG::next5(pidiv.originSeed) >> 16;
            if (pokemon.otGender() != RNG::CommonEvent3::getGenderBit7(random16))
            {
                return RandomCorrelationRating::Mismatch;
            }
            return RandomCorrelationRating::Match;
        }
    }
}
