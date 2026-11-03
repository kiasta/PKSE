#include "Legality/Encounters/EncounterSlot5.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_COMBEE = 415;
            constexpr uint16_t SPECIES_VESPIQUEN = 416;
            constexpr uint16_t SPECIES_DEERLING = 585;
            constexpr uint16_t SPECIES_SAWSBUCK = 586;
            /// PKHeX PKM.AbilityNumber of the hidden ability.
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;

            /// PKHeX SlotType5.ToString() with '_' replaced by a space.
            const char *getSlotTypeName(SlotType5 type) noexcept
            {
                switch (type)
                {
                case SlotType5::Standard:
                    return "Standard";
                case SlotType5::Grass:
                    return "Grass";
                case SlotType5::Surf:
                    return "Surf";
                case SlotType5::SuperRod:
                    return "Super Rod";
                case SlotType5::Swarm:
                    return "Swarm";
                case SlotType5::HiddenGrotto:
                    return "HiddenGrotto";
                }
                return "";
            }
        }

        const EncounterSlot5 &EncounterArea5::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot5::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot5::longName() const
        {
            return name() + " " + getSlotTypeName(type());
        }

        // PKHeX EncounterSlot5.IsMatchExact
        bool EncounterSlot5::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            // Deerling and Sawsbuck change form with the seasons; no other wild form can change.
            if (evolution.formId != slotFormId && slotSpeciesId != SPECIES_DEERLING && slotSpeciesId != SPECIES_SAWSBUCK)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterSlot5.GetMatchRating
        EncounterMatchRating EncounterSlot5::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            // Black 2/White 2 offer both Combee evolutions at level 24; a Vespiquen from a male
            // Combee (gender value at or above 0x1F) is an invalid evolution.
            if (slotSpeciesId == SPECIES_COMBEE && pokemon.speciesID() == SPECIES_VESPIQUEN && (pokemon.encryptionConstant() & 0xFF) >= 0x1F)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            // PKHeX IsDeferredHiddenAbility: a grotto slot always has its hidden ability, others never.
            if (isHidden != isHiddenGrotto())
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }
    }
}
