#include "Legality/Encounters/EncounterSlot9a.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/RNG/LumioseSolver.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint16_t SPECIES_FURFROU = 676;
            constexpr uint16_t SPECIES_OVERQWIL = 904;
            /// PKHeX EncounterSlot9a.ShinyCharm and ShinyHyperspace: the extra rolls each grants.
            constexpr uint8_t SHINY_CHARM_ROLLS = 3;
            constexpr uint8_t SHINY_HYPERSPACE_ROLLS = 3;
            constexpr uint8_t ALPHA_SCALE = 255;
        }

        const EncounterSlot9a &EncounterArea9a::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot9a::longName() const
        {
            return name() + (parent->type() == SlotType9a::Hyperspace ? " Hyperspace" : " Standard");
        }

        // PKHeX EncounterSlot9a.IsMatchExact
        bool EncounterSlot9a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // Rotom and Furfrou can change form in the game.
            const bool isValidOutOfBoundsForm = slotSpeciesId == SPECIES_ROTOM || slotSpeciesId == SPECIES_FURFROU;
            if (slotFormId != evolution.formId && !isRandomUnspecificForm() && !isValidOutOfBoundsForm)
            {
                return false;
            }
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel < slotLevelMin || metLevel > slotLevelMax)
            {
                return false;
            }
            if (pokemon.hasAlphaFlag() && pokemon.isAlpha() != slotIsAlpha)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterSlot9a.GetMatchRating
        EncounterMatchRating EncounterSlot9a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (slotIsAlpha && isAlphaMoveFlagMissing9a(pokemon, slotSpeciesId, slotFormId))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            // Some slots stay shiny-locked until a side quest is done.
            if (slotShiny == RNG::Shiny::Never && isShiny(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            // PKHeX IsFormArgMismatch: an Overqwil that evolved here must have its evolution counted.
            if (pokemon.speciesID() == SPECIES_OVERQWIL && slotSpeciesId != SPECIES_OVERQWIL && pokemon.hasFormArgument() && pokemon.formArgument() == 0)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            const SeedCorrelationResult correlation = tryGetSeed(pokemon);
            if (correlation == SeedCorrelationResult::Invalid)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (correlation == SeedCorrelationResult::Ignore)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterSlot9a.TryGetSeed
        SeedCorrelationResult EncounterSlot9a::tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept
        {
            RNG::GenerateParam9a parameters = getParameters(false, false);
            if (isSeedFound9a(parameters, pokemon))
            {
                return SeedCorrelationResult::Success;
            }
            if ((isShiny(pokemon) && !RNG::LumioseSolver::SEARCH_SHINY_SINGLE_ROLL) || !RNG::LumioseSolver::SEARCH_SHINY_MANY_ROLLS)
            {
                return SeedCorrelationResult::Ignore;
            }
            const bool isHyperspace = parent->location() == LOCATION_HYPERSPACE9A;
            parameters.rollCount = static_cast<uint8_t>(1 + SHINY_CHARM_ROLLS + (isHyperspace ? SHINY_HYPERSPACE_ROLLS : 0));
            return isSeedFound9a(parameters, pokemon) ? SeedCorrelationResult::Success : SeedCorrelationResult::Invalid;
        }

        RNG::GenerateParam9a EncounterSlot9a::getParameters(bool hasShinyCharm, bool hasShinyPower) const noexcept
        {
            const bool isHyperspace = parent->location() == LOCATION_HYPERSPACE9A;
            RNG::GenerateParam9a parameters;
            parameters.genderRatio = getGenderRatio9a(slotGender, slotSpeciesId, slotFormId);
            parameters.flawlessIVs = flawlessIVCount();
            parameters.rollCount = static_cast<uint8_t>(1 + (hasShinyCharm ? SHINY_CHARM_ROLLS : 0) + (isHyperspace && hasShinyPower ? SHINY_HYPERSPACE_ROLLS : 0));
            parameters.correlation = slotIsAlpha ? RNG::LumioseCorrelation::PreApplyIVs : RNG::LumioseCorrelation::Normal;
            parameters.sizeType = slotIsAlpha ? RNG::SizeType9::VALUE : RNG::SizeType9::RANDOM;
            parameters.scale = slotIsAlpha ? ALPHA_SCALE : 0;
            parameters.ability = RNG::AbilityPermission::Any12;
            parameters.shiny = slotShiny;
            return parameters;
        }
    }
}
