#include "Legality/Encounters/MysteryGiftWC8.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX GameVersion.FRX: the id HOME writes for FireRed on Switch.
            constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
            /// PKHeX WC8.IsScalarFixed / GetHomeScalar: card 9029 fixes both size scalars at 128.
            constexpr uint16_t CARD_FIXED_SCALAR = 9029;
            constexpr uint8_t HOME_FIXED_SCALAR = 128;
            /// PKHeX WC8.IsMatchExact: card 122 exists twice, with and without a nickname.
            constexpr uint16_t CARD_DUPLICATE_NICKNAME = 122;

            /// PKHeX WC8.IsHOMEGiftOld: a HOME gift received before HOME 3.0.0 (2023-05-30). PKHeX
            /// compares only a valid met date; PKSE reads "no date" as month or day 0.
            bool hasMetDate(const Pokemon::Pokemon &pokemon) noexcept
            {
                return pokemon.metMonth() != 0 && pokemon.metDay() != 0;
            }

            bool isMetBeforeHome300(const Pokemon::Pokemon &pokemon) noexcept
            {
                const int metYear = 2000 + pokemon.metYear();
                if (metYear != 2023)
                {
                    return metYear < 2023;
                }
                if (pokemon.metMonth() != 5)
                {
                    return pokemon.metMonth() < 5;
                }
                return pokemon.metDay() < 30;
            }
        }

        RNG::Shiny MysteryGiftWC8::getFixedShinyType() const noexcept
        {
            if (isHomeGift() && isHomeShinyPossible())
            {
                return RNG::Shiny::Random;
            }
            // PKHeX GetShinyXor: a card without a fixed ID is the player's anti-shiny fixed PID.
            if (ID32() == 0)
            {
                return RNG::Shiny::Never;
            }
            const uint32_t PID = properties.PID;
            const uint32_t shinyXor = (PID >> 16) ^ (PID & 0xFFFF) ^ properties.TID16 ^ properties.SID16;
            if (shinyXor == 0)
            {
                return RNG::Shiny::AlwaysSquare;
            }
            return shinyXor <= 15 ? RNG::Shiny::AlwaysStar : RNG::Shiny::Never;
        }

        RNG::Shiny MysteryGiftWC8::shiny() const noexcept
        {
            switch (properties.shinyType)
            {
            case ShinyType8::FixedValue:
                return getFixedShinyType();
            case ShinyType8::Random:
                return RNG::Shiny::Random;
            case ShinyType8::AlwaysStar:
                return RNG::Shiny::AlwaysStar;
            case ShinyType8::AlwaysSquare:
                return RNG::Shiny::AlwaysSquare;
            default:
                return RNG::Shiny::Never;
            }
        }

        RNG::AbilityPermission MysteryGiftWC8::ability() const noexcept
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

        // PKHeX WC8.CanBeReceivedByVersion (IRestrictVersion): 1 Sword, 2 Shield, 3 both.
        bool MysteryGiftWC8::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            if (version == Enums::GameVersion::SW)
            {
                return (properties.restrictVersion & 1) != 0;
            }
            if (version == Enums::GameVersion::SH)
            {
                return (properties.restrictVersion & 2) != 0;
            }
            return false;
        }

        int getWonderCardLanguageSlot(int language) noexcept
        {
            constexpr int languageJapanese = 1;
            constexpr int languageEnglish = 2;
            constexpr int languageUnused = 6;
            constexpr int languageChineseTraditional = 10;
            if (language < languageJapanese || language == languageUnused || language > languageChineseTraditional)
            {
                language = languageEnglish;
            }
            return language < languageUnused ? language - 1 : language - 2;
        }

        bool canWonderCardBeAnyLanguage(std::span<const uint16_t, WONDER_CARD_LANGUAGE_SLOT_COUNT> languageWords) noexcept
        {
            for (const uint16_t languageWord : languageWords)
            {
                if (languageWord != 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool canWonderCardHaveLanguage(std::span<const uint16_t, WONDER_CARD_LANGUAGE_SLOT_COUNT> languageWords, int language) noexcept
        {
            constexpr int languageJapanese = 1;
            constexpr int languageChineseTraditional = 10;
            if (language < languageJapanese || language > languageChineseTraditional)
            {
                return false;
            }
            if (canWonderCardBeAnyLanguage(languageWords))
            {
                return true;
            }
            for (const uint16_t languageWord : languageWords)
            {
                if (languageWord == language)
                {
                    return true;
                }
            }
            // PKHeX GetLanguage reads only the low byte of the redeeming language's slot.
            return (languageWords[getWonderCardLanguageSlot(language)] & 0xFF) == 0;
        }

        std::u16string MysteryGiftWC8::getTrainerName(int language) const
        {
            const char16_t *text = properties.trainerNames[getWonderCardLanguageSlot(language)];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool MysteryGiftWC8::isNicknamed(int language) const noexcept
        {
            return ((properties.nicknamedSlots >> getWonderCardLanguageSlot(language)) & 1) != 0;
        }

        bool MysteryGiftWC8::canBeAnyLanguage() const noexcept
        {
            return canWonderCardBeAnyLanguage(properties.languageWords);
        }

        bool MysteryGiftWC8::canHaveLanguage(int language) const noexcept
        {
            return canWonderCardHaveLanguage(properties.languageWords, language);
        }

        // PKHeX WC8.GetPID for the fixed shiny states (2, 3) and the fixed value (4).
        uint32_t MysteryGiftWC8::getExpectedPID(const Pokemon::Pokemon &pokemon, ShinyType8 type) const noexcept
        {
            const uint32_t lowPID = properties.PID & 0xFFFF;
            const uint32_t trainerXor = pokemon.tid16() ^ pokemon.sid16();
            if (type == ShinyType8::AlwaysStar)
            {
                return ((1u ^ lowPID ^ trainerXor) << 16) | lowPID;
            }
            if (type == ShinyType8::AlwaysSquare)
            {
                return ((0u ^ lowPID ^ trainerXor) << 16) | lowPID;
            }
            // PKHeX GetFixedPID
            const uint32_t PID = properties.PID;
            if (PID != 0 && ID32() != 0)
            {
                return PID;
            }
            const uint32_t pidXor = (PID >> 16) ^ (PID & 0xFFFF) ^ trainerXor;
            const bool isShinyForTrainer = pidXor < 16;
            if (!isShinyForTrainer)
            {
                return PID;
            }
            if (isHomeGift() && !isHomeShinyPossible())
            {
                return PID ^ 0x10000000;
            }
            return PID;
        }

        // PKHeX WC8.IsMatchExact
        bool MysteryGiftWC8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isFormatPK8 = isFormatSwordShield(pokemon);
            if (!properties.isEgg)
            {
                if (properties.trainerGender < 2)
                {
                    if (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16() || properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                if (!canBeAnyLanguage() && !canHaveLanguage(pokemon.language()))
                {
                    return false;
                }
                const std::u16string trainerName = getTrainerName(pokemon.language());
                if (!trainerName.empty() && trainerName != pokemon.otName())
                {
                    return false;
                }

                const uint8_t originVersion = getOriginVersion(pokemon);
                if (properties.originGame == VERSION_FIRERED_SWITCH)
                {
                    // PKHeX: HOME Celebi (temporary workaround).
                    if (isFormatPK8)
                    {
                        if (!(originVersion == static_cast<uint8_t>(Enums::GameVersion::SW) && pokemon.metLocation() == LOCATION_HOME_SWFR))
                        {
                            return false;
                        }
                    }
                    else if (!(originVersion == VERSION_FIRERED_SWITCH && pokemon.metLocation() != LOCATION_HOME_VIRTUAL_CONSOLE3_8))
                    {
                        return false;
                    }
                }
                else if (properties.originGame != 0 && properties.originGame != originVersion)
                {
                    return false;
                }

                if (properties.encryptionConstant != 0)
                {
                    if (properties.encryptionConstant != pokemon.encryptionConstant())
                    {
                        return false;
                    }
                }
                else if (isHomeGift())
                {
                    // Before HOME 3.0.0 a HOME gift kept the card's zero encryption constant.
                    if (hasMetDate(pokemon) && isMetBeforeHome300(pokemon) && pokemon.encryptionConstant() != properties.encryptionConstant)
                    {
                        return false;
                    }
                    const RNG::Shiny giftShiny = shiny();
                    const bool isGiftShiny = giftShiny == RNG::Shiny::Always || giftShiny == RNG::Shiny::AlwaysSquare || giftShiny == RNG::Shiny::AlwaysStar;
                    if (isGiftShiny)
                    {
                        if (!isShiny(pokemon))
                        {
                            return false;
                        }
                    }
                    else if (isShiny(pokemon) && !isHomeShinyPossible())
                    {
                        return false;
                    }
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SWSH, getEntityContextGroup(pokemon)))
            {
                return false;
            }

            const RNG::Shiny shinyType = shiny();
            if (properties.isEgg)
            {
                if (properties.eggLocation != pokemon.eggLocation())
                {
                    if (pokemon.eggLocation() != LOCATION_LINK_TRADE6)
                    {
                        return false;
                    }
                    // A shiny traded egg always has shiny xor 0 or 1.
                    if (properties.shinyType == ShinyType8::Random && isShiny(pokemon) && getShinyXorValue(pokemon) > 1)
                    {
                        return false;
                    }
                }
                if (!isShinyValid(shinyType, pokemon))
                {
                    return false;
                }
                if (pokemon.isEgg() && getEntityContextGroup(pokemon) == Enums::GameVersion::SWSH)
                {
                    return false;
                }
            }
            else
            {
                if (!isShinyValid(shinyType, pokemon))
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

            if (properties.metLevel != 0 && properties.metLevel != pokemon.metLevel())
            {
                return false;
            }
            const uint8_t expectedBall = properties.ball == 0 ? static_cast<uint8_t>(Enums::Ball::Poke) : properties.ball;
            if (expectedBall != pokemon.ball())
            {
                return false;
            }
            if (properties.trainerGender < 2 && properties.trainerGender != pokemon.otGender())
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
            if (isFormatPK8 && canGigantamax(pokemon) != properties.canGigantamax && !canToggleGigantamax(pokemon.speciesID(), pokemon.form(), properties.speciesId))
            {
                return false;
            }
            if (isFormatPK8 && getDynamaxLevel(pokemon) < properties.dynamaxLevel)
            {
                return false;
            }

            if (isHomeGift() && hasMetDate(pokemon) && !isMetBeforeHome300(pokemon))
            {
                // HOME 3.0.0 re-rolls the size of anything not received as an old HOME gift.
                if (pokemon.heightScalar() == 0 && pokemon.weightScalar() == 0)
                {
                    return false;
                }
            }
            if (properties.cardId == CARD_FIXED_SCALAR)
            {
                if (pokemon.heightScalar() != HOME_FIXED_SCALAR || pokemon.weightScalar() != HOME_FIXED_SCALAR)
                {
                    return false;
                }
                if (pokemon.hasScale() && pokemon.scale() != HOME_FIXED_SCALAR)
                {
                    return false;
                }
            }

            if (properties.cardId == CARD_DUPLICATE_NICKNAME && pokemon.isNicknamed() != isNicknamed(pokemon.language()))
            {
                return false;
            }

            // Types 0 and 1 do not use the fixed PID; 2-4 can change on a traded egg to keep the shiny state.
            if (properties.shinyType == ShinyType8::Never || properties.shinyType == ShinyType8::Random)
            {
                return true;
            }
            return pokemon.pid() == getExpectedPID(pokemon, properties.shinyType);
        }
    }
}
