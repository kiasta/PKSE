#include "Legality/Encounters/EncounterSlot7.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
        }

        const EncounterSlot7 &EncounterArea7::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot7::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot7::longName() const
        {
            return name() + (isSOS() ? " SOS" : " Standard");
        }

        Enums::Ball EncounterSlot7::fixedBall() const noexcept
        {
            return location() == LOCATION_PELAGO7 ? Enums::Ball::Poke : Enums::Ball::None;
        }

        // PKHeX EncounterSlot7.IsMatchExact
        bool EncounterSlot7::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            constexpr uint16_t speciesFurfrou = 676;
            constexpr uint16_t speciesOricorio = 741;
            if (slotFormId != evolution.formId && slotSpeciesId != speciesFurfrou && slotSpeciesId != speciesOricorio)
            {
                // Minior and the other random-form slots.
                if (!isRandomUnspecificForm())
                {
                    return false;
                }
            }
            return true;
        }

        // PKHeX EncounterSlot7.GetMatchRating and IsDeferredHiddenAbility
        EncounterMatchRating EncounterSlot7::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            // A regular slot never has its Hidden Ability; an SOS slot may.
            if (!isSOS() && isHidden)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        uint8_t EncounterSlot7::getFlawlessIVCountMinimum(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (!isSOS())
            {
                return 0;
            }
            // An Ability Patch could have given a regular ally its Hidden Ability after Gen 7.
            if (getEntityContextGroup(pokemon) == Enums::GameVersion::SM && pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER)
            {
                return 2;
            }
            return 0;
        }
    }
}
