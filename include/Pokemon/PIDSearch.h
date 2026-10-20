/**
 * Builds a PID that keeps everything the PID decides about a Pokemon.
 *
 * In Gens 3-5 the PID is more than an encryption key: it decides shininess (against the trainer's
 * ids), the gender (its low byte against the species' ratio, which PKHeX requires the stored gender
 * to agree with), the nature in Gens 3 and 4 (PID % 25) and the ability slot (bit 0 in Gens 3 and
 * 4, bit 16 in Gen 5). From Gen 6 on only shininess is left. A PID chosen at random keeps none of
 * them, so every setter that has to change the PID asks for the ones it must keep.
 *
 * The PID is CONSTRUCTED, not drawn at random until one fits. A random draw has to land in the
 * shiny window (8 or 16 in 65,536) and match the nature (1 in 25) and the rest at once, and a
 * million draws missed all of them often enough that "make shiny" silently did nothing about one
 * time in eleven. Here the low half is walked from a random start, and the high half is derived
 * inside the shiny window when shininess is wanted, so a PID is found whenever one exists.
 */

#ifndef POKEMON_PID_SEARCH_H
#define POKEMON_PID_SEARCH_H

#include <cstdint>

namespace Pokemon
{
    /// What a new PID must keep. Every "want" is -1 for "either", or the value to hold.
    struct PIDRequirements
    {
        uint32_t trainerID32 = 0;
        /// Shiny when (TID ^ SID ^ PID high ^ PID low) is below this: 8 in Gens 3-5, 16 from Gen 6.
        uint32_t shinyThreshold = 8;
        int wantShiny = -1; // 1 shiny, 0 not shiny
        /// The species' gender ratio (PersonalRecord::genderRatio). Ignored for a species whose
        /// gender is fixed (0 male only, 254 female only, 255 genderless): its PID decides nothing.
        uint8_t genderRatio = 255;
        int wantGender = -1; // 0 male, 1 female
        int wantNature = -1; // PID % 25 (Gens 3 and 4)
        /// Which PID bit is the ability slot: 0 in Gens 3 and 4, 16 in Gen 5, -1 for none.
        int abilityBitIndex = -1;
        int wantAbilityBit = -1;
    };

    /// A PID meeting every requirement, chosen at random among them. False, with `pidOut`
    /// untouched, only when no PID can meet them all.
    bool findPID(const PIDRequirements &requirements, uint32_t &pidOut) noexcept;

    /// The gender a Gen 3-5 PID gives a species: 0 male, 1 female, 2 genderless.
    uint8_t genderFromPID(uint32_t pidValue, uint8_t genderRatio) noexcept;
}

#endif
