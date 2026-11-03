/**
 * Traces a Pokemon's PID and IVs back to the RNG correlation and seed that produced them.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/MethodFinder.cs. Gen 1/2 return None, as in PKHeX:
 * their DV correlation has never been reduced to code.
 */
#ifndef LEGALITY_RNG_METHODFINDER_H
#define LEGALITY_RNG_METHODFINDER_H

#include <cstdint>

#include "Legality/RNG/PIDIV.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct MethodFinder
        {
            /// PKHeX MethodFinder.Analyze: the first correlation that explains the encryption
            /// constant (the PID through Gen 5) and the IVs, in PKHeX's order of preference.
            static PIDIV analyze(const Pokemon::Pokemon &pokemon) noexcept;

            /// A Method 1 seed for the Pokemon's PID and IVs.
            static bool getLCRNGMethod1Match(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept;

            /// `packedIVs` is PKHeX's IV32 (HP, Attack, Defense, Speed, Special Attack, Special
            /// Defense, 5 bits each from the low end).
            static bool getLCRNGMethod1Match(uint32_t PID, uint32_t packedIVs, uint32_t &result) noexcept;

            static bool getLCRNGMethod1Match(uint32_t PID, uint32_t firstIVs, uint32_t secondIVs, uint32_t &result) noexcept;

            /// The Poke Radar chain shiny bits of `PID`, read back from `seed`; `origin` gets the
            /// seed that starts the PID.
            static bool isChainShinyValid(uint16_t TID16, uint16_t SID16, uint32_t PID, uint32_t seed, uint32_t &origin) noexcept;

            /// PKHeX PKM.GetIVs for any generation's Pokemon.
            static uint32_t getPackedIVs(const Pokemon::Pokemon &pokemon) noexcept;
        };
    }
}

#endif
