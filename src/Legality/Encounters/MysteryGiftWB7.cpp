#include "Legality/Encounters/MysteryGiftWB7.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX WB7.IsHeightWeightFixed: the HOME card whose height and weight scalars are 128.
            constexpr uint16_t CARD_FIXED_SIZE = 9028;
            constexpr uint8_t FIXED_SIZE_SCALAR = 128;

            /// PKHeX WB7.GetLanguageIndex: English for a language outside the card's nine.
            int getLanguageIndex(int language) noexcept
            {
                constexpr int languageJapanese = static_cast<int>(Enums::LanguageID::Japanese);
                constexpr int languageUnused = 6;
                constexpr int languageChineseTraditional = static_cast<int>(Enums::LanguageID::ChineseTraditional);
                if (language < languageJapanese || language == languageUnused || language > languageChineseTraditional)
                {
                    return static_cast<int>(Enums::LanguageID::English);
                }
                return language < languageUnused ? language - 1 : language - 2;
            }

            /// PKHeX `pk is IScaledSize`: Let's Go and every format from Sword/Shield on store the scalars.
            bool hasSizeScalars(const Pokemon::Pokemon &pokemon) noexcept
            {
                return pokemon.getGameGroup() == Enums::GameVersion::GG || getFormatGeneration(pokemon) >= 8;
            }

            /// PKHeX IAwakened.IsAwakeningBelow.
            bool isAwakeningBelow(const Pokemon::Pokemon &pokemon, const std::array<uint8_t, 6> &initial) noexcept
            {
                return pokemon.avHP() < initial[0] || pokemon.avATK() < initial[1] || pokemon.avDEF() < initial[2] || pokemon.avSPE() < initial[3] || pokemon.avSPA() < initial[4] || pokemon.avSPD() < initial[5];
            }
        }

        // PKHeX WB7.Shiny and FixedShinyType
        RNG::Shiny MysteryGiftWB7::shiny() const noexcept
        {
            switch (properties.pidType)
            {
            case ShinyType6::FixedValue:
            {
                const uint32_t shinyXor = (properties.PID >> 16) ^ (properties.PID & 0xFFFF) ^ properties.TID16 ^ properties.SID16;
                return shinyXor <= 15 ? RNG::Shiny::Always : RNG::Shiny::Never;
            }
            case ShinyType6::Random:
                return RNG::Shiny::Random;
            case ShinyType6::Always:
                return RNG::Shiny::Always;
            default:
                return RNG::Shiny::Never;
            }
        }

        RNG::AbilityPermission MysteryGiftWB7::ability() const noexcept
        {
            switch (properties.abilityType)
            {
            case 0:
                return RNG::AbilityPermission::OnlyFirst;
            case 1:
                return RNG::AbilityPermission::OnlySecond;
            case 2:
                return RNG::AbilityPermission::OnlyHidden;
            case 3:
                return RNG::AbilityPermission::Any12;
            default:
                return RNG::AbilityPermission::Any12H;
            }
        }

        // PKHeX WB7.CanBeReceivedByVersion: bit 0 Let's Go Pikachu, bit 1 Let's Go Eevee.
        bool MysteryGiftWB7::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            if (version != Enums::GameVersion::GP && version != Enums::GameVersion::GE)
            {
                return false;
            }
            if (properties.restrictVersion == 0)
            {
                return true;
            }
            const int bitIndex = static_cast<int>(version) - static_cast<int>(Enums::GameVersion::GP);
            return (properties.restrictVersion & (1 << bitIndex)) != 0;
        }

        bool MysteryGiftWB7::canBeAnyLanguage() const noexcept
        {
            for (const uint8_t language : properties.languages)
            {
                if (language != 0)
                {
                    return false;
                }
            }
            return !isMainlandChinaGift();
        }

        bool MysteryGiftWB7::canHaveLanguage(int language) const noexcept
        {
            if (language < static_cast<int>(Enums::LanguageID::Japanese) || language > static_cast<int>(Enums::LanguageID::ChineseTraditional))
            {
                return false;
            }
            if (isMainlandChinaGift())
            {
                return language == static_cast<int>(Enums::LanguageID::ChineseSimplified);
            }
            if (canBeAnyLanguage())
            {
                return true;
            }
            for (const uint8_t cardLanguage : properties.languages)
            {
                if (cardLanguage == language)
                {
                    return true;
                }
            }
            return false;
        }

        std::u16string MysteryGiftWB7::getTrainerName(int language) const
        {
            const char16_t *trainerName = properties.trainerNames[getLanguageIndex(language)];
            return trainerName == nullptr ? std::u16string() : std::u16string(trainerName);
        }

        // PKHeX WB7.IsMatchExact. IsMatchEggLocationInternal is MysteryGift's default.
        bool MysteryGiftWB7::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!properties.isEgg)
            {
                if (properties.trainerGender != 3)
                {
                    if (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16() || properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                const std::u16string trainerName = getTrainerName(pokemon.language());
                if (!trainerName.empty() && trainerName != pokemon.otName())
                {
                    return false;
                }
                if (properties.originGame != 0 && properties.originGame != getOriginVersion(pokemon))
                {
                    return false;
                }
                if (properties.encryptionConstant != 0 && properties.encryptionConstant != pokemon.encryptionConstant())
                {
                    return false;
                }
                if (!isMatchEggLocation(pokemon, properties.eggLocation))
                {
                    return false;
                }
                if (!canBeAnyLanguage() && !canHaveLanguage(pokemon.language()))
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::GG, getEntityContextGroup(pokemon)))
            {
                return false;
            }

            if (properties.isEgg)
            {
                if (properties.eggLocation != pokemon.eggLocation())
                {
                    if (pokemon.eggLocation() != LOCATION_LINK_TRADE6)
                    {
                        return false;
                    }
                }
                else if (properties.pidType == ShinyType6::FixedValue && isShiny(pokemon))
                {
                    // An egg kept in its own game cannot be traded away for a different shiny state.
                    return false;
                }
                if (pokemon.isEgg() && getEntityContextGroup(pokemon) != Enums::GameVersion::GG)
                {
                    return false;
                }
            }
            else
            {
                if (!isShinyValid(shiny(), pokemon))
                {
                    return false;
                }
                if (!isMatchEggLocation(pokemon, properties.eggLocation))
                {
                    return false;
                }
                if (properties.location != pokemon.metLocation())
                {
                    return false;
                }
            }

            if (properties.metLevel != pokemon.metLevel())
            {
                return false;
            }
            if (properties.ball != pokemon.ball())
            {
                return false;
            }
            if (properties.trainerGender < 3 && properties.trainerGender != pokemon.otGender())
            {
                return false;
            }
            if (properties.nature != 0xFF && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (properties.gender != 3 && properties.gender != pokemon.gender())
            {
                return false;
            }
            if (properties.cardId == CARD_FIXED_SIZE)
            {
                if (hasSizeScalars(pokemon) && (pokemon.heightScalar() != FIXED_SIZE_SCALAR || pokemon.weightScalar() != FIXED_SIZE_SCALAR))
                {
                    return false;
                }
                if (pokemon.hasScale() && pokemon.scale() != FIXED_SIZE_SCALAR)
                {
                    return false;
                }
            }
            if (pokemon.hasAwakeningValues() && isAwakeningBelow(pokemon, properties.awakeningValues))
            {
                return false;
            }
            return properties.pidType != ShinyType6::FixedValue || pokemon.pid() == properties.PID;
        }
    }
}
