#include "Legality/Encounters/EncounterShadow3XD.h"

#include <string>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EncounterShadow3XD.IsMatchLocation: the other met locations PKHeX accepts for any
            /// XD shadow Pokemon (a shadow that fled is met again elsewhere, the Poke Spots among them).
            bool isShadowRecaptureLocation(uint16_t metLocation) noexcept
            {
                return metLocation == 59 || metLocation == 90 || metLocation == 91 || metLocation == 92 || metLocation == 113;
            }
        }

        std::string EncounterShadow3XD::name() const
        {
            return "XD Shadow Encounter " + std::to_string(properties.shadowIndex);
        }

        // PKHeX EncounterShadow3XD.IsMatchExact
        bool EncounterShadow3XD::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (getFormatGeneration(pokemon) == 3 && pokemon.metLocation() != properties.location && !isShadowRecaptureLocation(pokemon.metLocation()))
            {
                return false;
            }
            if (!isMatchLevelGameCube(pokemon, evolution, properties.level))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.formID(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterShadow3XD.GetMatchRating: every XD shadow is a fateful encounter.
        EncounterMatchRating EncounterShadow3XD::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (!pokemon.isFatefulEncounter())
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.fixedBall != Enums::Ball::None && pokemon.ball() != static_cast<uint8_t>(properties.fixedBall))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        RandomCorrelationRating EncounterShadow3XD::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const
        {
            return type == RNG::PIDType::ColosseumXD || type == RNG::PIDType::ColosseumXDAntishiny ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }

        RNG::PIDType EncounterShadow3XD::getSuggestedCorrelation() const noexcept
        {
            return RNG::PIDType::ColosseumXD;
        }
    }
}
