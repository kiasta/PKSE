#include "Legality/Encounters/MysteryGiftWB8.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterRules8b.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_MANAPHY = 490;
            // PKHeX WB8.PIDType values (ShinyType8).
            constexpr uint8_t SHINY_TYPE_RANDOM = 1;
            constexpr uint8_t SHINY_TYPE_ALWAYS_STAR = 2;
            constexpr uint8_t SHINY_TYPE_ALWAYS_SQUARE = 3;
            constexpr uint8_t SHINY_TYPE_FIXED_VALUE = 4;

            struct HomeScalars
            {
                uint8_t heightScalar;
                uint8_t weightScalar;
            };

            // PKHeX WB8.IsScalarFixed / GetHomeScalars: the HOME Sinnoh starters and Manaphy have fixed sizes.
            bool tryGetHomeScalars(uint16_t cardId, HomeScalars &scalars) noexcept
            {
                switch (cardId)
                {
                case 9015: // Turtwig
                    scalars = {147, 160};
                    return true;
                case 9016: // Chimchar
                    scalars = {146, 63};
                    return true;
                case 9017: // Piplup
                    scalars = {61, 203};
                    return true;
                case 9026: // Manaphy
                    scalars = {128, 128};
                    return true;
                default:
                    return false;
                }
            }
        }

        RNG::Shiny MysteryGiftWB8::shiny() const noexcept
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

        RNG::AbilityPermission MysteryGiftWB8::ability() const noexcept
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

        bool MysteryGiftWB8::canRecordBeReceivedByVersion(Enums::GameVersion version, const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (version == Enums::GameVersion::BD || version == Enums::GameVersion::SP)
            {
                return true;
            }
            return isFormatSwordShield(pokemon) && isValidMetBrilliantDiamondShiningPearl(pokemon.metLocation(), getOriginVersion(pokemon));
        }

        std::u16string MysteryGiftWB8::getTrainerName(int language) const
        {
            const char16_t *text = properties.trainerNames[getWonderCardLanguageSlot(language)];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool MysteryGiftWB8::isNicknamed(int language) const noexcept
        {
            return ((properties.nicknamedSlots >> getWonderCardLanguageSlot(language)) & 1) != 0;
        }

        // PKHeX WB8.GetPID for the fixed shiny states (2, 3) and the fixed value (4).
        uint32_t MysteryGiftWB8::getExpectedPID(const Pokemon::Pokemon &pokemon, uint8_t type) const noexcept
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
                // PKHeX GetAntishinyFixedHOME.
                return pokemon.id32() + 0x10u;
            }
            return PID;
        }

        // PKHeX WB8.IsMatchLocation
        bool MysteryGiftWB8::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            return isMatchLocation8b(pokemon, pokemon.metLocation() == properties.location, properties.location);
        }

        // PKHeX WB8.IsMatchExact
        bool MysteryGiftWB8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
                    // A BDSP-only card's Pokemon moved to Sword/Shield carries HOME's remapped location.
                    if (properties.originGame == static_cast<int32_t>(Enums::GameVersion::BD) && !(originVersion == static_cast<uint8_t>(Enums::GameVersion::SW) && pokemon.metLocation() == LOCATION_HOME_SWBD))
                    {
                        return false;
                    }
                    if (properties.originGame == static_cast<int32_t>(Enums::GameVersion::SP) && !(originVersion == static_cast<uint8_t>(Enums::GameVersion::SH) && pokemon.metLocation() == LOCATION_HOME_SHSP))
                    {
                        return false;
                    }
                }
                if (properties.encryptionConstant != 0 && properties.encryptionConstant != pokemon.encryptionConstant())
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::BDSP, getEntityContextGroup(pokemon)))
            {
                return false;
            }

            const RNG::Shiny shinyType = shiny();
            if (properties.isEgg)
            {
                const uint16_t expectedEggLocation = properties.speciesId == SPECIES_MANAPHY ? properties.location : properties.eggLocation;
                if (expectedEggLocation != pokemon.eggLocation())
                {
                    if (pokemon.eggLocation() != LOCATION_LINK_TRADE6_NPC)
                    {
                        return false;
                    }
                    // A shiny traded egg always has shiny xor 0 or 1.
                    if (properties.shinyType == SHINY_TYPE_RANDOM && isShiny(pokemon) && getShinyXorValue(pokemon) > 1)
                    {
                        return false;
                    }
                }
                if (!isShinyValid(shinyType, pokemon))
                {
                    return false;
                }
                if (pokemon.isEgg() && getEntityContextGroup(pokemon) != Enums::GameVersion::BDSP)
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
                // PKHeX WB8.IsMatchEggLocationInternal: a PB8 stores "none" as 65535, every other format 0.
                const uint16_t expectedEggLocation = pokemon.getGameGroup() == Enums::GameVersion::BDSP ? LOCATION_DEFAULT8B_NONE : 0;
                if (pokemon.eggLocation() != expectedEggLocation)
                {
                    return false;
                }
                if (!isMatchLocation(pokemon))
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
