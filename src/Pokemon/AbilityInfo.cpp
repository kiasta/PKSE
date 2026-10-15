#include "Pokemon/AbilityInfo.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Pokemon
{
    AbilitySlots getAbilitySlots(uint16_t species, uint8_t form, Enums::GameVersion group)
    {
        // Gens 1 and 2 have no abilities at all. Returning slots for them would let a caller
        // "validate" a field the format does not have; count 0 makes every id illegal, which is
        // why hasAbility() must gate the call rather than this standing in for it.
        if (!personalHasAbilities(group))
        {
            return AbilitySlots{{0, 0, 0}, 0};
        }

        // EVERY GROUP READS ITS OWN TABLE. Ability slots are per-game data rather than a constant of
        // the species: Scarlet/Violet moved the Piplup line's hidden ability from Defiant to Competitive,
        // Shiftry's second from Early Bird to Wind Rider and Gallade's from none to Sharpness, and
        // Legends: Arceus's Hisuian Growlithe has Rock Head where S/V's has Justified. Resolving any of
        // those against another game's table calls a legitimate Pokemon illegal -- which is how it was
        // found, on a real Ultra Sun save.
        const PersonalRecord &record = getPersonalRecord(group, species, form);
        const uint8_t slotCount = personalHasHiddenAbility(group) ? 3 : 2;
        return AbilitySlots{{record.ability1, record.ability2, record.abilityHidden}, slotCount};
    }

    bool isAbilityLegal(const Pokemon &pokemon)
    {
        const uint16_t species = pokemon.speciesID();
        const uint8_t form = pokemon.form();
        const Enums::GameVersion group = pokemon.getGameGroup();
        const uint16_t abilityId = pokemon.ability();
        if (isAbilityLegal(getAbilitySlots(species, form, group), abilityId))
            return true;
        if (!pokemon.hasBirthAbility())
            return false;
        // The slot the Pokemon holds, as an index into AbilitySlots::slot -- 1, 2 and 4 name
        // slot 1, slot 2 and the hidden slot.
        const uint8_t abilityNumber = pokemon.abilityNumber();
        const int slotIndex = abilityNumber == 1 ? 0 : abilityNumber == 2 ? 1 : abilityNumber == 4 ? 2 : -1;
        if (slotIndex < 0)
            return false;
        uint16_t ancestorSpecies[EVO_MAX_CHAIN];
        uint8_t ancestorForms[EVO_MAX_CHAIN];
        const int ancestorCount = getPreEvolutionChain(group, species, form, ancestorSpecies, ancestorForms, EVO_MAX_CHAIN);
        for (int ancestorIndex = 0; ancestorIndex < ancestorCount; ++ancestorIndex)
        {
            const AbilitySlots ancestorSlots = getAbilitySlots(ancestorSpecies[ancestorIndex], ancestorForms[ancestorIndex], group);
            if (slotIndex < ancestorSlots.count && ancestorSlots.slot[slotIndex] == abilityId)
                return true;
        }
        return false;
    }

    uint8_t getAbilityNumberForId(const AbilitySlots &slots, uint16_t abilityId) noexcept
    {
        if (abilityId == 0)
            return 0;
        // Lowest matching slot wins, so a species whose slot 2 duplicates slot 1 reads
        // as slot 1 -- the same collapse PKHeX's GetIndexOfAbility does.
        for (int slotIndex = 0; slotIndex < slots.count; ++slotIndex)
            if (slots.slot[slotIndex] == abilityId)
                return static_cast<uint8_t>(1 << slotIndex); // 1 / 2 / 4
        return 0;
    }
}
