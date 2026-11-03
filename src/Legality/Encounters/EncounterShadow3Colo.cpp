#include "Legality/Encounters/EncounterShadow3Colo.h"

#include <string>

#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// The Card e Room, where the Japanese game battles the e-Reader trainers.
            constexpr uint8_t LOCATION_CARD_E_ROOM = 128;
        }

        std::string EncounterShadow3Colo::name() const
        {
            return "COLO Shadow Encounter " + std::to_string(properties.shadowIndex);
        }

        bool EncounterShadow3Colo::isEReader() const noexcept
        {
            return properties.location == LOCATION_CARD_E_ROOM;
        }

        // PKHeX EncounterShadow3Colo.IsMatchExact
        bool EncounterShadow3Colo::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            return isMatchExactGameCube(pokemon, evolution, properties.speciesId, properties.level, properties.location);
        }

        // PKHeX EncounterShadow3Colo.GetMatchRating: a Colosseum shadow is never a fateful encounter.
        EncounterMatchRating EncounterShadow3Colo::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.isFatefulEncounter())
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterShadow3Colo.IsCompatible: the e-Reader shadows have fixed IVs and no correlation.
        RandomCorrelationRating EncounterShadow3Colo::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const
        {
            if (isEReader())
            {
                return RandomCorrelationRating::Match;
            }
            return type == RNG::PIDType::ColosseumXD ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }

        RNG::PIDType EncounterShadow3Colo::getSuggestedCorrelation() const noexcept
        {
            return isEReader() ? RNG::PIDType::None : RNG::PIDType::ColosseumXD;
        }
    }
}
