#include "Legality/Encounters/EncounterStatic1.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PIKACHU = 25;
            /// The Light Ball item id that Yellow's starter Pikachu carries in its catch-rate byte.
            constexpr uint8_t CATCH_RATE_LIGHT_BALL_PIKACHU = 0xA3;
        }

        bool EncounterStatic1::isStarterPikachu() const noexcept
        {
            return staticVersion == Enums::GameVersion::YW && staticSpeciesId == SPECIES_PIKACHU && staticLevel == 5;
        }

        // PKHeX EncounterStatic1.IsMatchExact
        bool EncounterStatic1::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // A PK1 stores no met location; Gen 2 met data rules out a Gen 1 encounter.
            if (GameBoyEntity::getCaughtData(pokemon) != 0)
            {
                return false;
            }
            if (staticLevel > evolution.levelMax)
            {
                return false;
            }
            // These encounters only exist in the Japanese Blue.
            if (staticVersion == Enums::GameVersion::BU && !GameBoyEntity::isJapanese(pokemon))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(staticSpeciesId, 0, pokemon.formID(), Enums::GameVersion::RBY, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic1.GetMatchRating
        EncounterMatchRating EncounterStatic1::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.getGameGroup() == Enums::GameVersion::RBY && !isCatchRateValid(GameBoyEntity::getCatchRate(pokemon)))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        bool EncounterStatic1::isCatchRateValid(uint8_t rate) const noexcept
        {
            if (ALLOW_GEN1_TRADEBACK && GBRestrictions::isCatchRateHeldItem(rate))
            {
                return true;
            }
            if (isStarterPikachu())
            {
                return rate == CATCH_RATE_LIGHT_BALL_PIKACHU;
            }
            // Red/Blue and Yellow can disagree on a species' catch rate.
            return GBRestrictions::rateMatchesEncounter(staticSpeciesId, staticVersion, rate);
        }
    }
}
