#include "Legality/Encounters/MysteryGiftWA9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Pokemon/Pokemon.h"
#include "Legality/RNG/ShinyUtil.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t LANGUAGE_JAPANESE = 1;
            constexpr uint8_t LANGUAGE_ENGLISH = 2;
            constexpr uint8_t LANGUAGE_UNUSED = 6;
            constexpr uint8_t LANGUAGE_SPANISH_LATIN_AMERICA = 11;
            /// PKHeX CanBeAnyLanguage reads nine language words, though a card has ten slots.
            constexpr int LANGUAGE_WORDS_CHECKED = 9;
            constexpr uint16_t SCALE_RANDOM = 256;
            /// The stored HP IV that stands for one flawless IV (0xFD two, 0xFE three).
            constexpr uint8_t FLAWLESS_IV_MARKER = 0xFC;

            /// PKHeX WA9.GetLanguageIndex: the card skips language 6, and anything invalid reads English.
            int getLanguageSlot(int language) noexcept
            {
                if (language < LANGUAGE_JAPANESE || language == LANGUAGE_UNUSED || language > LANGUAGE_SPANISH_LATIN_AMERICA)
                {
                    language = LANGUAGE_ENGLISH;
                }
                return language < LANGUAGE_UNUSED ? language - 1 : language - 2;
            }

            /// PKHeX WA9.HomeBaseIV: the HOME Z-A starter gifts (9031-9033) set every other IV to 20.
            int getHomeBaseIV(uint16_t cardId) noexcept
            {
                return cardId >= 9031 && cardId <= 9033 ? 20 : 0;
            }
        }

        RNG::Shiny MysteryGiftWA9::shiny() const noexcept
        {
            switch (properties.shinyType)
            {
            case ShinyType8::FixedValue:
            {
                // PKHeX FixedShinyType: a card without a fixed ID is the player's anti-shiny fixed PID.
                const uint32_t ID32 = ID32Old();
                if (ID32 == 0)
                {
                    return RNG::Shiny::Never;
                }
                const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(properties.PID, ID32);
                if (shinyXor == 0)
                {
                    return RNG::Shiny::AlwaysSquare;
                }
                return shinyXor <= 15 ? RNG::Shiny::Always : RNG::Shiny::Never;
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

        RNG::AbilityPermission MysteryGiftWA9::ability() const noexcept
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

        // PKHeX WA9.GetFlawlessIVCount
        uint8_t MysteryGiftWA9::flawlessIVCount() const noexcept
        {
            const int flawlessMarker = properties.individualValues[0] - FLAWLESS_IV_MARKER;
            if (flawlessMarker >= 0 && flawlessMarker < 3)
            {
                return static_cast<uint8_t>(flawlessMarker + 1);
            }
            return 0;
        }

        std::u16string MysteryGiftWA9::getTrainerName(int language) const
        {
            const char16_t *text = properties.trainerNames[getLanguageSlot(language)];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool MysteryGiftWA9::isNicknamed(int language) const noexcept
        {
            return ((properties.nicknamedSlots >> getLanguageSlot(language)) & 1) != 0;
        }

        bool MysteryGiftWA9::canBeAnyLanguage() const noexcept
        {
            for (int slotIndex = 0; slotIndex < LANGUAGE_WORDS_CHECKED; slotIndex++)
            {
                if (properties.languageWords[slotIndex] != 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool MysteryGiftWA9::canHaveLanguage(int language) const noexcept
        {
            if (language < LANGUAGE_JAPANESE || language > LANGUAGE_SPANISH_LATIN_AMERICA)
            {
                return false;
            }
            if (canBeAnyLanguage())
            {
                return true;
            }
            for (int slotIndex = 0; slotIndex < LANGUAGE_WORDS_CHECKED; slotIndex++)
            {
                if (properties.languageWords[slotIndex] == language)
                {
                    return true;
                }
            }
            // PKHeX GetLanguage reads only the low byte of the redeeming language's slot.
            return (properties.languageWords[getLanguageSlot(language)] & 0xFF) == 0;
        }

        // PKHeX WA9.GetPID for the fixed shiny states (2, 3) and the fixed value (4).
        uint32_t MysteryGiftWA9::getExpectedPID(const Pokemon::Pokemon &pokemon, ShinyType8 type) const noexcept
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

        // PKHeX WA9.IsMatchExact
        bool MysteryGiftWA9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!properties.isEgg)
            {
                if (properties.trainerGender < 2)
                {
                    if (ID32Old() != pokemon.id32())
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

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::ZA, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.hasAlphaFlag() && pokemon.isAlpha() != properties.isAlpha)
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
                if (pokemon.isEgg() && getEntityContextGroup(pokemon) != Enums::GameVersion::ZA)
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
                if (pokemon.metLocation() != properties.location)
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
            if (fixedNature() != NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (properties.gender != 3 && properties.gender != pokemon.gender())
            {
                return false;
            }
            if (RNG::HomeQuirks::hasSizeScalars(pokemon) && properties.scale != SCALE_RANDOM)
            {
                const uint8_t currentSize = pokemon.hasScale() ? pokemon.scale() : pokemon.heightScalar();
                if (properties.scale != currentSize)
                {
                    return false;
                }
            }

            if (isHomeGift())
            {
                const uint8_t flawlessCount = flawlessIVCount();
                const int statValues[6] = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD()};
                if (flawlessCount > 0)
                {
                    // The HOME Z-A starters set every other IV to 20, so no more IVs can be 31.
                    if (getFlawlessIVCount(pokemon) != flawlessCount)
                    {
                        return false;
                    }
                    const int homeBaseIV = getHomeBaseIV(properties.cardId);
                    for (const int statValue : statValues)
                    {
                        if (statValue != 31 && statValue != homeBaseIV)
                        {
                            return false;
                        }
                    }
                }
                else
                {
                    for (int statIndex = 0; statIndex < 6; statIndex++)
                    {
                        if (statValues[statIndex] != properties.individualValues[statIndex])
                        {
                            return false;
                        }
                    }
                }
            }

            // Types 0 and 1 do not use the fixed PID; 2-4 can change on a traded egg to keep the shiny state.
            if (properties.shinyType == ShinyType8::Never || properties.shinyType == ShinyType8::Random)
            {
                return true;
            }
            return pokemon.pid() == getExpectedPID(pokemon, properties.shinyType);
        }

        // PKHeX WA9.IsMatchPartial
        EncounterMatchRating MysteryGiftWA9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (!isHomeGift() && tryGetSeed(pokemon) != SeedCorrelationResult::Success)
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        SeedCorrelationResult MysteryGiftWA9::tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return isSeedFound9a(getParameters(), pokemon) ? SeedCorrelationResult::Success : SeedCorrelationResult::Invalid;
        }

        RNG::GenerateParam9a MysteryGiftWA9::getParameters() const noexcept
        {
            RNG::GenerateParam9a parameters;
            const uint8_t flawlessCount = flawlessIVCount();
            parameters.genderRatio = getGenderRatio9a(properties.gender, properties.speciesId, properties.formId);
            parameters.flawlessIVs = flawlessCount;
            parameters.rollCount = 1;
            parameters.correlation = RNG::LumioseCorrelation::SkipTrainer;
            parameters.sizeType = properties.scale == SCALE_RANDOM ? RNG::SizeType9::RANDOM : RNG::SizeType9::VALUE;
            parameters.scale = static_cast<uint8_t>(properties.scale);
            parameters.nature = fixedNature();
            parameters.ability = ability();
            parameters.shiny = shiny();
            if (flawlessCount == 0)
            {
                const std::array<uint8_t, 6> &stored = properties.individualValues;
                parameters.individualValues = RNG::IndividualValueSet{static_cast<int8_t>(stored[0]), static_cast<int8_t>(stored[1]), static_cast<int8_t>(stored[2]), static_cast<int8_t>(stored[3]), static_cast<int8_t>(stored[4]), static_cast<int8_t>(stored[5]), true};
            }
            return parameters;
        }
    }
}
