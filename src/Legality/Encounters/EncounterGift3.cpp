#include "Legality/Encounters/EncounterGift3.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/RNG/CommonEvent3.h"
#include "Legality/RNG/CommonEvent3Checker.h"
#include "Legality/RNG/LCRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t MOVE_WISH = 273;
            constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        }

        uint8_t getVersion3Bit(uint8_t version) noexcept
        {
            switch (static_cast<Enums::GameVersion>(version))
            {
            case Enums::GameVersion::SA:
                return VERSION3_SAPPHIRE;
            case Enums::GameVersion::RU:
                return VERSION3_RUBY;
            case Enums::GameVersion::EM:
                return VERSION3_EMERALD;
            case Enums::GameVersion::FR:
                return VERSION3_FIRERED;
            case Enums::GameVersion::LG:
                return VERSION3_LEAFGREEN;
            default:
                return version == VERSION_COLOSSEUM_XD ? VERSION3_COLOSSEUM_XD : 0;
            }
        }

        Enums::GameVersion EncounterGift3::version() const noexcept
        {
            switch (properties.versionMask)
            {
            case VERSION3_SAPPHIRE:
                return Enums::GameVersion::SA;
            case VERSION3_RUBY:
                return Enums::GameVersion::RU;
            case VERSION3_EMERALD:
                return Enums::GameVersion::EM;
            case VERSION3_FIRERED:
                return Enums::GameVersion::FR;
            case VERSION3_LEAFGREEN:
                return Enums::GameVersion::LG;
            case VERSION3_FIRERED_LEAFGREEN:
                return Enums::GameVersion::FRLG;
            default:
                return Enums::GameVersion::RSE;
            }
        }

        bool EncounterGift3::isFixedTrainer() const noexcept
        {
            for (const char16_t *character = properties.originalTrainerName; *character != u'\0'; character++)
            {
                if (*character != u' ' && *character != u'\t' && *character != u'　')
                {
                    return true;
                }
            }
            return false;
        }

        uint32_t EncounterGift3::getGender(uint32_t seed) const noexcept
        {
            using RNG::CommonEvent3;
            using RNG::LCRNG;
            switch (properties.originalTrainerGender)
            {
            case GiftGender3::Only0:
            case GiftGender3::RandD3_0:
                return 0;
            case GiftGender3::Only1:
            case GiftGender3::RandD3_1:
                return 1;
            case GiftGender3::RandD3:
                return CommonEvent3::getGenderBit0(LCRNG::next(seed) >> 16);
            case GiftGender3::RandS3:
                return CommonEvent3::getGenderBit3(LCRNG::next(seed) >> 16);
            case GiftGender3::RandS7:
                return CommonEvent3::getGenderBit7(LCRNG::next(seed) >> 16);
            case GiftGender3::RandSG15:
                return CommonEvent3::getGenderBit15(LCRNG::next2(seed) >> 16);
            default:
                return 0;
            }
        }

        bool EncounterGift3::isGenderSpecificMatch(uint8_t gender) const noexcept
        {
            switch (properties.originalTrainerGender)
            {
            case GiftGender3::Only0:
            case GiftGender3::RandD3_0:
                return gender == 0;
            case GiftGender3::Only1:
            case GiftGender3::RandD3_1:
                return gender == 1;
            default:
                return true;
            }
        }

        // PKHeX EncounterGift3.IsMatchGender: the generator advances once more inside.
        bool EncounterGift3::isMatchGender(const Pokemon::Pokemon &pokemon, uint32_t seed) const noexcept
        {
            uint32_t expectedGender = 0;
            if (properties.originalTrainerGender == GiftGender3::RandD3_0 || properties.originalTrainerGender == GiftGender3::RandD3_1)
            {
                expectedGender = RNG::CommonEvent3::getGenderBit0(RNG::LCRNG::next6(seed) >> 16);
            }
            else
            {
                expectedGender = getGender(RNG::LCRNG::next4(seed));
            }
            return pokemon.otGender() == expectedGender;
        }

        // PKHeX EncounterGift3.IsMatchExact
        bool EncounterGift3::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if ((properties.versionMask & getVersion3Bit(getOriginVersion(pokemon))) == 0)
            {
                return false;
            }
            if (pokemon.isEgg() && !properties.isEgg)
            {
                return false;
            }
            const bool isHatchedEgg = properties.isEgg && !pokemon.isEgg();
            if (!isHatchedEgg)
            {
                if (properties.SID16 != UNSPECIFIED_ID && properties.SID16 != pokemon.sid16())
                {
                    return false;
                }
                if (properties.TID16 != UNSPECIFIED_ID && properties.TID16 != pokemon.tid16())
                {
                    return false;
                }
                if (!isGenderSpecificMatch(pokemon.otGender()))
                {
                    return false;
                }
                if (properties.originalTrainerName[0] != u'\0' && pokemon.otName() != properties.originalTrainerName)
                {
                    return false;
                }
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.language != 0)
            {
                if (properties.language != pokemon.language())
                {
                    return false;
                }
            }
            else if (!properties.isEgg && pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return false;
            }
            if (pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke))
            {
                return false;
            }
            if (properties.isFatefulEncounter != pokemon.isFatefulEncounter() && !properties.isEgg)
            {
                return false;
            }
            if (getFormatGeneration(pokemon) == 3)
            {
                if (isHatchedEgg)
                {
                    return true;
                }
                return properties.metLevel == pokemon.metLevel() && pokemon.metLocation() == location();
            }
            if (pokemon.isEgg())
            {
                return false;
            }
            if (properties.level > pokemon.metLevel())
            {
                return false;
            }
            const uint16_t noneLocation = getEntityContextGroup(pokemon) == Enums::GameVersion::BDSP ? LOCATION_DEFAULT8B_NONE : 0;
            return pokemon.eggLocation() == noneLocation;
        }

        EncounterMatchRating EncounterGift3::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.isEgg && pokemon.isEgg() && properties.isFatefulEncounter != pokemon.isFatefulEncounter())
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterGift3.IsCompatibleReviseReset
        RandomCorrelationRating EncounterGift3::isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const
        {
            using RNG::CommonEvent3Checker;
            using RNG::PIDType;
            const PIDType previousType = pidiv.mutated();
            const PIDType type = previousType == PIDType::None ? pidiv.type : previousType;
            if ((type == PIDType::BACDEggAntishiny || type == PIDType::BACDEggShiny) && !properties.isEgg)
            {
                return RandomCorrelationRating::Mismatch;
            }
            const bool isAlgorithmicGender = properties.originalTrainerGender == GiftGender3::RandAlgo || properties.originalTrainerGender == GiftGender3::Recipient;
            if (!isAlgorithmicGender && (!properties.isEgg || pokemon.isEgg()) && !isMatchGender(pokemon, pidiv.originSeed))
            {
                return RandomCorrelationRating::Mismatch;
            }
            const bool hasWish = properties.moves.contains(MOVE_WISH);
            bool isMatch = false;
            switch (properties.method)
            {
            case PIDType::BACDUnrestricted:
                isMatch = type == PIDType::BACD;
                break;
            case PIDType::BACDRestricted:
                isMatch = CommonEvent3Checker::isRestrictedSimple(pidiv, type);
                break;
            case PIDType::BACDRestrictedAntishiny:
                isMatch = CommonEvent3Checker::isRestrictedAnti(pidiv, type);
                break;
            case PIDType::BACDUnrestrictedAntishinyXor:
                isMatch = CommonEvent3Checker::isUnrestrictedAntiX(pidiv, type);
                break;
            case PIDType::BACDTableAntishiny:
                isMatch = CommonEvent3Checker::isRestrictedTable2(pidiv, type, properties.speciesId, hasWish);
                break;
            case PIDType::BACDTableShiny:
                isMatch = CommonEvent3Checker::isRestrictedTable3(pidiv, type, properties.speciesId, hasWish);
                break;
            case PIDType::BACDRestrictedBinaryCodedDecimal:
                isMatch = CommonEvent3Checker::isBerryFixShiny(pidiv, type);
                break;
            case PIDType::BACDMystryMew:
                isMatch = CommonEvent3Checker::isMystryMew(pidiv, type);
                break;
            case PIDType::Channel:
                isMatch = CommonEvent3Checker::isChannelJirachi(pidiv, type);
                break;
            case PIDType::Method2:
                // PID modulo VBlank abuse
                isMatch = type == PIDType::Method2 || type == PIDType::Method1 || type == PIDType::Method4;
                break;
            default:
                isMatch = false;
                break;
            }
            return isMatch ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }

        bool isMatchDistributionMetData3(const Pokemon::Pokemon &pokemon, uint8_t level, uint16_t location)
        {
            if (pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke))
            {
                return false;
            }
            if (getFormatGeneration(pokemon) == 3)
            {
                return level == pokemon.metLevel() && location == pokemon.metLocation();
            }
            if (pokemon.isEgg())
            {
                return false;
            }
            if (level > pokemon.metLevel())
            {
                return false;
            }
            const uint16_t noneLocation = getEntityContextGroup(pokemon) == Enums::GameVersion::BDSP ? LOCATION_DEFAULT8B_NONE : 0;
            return pokemon.eggLocation() == noneLocation;
        }
    }
}
