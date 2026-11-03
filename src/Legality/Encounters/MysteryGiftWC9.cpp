#include "Legality/Encounters/MysteryGiftWC9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/RNG/Encounter9RNG.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Pokemon/Pokemon.h"
#include "Legality/RNG/ShinyUtil.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX WC9.Scale: 256 lets the redeemed Pokemon's scale be random.
            constexpr uint16_t SCALE_RANDOM = 256;
            /// PKHeX HomeQuirks.IsGlitchedHisuianZoroarkSV: the Hisuian Zoroark card HOME bumped to 255 sizes.
            constexpr uint16_t CARD_GLITCHED_ZOROARK = 1513;
            constexpr uint8_t GLITCHED_SIZE = 255;

            bool isLeapYear(int year) noexcept
            {
                return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
            }

            /// PKHeX PKM.MetDate as year * 10000 + month * 100 + day; false when the stored date is not a real date.
            bool tryGetMetDate(const Pokemon::Pokemon &pokemon, int &packedDate) noexcept
            {
                constexpr int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                const int metYear = 2000 + pokemon.metYear();
                const int metMonth = pokemon.metMonth();
                const int metDay = pokemon.metDay();
                if (metMonth < 1 || metMonth > 12 || metDay < 1)
                {
                    return false;
                }
                const int monthLength = daysInMonth[metMonth - 1] + (metMonth == 2 && isLeapYear(metYear) ? 1 : 0);
                if (metDay > monthLength)
                {
                    return false;
                }
                packedDate = metYear * 10000 + metMonth * 100 + metDay;
                return true;
            }

            /// PKHeX WC9.IsBeforePatch200 (2023-09-13 and earlier: the old ID32 handling).
            bool isBeforePatch200(int packedDate) noexcept
            {
                return packedDate <= 20230913;
            }

            /// PKHeX WC9.IsBeforePatch120 (before 2023-03-01: no scale was fixed).
            bool isBeforePatch120(int packedDate) noexcept
            {
                return packedDate < 20230301;
            }

            /// PKHeX WC9.IsBeforePatch120(cardID): the cards first distributed before S/V 1.2.0.
            bool isCardBeforePatch120(uint16_t cardId) noexcept
            {
                return cardId == 1 || cardId == 6 || cardId == 501 || cardId == 1501;
            }
        }

        RNG::Shiny MysteryGiftWC9::shiny() const noexcept
        {
            switch (properties.shinyType)
            {
            case ShinyType8::FixedValue:
            {
                // PKHeX FixedShinyType: a card without a fixed ID is the player's anti-shiny fixed PID.
                if (properties.ID32 == 0)
                {
                    return RNG::Shiny::Never;
                }
                const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(properties.PID, properties.ID32);
                if (shinyXor == 0)
                {
                    return RNG::Shiny::AlwaysSquare;
                }
                return shinyXor <= 15 ? RNG::Shiny::AlwaysStar : RNG::Shiny::Never;
            }
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

        RNG::AbilityPermission MysteryGiftWC9::ability() const noexcept
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

        // PKHeX WC9.CanBeReceivedByVersion for a game alone: 1 Scarlet, 2 Violet, 0 or 3 both.
        bool MysteryGiftWC9::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            const bool isAnyVersion = properties.restrictVersion == 0 || properties.restrictVersion == 3;
            if (version == Enums::GameVersion::SL)
            {
                return isAnyVersion || properties.restrictVersion == 1;
            }
            if (version == Enums::GameVersion::VL)
            {
                return isAnyVersion || properties.restrictVersion == 2;
            }
            return false;
        }

        // PKHeX WC9.CanBeReceivedByVersion(PKM)
        bool MysteryGiftWC9::canBeReceivedBy(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t version = getOriginVersion(pokemon);
            const uint16_t metLocation = pokemon.metLocation();
            const bool isScarlet = version == static_cast<uint8_t>(Enums::GameVersion::SL) || metLocation == LOCATION_HOME_SWSL;
            const bool isViolet = version == static_cast<uint8_t>(Enums::GameVersion::VL) || metLocation == LOCATION_HOME_SHVL;
            switch (properties.restrictVersion)
            {
            case 1:
                return isScarlet;
            case 2:
                return isViolet;
            case 0:
            case 3:
                return isScarlet || isViolet;
            default:
                return false;
            }
        }

        std::u16string MysteryGiftWC9::getTrainerName(int language) const
        {
            const char16_t *text = properties.trainerNames[getWonderCardLanguageSlot(language)];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool MysteryGiftWC9::isNicknamed(int language) const noexcept
        {
            return ((properties.nicknamedSlots >> getWonderCardLanguageSlot(language)) & 1) != 0;
        }

        // PKHeX WC9's language-slot code is WC8's, line for line.
        bool MysteryGiftWC9::canBeAnyLanguage() const noexcept
        {
            return canWonderCardBeAnyLanguage(properties.languageWords);
        }

        bool MysteryGiftWC9::canHaveLanguage(int language) const noexcept
        {
            return canWonderCardHaveLanguage(properties.languageWords, language);
        }

        // PKHeX WC9.GetPID for the fixed shiny states (2, 3) and the fixed value (4).
        uint32_t MysteryGiftWC9::getExpectedPID(const Pokemon::Pokemon &pokemon, ShinyType8 type) const noexcept
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
            // PKHeX GetFixedPID returns the card's PID on every path.
            return properties.PID;
        }

        // PKHeX WC9.IsMatchExact
        bool MysteryGiftWC9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!properties.isEgg)
            {
                if (properties.trainerGender < 2)
                {
                    int packedDate = 0;
                    const bool isOldID = tryGetMetDate(pokemon, packedDate) && isBeforePatch200(packedDate);
                    const uint32_t expectedID32 = isOldID ? ID32Old() : properties.ID32;
                    if (expectedID32 != pokemon.id32())
                    {
                        return false;
                    }
                    if (properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                const int language = pokemon.language();
                if (!canBeAnyLanguage() && !canHaveLanguage(language))
                {
                    return false;
                }
                // PKHeX also accepts a trainer name HOME replaced (ReplaceTrainerNameHOME), which PKSE does not model.
                const std::u16string trainerName = getTrainerName(language);
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
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SV, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.hasTeraType() && pokemon.teraTypeOriginal() != properties.teraTypeOriginal)
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
                if (pokemon.isEgg() && getEntityContextGroup(pokemon) != Enums::GameVersion::SV)
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
                if (!isMatchLocation9(pokemon, pokemon.metLocation() == properties.location, properties.location, Enums::GameVersion::SV))
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
            if (RNG::HomeQuirks::hasSizeScalars(pokemon) && !isMatchSize(pokemon))
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

        // PKHeX WC9.IsMatchSize
        bool MysteryGiftWC9::isMatchSize(const Pokemon::Pokemon &pokemon) const
        {
            // PKHeX HomeQuirks.IsGlitchedHisuianZoroarkSV: HOME misapplied Legends: Arceus's alpha fix to this card.
            const bool hasNoTracker = pokemon.hasHomeTracker() && pokemon.homeTracker() == 0;
            if (!hasNoTracker && properties.cardId == CARD_GLITCHED_ZOROARK && pokemon.heightScalar() == GLITCHED_SIZE && pokemon.weightScalar() == GLITCHED_SIZE)
            {
                if (!pokemon.hasScale() || pokemon.scale() == GLITCHED_SIZE)
                {
                    return true;
                }
            }

            if (!RNG::Encounter9RNG::isHeightMatchSV(pokemon, static_cast<uint8_t>(properties.heightValue)))
            {
                return false;
            }
            if (pokemon.weightScalar() != properties.weightValue)
            {
                return false;
            }
            // S/V 1.2.0 added a scale to every card; a card from before it is random if redeemed before the patch.
            int packedDate = 0;
            if (isCardBeforePatch120(properties.cardId) && (!tryGetMetDate(pokemon, packedDate) || isBeforePatch120(packedDate)))
            {
                return true;
            }
            if (properties.scale == SCALE_RANDOM)
            {
                return true;
            }
            const uint8_t currentSize = pokemon.hasScale() ? pokemon.scale() : pokemon.heightScalar();
            return properties.scale == currentSize;
        }

        // PKHeX WC9.IsMatchPartial
        EncounterMatchRating MysteryGiftWC9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.hasTeraType() && pokemon.teraTypeOriginal() != properties.teraTypeOriginal)
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }
    }
}
