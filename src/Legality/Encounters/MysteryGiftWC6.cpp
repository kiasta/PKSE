#include "Legality/Encounters/MysteryGiftWC6.h"

#include <string_view>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Names/RibbonNames.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX WC6.IsMatchDeferred: the Eevee card whose evolutions PKHeX defers on friendship,
            /// the Diancie card handed out without IV enforcement, and the Magmar card with and
            /// without the Classic Ribbon.
            constexpr uint16_t CARD_EEVEE = 184;
            constexpr uint16_t CARD_DIANCIE = 525;
            constexpr uint16_t CARD_MAGMAR = 504;
            constexpr uint8_t INDIVIDUAL_VALUE_THREE_FLAWLESS = 0xFE;

            bool hasClassicRibbon(const Pokemon::Pokemon &pokemon)
            {
                constexpr std::string_view classicStem = "ribbonclassic";
                const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
                for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(pokemon.getGameGroup()))
                {
                    if (ribbon.spriteStem != nullptr && classicStem == ribbon.spriteStem)
                    {
                        return Names::getRibbonValue(entityData, ribbon) != 0;
                    }
                }
                return false;
            }
        }

        Enums::GameVersion MysteryGiftWC6::version() const noexcept
        {
            switch (properties.restrictVersion)
            {
            case 1:
                return Enums::GameVersion::X;
            case 2:
                return Enums::GameVersion::Y;
            case 3:
                return Enums::GameVersion::XY;
            case 4:
                return Enums::GameVersion::AS;
            case 8:
                return Enums::GameVersion::OR;
            case 12:
                return Enums::GameVersion::ORAS;
            default:
                // PKHeX GameVersion.Gen6, which PKSE has no group for.
                return Enums::GameVersion::Any;
            }
        }

        // PKHeX WC6.Shiny: an egg's shininess is rolled when it hatches.
        RNG::Shiny MysteryGiftWC6::shiny() const noexcept
        {
            if (properties.isEgg)
            {
                return RNG::Shiny::Random;
            }
            switch (properties.pidType)
            {
            case ShinyType6::FixedValue:
            {
                // PKHeX GetShinyXor: a card without a fixed ID is the player's anti-shiny fixed PID.
                if (ID32() == 0)
                {
                    return RNG::Shiny::Never;
                }
                const uint32_t shinyXor = (properties.PID >> 16) ^ (properties.PID & 0xFFFF) ^ properties.TID16 ^ properties.SID16;
                if (shinyXor == 0)
                {
                    return RNG::Shiny::AlwaysSquare;
                }
                return shinyXor <= 15 ? RNG::Shiny::AlwaysStar : RNG::Shiny::Never;
            }
            case ShinyType6::Random:
                return RNG::Shiny::Random;
            case ShinyType6::Always:
                return RNG::Shiny::Always;
            default:
                return RNG::Shiny::Never;
            }
        }

        RNG::AbilityPermission MysteryGiftWC6::ability() const noexcept
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

        // PKHeX WC6.CanBeReceivedByVersion: bit 0 X through bit 3 Omega Ruby.
        bool MysteryGiftWC6::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            if (version < Enums::GameVersion::X || version > Enums::GameVersion::OR)
            {
                return false;
            }
            if (properties.restrictVersion == 0)
            {
                return true;
            }
            const int bitIndex = static_cast<int>(version) - static_cast<int>(Enums::GameVersion::X);
            return (properties.restrictVersion & (1 << bitIndex)) != 0;
        }

        // PKHeX MysteryGift.GetMatchRating with WC6.IsMatchPartial and WC6.IsMatchDeferred
        EncounterMatchRating MysteryGiftWC6::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.restrictLanguage != 0 && properties.restrictLanguage != pokemon.language())
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (!canBeReceivedByVersion(static_cast<Enums::GameVersion>(getOriginVersion(pokemon))))
            {
                if (!properties.isEgg || pokemon.isEgg() || pokemon.eggLocation() != LOCATION_LINK_TRADE6)
                {
                    return EncounterMatchRating::PartialMatch;
                }
            }
            switch (properties.cardId)
            {
            case CARD_EEVEE:
                if (properties.speciesId != pokemon.speciesID() && pokemon.otFriendship() != Pokemon::getPersonalRecord(Enums::GameVersion::ORAS, properties.speciesId, 0).baseFriendship)
                {
                    return EncounterMatchRating::Deferred;
                }
                break;
            case CARD_DIANCIE:
                if (properties.individualValues[0] == INDIVIDUAL_VALUE_THREE_FLAWLESS)
                {
                    return EncounterMatchRating::Deferred;
                }
                break;
            case CARD_MAGMAR:
                if (properties.hasClassicRibbon != hasClassicRibbon(pokemon))
                {
                    return EncounterMatchRating::Deferred;
                }
                break;
            default:
                break;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX WC6.IsMatchExact. A trainer name the game replaced (ReplaceTrainerName7 and
        // ReplaceTrainerNameHOME) is not modelled: only the card's own name matches.
        bool MysteryGiftWC6::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const uint8_t originVersion = getOriginVersion(pokemon);
            if (!properties.isEgg)
            {
                if (properties.trainerGender != 3)
                {
                    // An Omega Ruby/Alpha Sapphire demo card (card 0) has no fixed ID.
                    if (properties.cardId != 0 && (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16()))
                    {
                        return false;
                    }
                    if (properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                const std::u16string trainerName(properties.trainerName);
                if (!trainerName.empty() && trainerName != pokemon.otName())
                {
                    return false;
                }
                if (properties.pidType == ShinyType6::FixedValue && pokemon.pid() != properties.PID)
                {
                    return false;
                }
                if (!isShinyValid(shiny(), pokemon))
                {
                    return false;
                }
                if (properties.originGame != 0 && properties.originGame != originVersion)
                {
                    return false;
                }
                if (properties.encryptionConstant != 0 && properties.encryptionConstant != pokemon.encryptionConstant())
                {
                    return false;
                }
                if (properties.language != 0 && properties.language != pokemon.language())
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::XY, getEntityContextGroup(pokemon)))
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
                if (pokemon.isEgg() && getFormatGeneration(pokemon) != 6)
                {
                    return false;
                }
            }
            else
            {
                if (!isMatchEggLocation(pokemon, properties.eggLocation))
                {
                    return false;
                }
                if (properties.location != pokemon.metLocation())
                {
                    return false;
                }
            }

            if (properties.level != pokemon.metLevel())
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
            return !ContestStatsReadOnly::isContestBelow(pokemon, properties.contestStats);
        }
    }
}
