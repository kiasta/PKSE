#include "Legality/Encounters/EncounterSlot6XY.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
            constexpr uint16_t SPECIES_BURMY = 412;
            constexpr uint16_t SPECIES_SAWSBUCK = 586;
            constexpr uint16_t SPECIES_FURFROU = 676;
        }

        const char *getSlotType6Name(SlotType6 type) noexcept
        {
            switch (type)
            {
            case SlotType6::Standard:
                return "Standard";
            case SlotType6::Grass:
                return "Grass";
            case SlotType6::Surf:
                return "Surf";
            case SlotType6::Old_Rod:
                return "Old Rod";
            case SlotType6::Good_Rod:
                return "Good Rod";
            case SlotType6::Super_Rod:
                return "Super Rod";
            case SlotType6::Rock_Smash:
                return "Rock Smash";
            case SlotType6::Horde:
                return "Horde";
            case SlotType6::FriendSafari:
                return "FriendSafari";
            }
            return "";
        }

        const EncounterSlot6XY &EncounterArea6XY::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot6XY::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot6XY::longName() const
        {
            return name() + " " + getSlotType6Name(parent->type());
        }

        // PKHeX EncounterSlot6XY.IsMatchExact
        bool EncounterSlot6XY::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (slotFormId != evolution.formId && !isRandomUnspecificForm() && !isValidOutOfBoundsForm(pokemon))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterSlot6XY.IsValidOutOfBoundsForm
        bool EncounterSlot6XY::isValidOutOfBoundsForm(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (slotSpeciesId == SPECIES_BURMY || slotSpeciesId == SPECIES_FURFROU)
            {
                // Both can change form in the game.
                return true;
            }
            if (slotSpeciesId == SPECIES_SAWSBUCK)
            {
                // A Friend Safari Sawsbuck can change season once it reaches Gen 8 or later.
                return getFormatGeneration(pokemon) >= 8;
            }
            return false;
        }

        // PKHeX EncounterSlot6XY.GetMatchRating and IsDeferredHiddenAbility
        EncounterMatchRating EncounterSlot6XY::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
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
    }
}
