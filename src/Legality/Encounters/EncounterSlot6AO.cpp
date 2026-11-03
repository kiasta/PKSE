#include "Legality/Encounters/EncounterSlot6AO.h"

#include <algorithm>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
            constexpr uint16_t SPECIES_WURMPLE = 265;
            constexpr uint16_t SPECIES_DEERLING = 585;
        }

        const EncounterSlot6AO &EncounterArea6AO::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot6AO::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot6AO::longName() const
        {
            return name() + " " + getSlotType6Name(parent->type());
        }

        // PKHeX EncounterSlot6AO.GetMoveBonusPossible
        std::span<const uint16_t> EncounterSlot6AO::getMoveBonusPossible() const noexcept
        {
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::XY);
            if (tree == nullptr)
            {
                return {};
            }
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(slotSpeciesId, slotFormId);
            return LearnSource::getEggMoves(LearnSource::LearnEnvironment::ORAS, baby.speciesId, baby.formId);
        }

        bool EncounterSlot6AO::isMoveBonus(uint16_t moveId) const noexcept
        {
            const std::span<const uint16_t> moves = getMoveBonusPossible();
            return std::find(moves.begin(), moves.end(), moveId) != moves.end();
        }

        // PKHeX EncounterSlot6AO.IsMatchExact
        bool EncounterSlot6AO::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const uint8_t boostMax = parent->type() != SlotType6::Rock_Smash ? DEXNAV_BOOST : FLUTE_BOOST_MAX;
            if (!isLevelWithinRange(pokemon.metLevel(), FLUTE_BOOST_MIN, boostMax))
            {
                return false;
            }
            if (evolution.formId != slotFormId && !isRandomUnspecificForm())
            {
                if (slotSpeciesId != SPECIES_DEERLING)
                {
                    return false;
                }
                // Deerling can change season once it reaches Gen 8 or later.
                if (getFormatGeneration(pokemon) < 8 || evolution.formId >= 4)
                {
                    return false;
                }
            }
            return true;
        }

        // PKHeX EncounterSlot6AO.GetMatchRating and IsDeferredHiddenAbility
        EncounterMatchRating EncounterSlot6AO::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isDeferredWurmple(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (!isHiddenAbilityPossible() && isHidden)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        bool EncounterSlot6AO::isDeferredWurmple(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return slotSpeciesId == SPECIES_WURMPLE && pokemon.speciesID() != SPECIES_WURMPLE && !WurmpleUtil::isWurmpleEvolutionValid(pokemon);
        }
    }
}
