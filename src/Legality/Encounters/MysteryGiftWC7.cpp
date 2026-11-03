#include "Legality/Encounters/MysteryGiftWC7.h"

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
            /// PKHeX WC7: card 2046 (Ash-Greninja) went to Sun/Moon only and arrives at level 36 or 37.
            constexpr uint16_t CARD_ASH_GRENINJA = 2046;
            /// PKHeX WC7: cards 1122 and 1133 exist once per version pair.
            constexpr uint16_t CARD_VERSION_PAIR_A = 1122;
            constexpr uint16_t CARD_VERSION_PAIR_B = 1133;
            constexpr uint16_t SPECIES_GRENINJA = 658;
        }

        Enums::GameVersion MysteryGiftWC7::version() const noexcept
        {
            if (properties.cardId == CARD_ASH_GRENINJA)
            {
                return Enums::GameVersion::SM;
            }
            switch (properties.restrictVersion)
            {
            case 1:
                return Enums::GameVersion::SN;
            case 2:
                return Enums::GameVersion::MN;
            case 3:
                return Enums::GameVersion::SM;
            case 4:
                return Enums::GameVersion::US;
            case 8:
                return Enums::GameVersion::UM;
            case 12:
                return Enums::GameVersion::USUM;
            default:
                // PKHeX GameVersion.Gen7, which PKSE has no group for.
                return Enums::GameVersion::Any;
            }
        }

        // PKHeX WC7.Shiny: an egg's shininess is rolled when it hatches.
        RNG::Shiny MysteryGiftWC7::shiny() const noexcept
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

        RNG::AbilityPermission MysteryGiftWC7::ability() const noexcept
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

        // PKHeX WC7.CanBeReceivedByVersion: bit 0 Sun through bit 3 Ultra Moon.
        bool MysteryGiftWC7::canBeReceivedByVersion(Enums::GameVersion version) const noexcept
        {
            if (version < Enums::GameVersion::SN || version > Enums::GameVersion::UM)
            {
                return false;
            }
            if (properties.cardId == CARD_ASH_GRENINJA)
            {
                return version == Enums::GameVersion::SN || version == Enums::GameVersion::MN;
            }
            if (properties.restrictVersion == 0)
            {
                return true;
            }
            const int bitIndex = static_cast<int>(version) - static_cast<int>(Enums::GameVersion::SN);
            return (properties.restrictVersion & (1 << bitIndex)) != 0;
        }

        // PKHeX WC7.IsMatchPartial
        EncounterMatchRating MysteryGiftWC7::getMatchRating(const Pokemon::Pokemon &pokemon) const
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
            return EncounterMatchRating::Match;
        }

        // PKHeX WC7.IsMatchExact. A trainer name the game replaced (ReplaceTrainerName7 and
        // ReplaceTrainerNameHOME) is not modelled: only the card's own name matches.
        bool MysteryGiftWC7::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const uint8_t originVersion = getOriginVersion(pokemon);
            if (!properties.isEgg)
            {
                if (properties.trainerGender != 3)
                {
                    if (properties.SID16 != pokemon.sid16() || properties.TID16 != pokemon.tid16() || properties.trainerGender != pokemon.otGender())
                    {
                        return false;
                    }
                }
                const std::u16string trainerName(properties.trainerName);
                if (!trainerName.empty() && trainerName != pokemon.otName())
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

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SM, getEntityContextGroup(pokemon)))
            {
                // Ash-Greninja and Greninja share one Mega form in Legends: Z-A.
                constexpr uint8_t formAshGreninja = 1;
                constexpr uint8_t formMegaGreninja = 3;
                const bool isAshGreninjaMega = properties.speciesId == SPECIES_GRENINJA && properties.formId == formAshGreninja && getEntityContextGroup(pokemon) == Enums::GameVersion::ZA && pokemon.form() == formMegaGreninja;
                if (!isAshGreninjaMega)
                {
                    return false;
                }
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
                if (pokemon.isEgg() && getFormatGeneration(pokemon) != 7)
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
            if (ContestStatsReadOnly::isContestBelow(pokemon, properties.contestStats))
            {
                return false;
            }
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(originVersion);
            if ((properties.cardId == CARD_VERSION_PAIR_A || properties.cardId == CARD_VERSION_PAIR_B) && !canBeReceivedByVersion(version))
            {
                return false;
            }
            if (properties.cardId == CARD_ASH_GRENINJA)
            {
                // The Greninja card has a varying PID; only its Sun/Moon origin is fixed.
                return version == Enums::GameVersion::SN || version == Enums::GameVersion::MN;
            }
            return properties.pidType != ShinyType6::FixedValue || pokemon.pid() == properties.PID;
        }
    }
}
