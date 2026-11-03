#include "Legality/Encounters/EncounterGift3NY.h"

#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGift3.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX Gen3PCNY. PCNYa never existed: the memory card labelled "A" wrote PCNYc.
            constexpr const char16_t *TRAINER_NAME_PCNY_B = u"PCNYb";
            constexpr const char16_t *TRAINER_NAME_PCNY_C = u"PCNYc";
            constexpr const char16_t *TRAINER_NAME_PCNY_D = u"PCNYd";
            constexpr uint16_t TRAINER_ID_LIMIT = 3000;

            /// PKHeX Distribution3NY.IsValidTrainerName: the machines that ran each distribution.
            bool isValidTrainerName(Distribution3NY distribution, const std::u16string &trainerName)
            {
                const bool isB = trainerName == TRAINER_NAME_PCNY_B;
                const bool isC = trainerName == TRAINER_NAME_PCNY_C;
                const bool isD = trainerName == TRAINER_NAME_PCNY_D;
                switch (distribution)
                {
                case Distribution3NY::Evolution:
                case Distribution3NY::Monster:
                case Distribution3NY::Halloween:
                case Distribution3NY::EXDragon:
                    return isB || isC;
                case Distribution3NY::Dragon:
                    // Only C and D ran it, but staff used B for a while when a machine was down.
                    return isB || isC || isD;
                case Distribution3NY::UnknownSpring:
                case Distribution3NY::Colosseum:
                case Distribution3NY::Box:
                    return isC || isD;
                case Distribution3NY::BabyTrade:
                case Distribution3NY::SlitherSwim:
                case Distribution3NY::AncientAliens:
                case Distribution3NY::Sixth:
                    return isD;
                }
                return false;
            }
        }

        // PKHeX EncounterGift3NY.IsMatchExact
        bool EncounterGift3NY::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // The Gen 3 version must be Ruby or Sapphire.
            if ((getVersion3Bit(getOriginVersion(pokemon)) & VERSION3_RUBY_SAPPHIRE) == 0)
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
            // PKHeX Distribution3NY.IsValidTrainerID.
            if (pokemon.tid16() == 0 || pokemon.tid16() >= TRAINER_ID_LIMIT)
            {
                return false;
            }
            if (!isValidTrainerName(properties.distribution, pokemon.otName()))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return false;
            }
            return isMatchDistributionMetData3(pokemon, properties.level, LOCATION);
        }

        // PKHeX EncounterGift3NY.IsCompatibleReviseReset: the gift's pick from the table is a separate
        // RNG call, so the correlation alone decides.
        RandomCorrelationRating EncounterGift3NY::isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &) const
        {
            const RNG::PIDType previousType = pidiv.mutated();
            const RNG::PIDType type = previousType == RNG::PIDType::None ? pidiv.type : previousType;
            return type == RNG::PIDType::BACDAntishinyXor ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }
    }
}
