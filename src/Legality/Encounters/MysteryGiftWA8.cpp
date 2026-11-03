#include "Legality/Encounters/MysteryGiftWA8.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon8SWSH.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX WA8.PIDType values (ShinyType8).
            constexpr uint8_t SHINY_TYPE_RANDOM = 1;
            constexpr uint8_t SHINY_TYPE_ALWAYS_STAR = 2;
            constexpr uint8_t SHINY_TYPE_ALWAYS_SQUARE = 3;
            constexpr uint8_t SHINY_TYPE_FIXED_VALUE = 4;

            struct HomeScalars
            {
                uint8_t heightScalar;
                uint8_t weightScalar;
            };

            // PKHeX WA8.IsScalarFixed / GetHomeScalars: the HOME Hisui starters and Enamorus have fixed sizes.
            bool tryGetHomeScalars(uint16_t cardId, HomeScalars &scalars) noexcept
            {
                switch (cardId)
                {
                case 9018: // Rowlet
                    scalars = {172, 171};
                    return true;
                case 9019: // Cyndaquil
                    scalars = {101, 145};
                    return true;
                case 9020: // Oshawott
                    scalars = {239, 79};
                    return true;
                case 9027: // Enamorus
                    scalars = {127, 127};
                    return true;
                default:
                    return false;
                }
            }

            // PKHeX IGanbaru.IsGanbaruValuesBelow, against the card's minimum grit values.
            bool isGritBelow(const Pokemon::Pokemon8LA &pokemon, const std::array<uint8_t, 6> &minimum) noexcept
            {
                return pokemon.gritHP() < minimum[0] || pokemon.gritATK() < minimum[1] || pokemon.gritDEF() < minimum[2] || pokemon.gritSPA() < minimum[4] || pokemon.gritSPD() < minimum[5] || pokemon.gritSPE() < minimum[3];
            }
        }

        RNG::Shiny MysteryGiftWA8::shiny() const noexcept
        {
            switch (properties.shinyType)
            {
            case SHINY_TYPE_FIXED_VALUE:
            {
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
            case SHINY_TYPE_RANDOM:
                return RNG::Shiny::Random;
            case SHINY_TYPE_ALWAYS_STAR:
                return RNG::Shiny::AlwaysStar;
            case SHINY_TYPE_ALWAYS_SQUARE:
                return RNG::Shiny::AlwaysSquare;
            default:
                return RNG::Shiny::Never;
            }
        }

        RNG::AbilityPermission MysteryGiftWA8::ability() const noexcept
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

        bool MysteryGiftWA8::canRecordBeReceivedByVersion(Enums::GameVersion version, const Pokemon::Pokemon &pokemon) const noexcept
        {
            return version == Enums::GameVersion::PLA || (isFormatSwordShield(pokemon) && version == Enums::GameVersion::SW);
        }

        std::u16string MysteryGiftWA8::getTrainerName(int language) const
        {
            const char16_t *text = properties.trainerNames[getWonderCardLanguageSlot(language)];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool MysteryGiftWA8::isNicknamed(int language) const noexcept
        {
            return ((properties.nicknamedSlots >> getWonderCardLanguageSlot(language)) & 1) != 0;
        }

        // PKHeX WA8.GetPID for the fixed shiny states (2, 3) and the fixed value (4).
        uint32_t MysteryGiftWA8::getExpectedPID(const Pokemon::Pokemon &pokemon, uint8_t type) const noexcept
        {
            const uint32_t lowPID = properties.PID & 0xFFFF;
            const uint32_t trainerXor = pokemon.tid16() ^ pokemon.sid16();
            if (type == SHINY_TYPE_ALWAYS_STAR)
            {
                return ((1u ^ lowPID ^ trainerXor) << 16) | lowPID;
            }
            if (type == SHINY_TYPE_ALWAYS_SQUARE)
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
            if (isHomeGift())
            {
                // PKHeX WA8.GetAntishinyFixedHOME (WB8's adds instead).
                return pokemon.id32() ^ 0x10u;
            }
            return PID;
        }

        // PKHeX WA8.IsMatchExact's ball rule: the card's ball, or the proto-Poke Ball for anything older
        // (Cherish included) unless it is a HOME gift; a PK8 holds a plain Poke Ball for the Hisui balls.
        bool MysteryGiftWA8::isMatchBall(const Pokemon::Pokemon &pokemon) const noexcept
        {
            constexpr uint8_t ballLegendsPoke = static_cast<uint8_t>(Enums::Ball::LAPoke);
            uint8_t expectedBall = properties.ball == 0 ? ballLegendsPoke : properties.ball;
            if (expectedBall < ballLegendsPoke && !isHomeGift())
            {
                expectedBall = ballLegendsPoke;
            }
            if (isFormatSwordShield(pokemon) && expectedBall >= static_cast<uint8_t>(Enums::Ball::Strange))
            {
                expectedBall = static_cast<uint8_t>(Enums::Ball::Poke);
            }
            return expectedBall == pokemon.ball();
        }

        // PKHeX WA8.IsMatchExact
        bool MysteryGiftWA8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!properties.isEgg)
            {
                if (properties.trainerGender < 2)
                {
                    if (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16() || properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                if (!canWonderCardBeAnyLanguage(properties.languageWords) && !canWonderCardHaveLanguage(properties.languageWords, pokemon.language()))
                {
                    return false;
                }
                const std::u16string trainerName = getTrainerName(pokemon.language());
                if (!trainerName.empty() && trainerName != pokemon.otName())
                {
                    return false;
                }
                const uint8_t originVersion = getOriginVersion(pokemon);
                if (properties.originGame != 0 && properties.originGame != originVersion)
                {
                    // A Legends: Arceus card's Pokemon moved to Sword/Shield carries HOME's remapped location.
                    if (properties.originGame == static_cast<int32_t>(Enums::GameVersion::PLA) && !(originVersion == static_cast<uint8_t>(Enums::GameVersion::SW) && pokemon.metLocation() == LOCATION_HOME_SWLA))
                    {
                        return false;
                    }
                }
                if (properties.encryptionConstant != 0 && properties.encryptionConstant != pokemon.encryptionConstant())
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::PLA, getEntityContextGroup(pokemon)))
            {
                return false;
            }

            // Never an egg.
            if (!isShinyValid(shiny(), pokemon))
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, properties.eggLocation))
            {
                return false;
            }
            if (isFormatSwordShield(pokemon) ? pokemon.metLocation() != LOCATION_HOME_SWLA : pokemon.metLocation() != properties.location)
            {
                return false;
            }

            if (properties.metLevel != 0 && properties.metLevel != pokemon.metLevel())
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
            if (!isMatchBall(pokemon))
            {
                return false;
            }
            if (isFormatSwordShield(pokemon) && static_cast<const Pokemon::Pokemon8SWSH &>(pokemon).dynamaxLevel() < properties.dynamaxLevel)
            {
                return false;
            }
            if (pokemon.getGameGroup() == Enums::GameVersion::PLA && isGritBelow(static_cast<const Pokemon::Pokemon8LA &>(pokemon), properties.gritValues))
            {
                return false;
            }

            HomeScalars scalars{};
            if (tryGetHomeScalars(properties.cardId, scalars))
            {
                if (RNG::HomeQuirks::hasSizeScalars(pokemon) && (pokemon.heightScalar() != scalars.heightScalar || pokemon.weightScalar() != scalars.weightScalar))
                {
                    return false;
                }
                if (pokemon.hasScale() && pokemon.scale() != scalars.heightScalar)
                {
                    return false;
                }
            }

            // Types 0 and 1 do not use the fixed PID; 2-4 can change on a traded egg to keep the shiny state.
            if (properties.shinyType <= SHINY_TYPE_RANDOM)
            {
                return true;
            }
            return pokemon.pid() == getExpectedPID(pokemon, properties.shinyType);
        }
    }
}
