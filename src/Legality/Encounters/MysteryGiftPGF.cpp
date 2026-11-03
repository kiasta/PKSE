#include "Legality/Encounters/MysteryGiftPGF.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t PID_TYPE_NEVER_SHINY = 0;
            constexpr uint8_t PID_TYPE_ALWAYS_SHINY = 2;
            constexpr uint8_t GENDER_RANDOM_PGF = 2;
            constexpr uint8_t NATURE_RANDOM_PGF = 0xFF;
        }

        // PKHeX PGF.Version: the origin game, else the receivable games as a group.
        Enums::GameVersion MysteryGiftPGF::version() const noexcept
        {
            using Enums::GameVersion;
            if (properties.originGame != 0)
            {
                return static_cast<GameVersion>(properties.originGame);
            }
            switch (properties.restrictVersion)
            {
            case 1:
                return GameVersion::W;
            case 2:
                return GameVersion::B;
            case 3:
                return GameVersion::BW;
            case 4:
                return GameVersion::W2;
            case 8:
                return GameVersion::B2;
            case 12:
                return GameVersion::B2W2;
            default:
                // PKHeX GameVersion.Gen5: any Gen 5 game.
                return GameVersion::Any;
            }
        }

        // PKHeX PGF.Shiny
        RNG::Shiny MysteryGiftPGF::shiny() const noexcept
        {
            if (properties.isEgg)
            {
                return RNG::Shiny::Random;
            }
            if (properties.PIDType == PID_TYPE_NEVER_SHINY)
            {
                return RNG::Shiny::Never;
            }
            if (properties.PIDType == PID_TYPE_ALWAYS_SHINY)
            {
                return RNG::Shiny::Always;
            }
            return RNG::Shiny::Random;
        }

        // PKHeX PGF.Ability
        RNG::AbilityPermission MysteryGiftPGF::ability() const noexcept
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

        // PKHeX PGF.CanBeReceivedByVersion
        bool MysteryGiftPGF::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            if (properties.originGame != 0)
            {
                return static_cast<uint8_t>(version) == properties.originGame;
            }
            const int versionValue = static_cast<int>(version);
            if (versionValue < static_cast<int>(Enums::GameVersion::W) || versionValue > static_cast<int>(Enums::GameVersion::B2))
            {
                return false;
            }
            if (properties.restrictVersion == 0)
            {
                return true;
            }
            const int bitIndex = versionValue - static_cast<int>(Enums::GameVersion::W);
            return (properties.restrictVersion & (1 << bitIndex)) != 0;
        }

        // PKHeX PGF.IsMatchExact
        bool MysteryGiftPGF::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!properties.isEgg)
            {
                if (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16())
                {
                    return false;
                }
                if (pokemon.otName() != std::u16string(properties.trainerName))
                {
                    return false;
                }
                if (properties.trainerGender < 3 && properties.trainerGender != pokemon.otGender())
                {
                    return false;
                }
                if (properties.PID != 0 && pokemon.pid() != properties.PID)
                {
                    return false;
                }
                if (properties.PIDType == PID_TYPE_NEVER_SHINY && isShiny(pokemon))
                {
                    return false;
                }
                if (properties.PIDType == PID_TYPE_ALWAYS_SHINY && !isShiny(pokemon))
                {
                    return false;
                }
                if (properties.originGame != 0 && properties.originGame != getOriginVersion(pokemon))
                {
                    return false;
                }
                if (properties.language != 0 && properties.language != pokemon.language())
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
            else
            {
                if (properties.eggLocation != pokemon.eggLocation())
                {
                    // Traded.
                    if (pokemon.isEgg() || !isLinkTradeLocation5(pokemon.eggLocation()))
                    {
                        return false;
                    }
                }
                else if (properties.PIDType == PID_TYPE_NEVER_SHINY && isShiny(pokemon))
                {
                    // An egg cannot be traded away to lose its shininess.
                    return false;
                }
                if (pokemon.isEgg() && getFormatGeneration(pokemon) != 5)
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.level != pokemon.metLevel())
            {
                return false;
            }
            if (properties.ball != pokemon.ball())
            {
                return false;
            }
            if (properties.nature != NATURE_RANDOM_PGF && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM_PGF && properties.gender != pokemon.gender())
            {
                return false;
            }
            return !ContestStatsReadOnly::isContestBelow(pokemon, properties.contestStats);
        }

        // PKHeX PGF.IsMatchPartial
        bool MysteryGiftPGF::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.restrictLanguage != 0 && properties.restrictLanguage != pokemon.language())
            {
                return true;
            }
            return !canBeReceivedByVersion(static_cast<Enums::GameVersion>(getOriginVersion(pokemon)));
        }

        // PKHeX MysteryGift.GetMatchRating (PGF is never Deferred)
        EncounterMatchRating MysteryGiftPGF::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }
    }
}
