/**
 * XD's Poke Spots: the activation and slot rolls before a Poke Spot PID, and the snack animation
 * rolls before its IVs.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/CXD/MethodPokeSpot.cs (the verification half; the
 * generators stay with the encounter templates).
 */
#ifndef LEGALITY_RNG_METHODPOKESPOT_H
#define LEGALITY_RNG_METHODPOKESPOT_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        /// Bonsly-only spawning is never needed: its 30% is less forgiving than Munchlax's 10%.
        enum class PokeSpotSetup : uint8_t
        {
            Invalid,
            Neither,
            Munchlax,
        };

        struct MethodPokeSpot
        {
            static PokeSpotSetup isValidActivation(uint8_t slot, uint32_t seed, uint32_t &origin) noexcept;

            static bool tryGetOriginSeeds(const Pokemon::Pokemon &pokemon, uint8_t slotNumber, uint8_t levelMin, uint8_t levelMax, uint32_t &PIDOrigin, uint32_t &IVOrigin) noexcept;
            static bool tryGetOriginSeedIVs(const Pokemon::Pokemon &pokemon, uint8_t levelMin, uint8_t levelMax, uint32_t &origin) noexcept;
            static bool tryGetOriginSeedPID(uint32_t PID, uint8_t slot, uint32_t &origin) noexcept;
            static bool tryGetOriginSeedIVs(uint32_t firstIVs, uint32_t secondIVs, uint8_t levelMin, uint8_t levelMax, uint8_t metLevel, bool hasOriginalMetLevel, uint32_t &origin) noexcept;

            static constexpr uint8_t getSlot(uint32_t encounterSlotValue) noexcept { return encounterSlotValue < 50 ? 0 : (encounterSlotValue < 85 ? 1 : 2); }
            static constexpr bool isSlotValid(uint8_t slot, uint32_t encounterSlotValue) noexcept { return getSlot(encounterSlotValue) == slot; }

            static bool isValidAnimation(uint32_t seed, uint32_t &origin, uint32_t &animation) noexcept;
        };
    }
}

#endif
