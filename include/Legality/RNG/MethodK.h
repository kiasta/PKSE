/**
 * HeartGold and SoulSilver wild encounters (Method K): walks back from the PID's seed to the seed
 * that started the encounter, and the lead it needed.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/MethodK.cs. The Safari Zone and the
 * Bug-Catching Contest reroll a spread with no 31 IV up to three times, so a match can sit behind
 * up to three rejected PID/IV spreads; the search recurses through them.
 */
#ifndef LEGALITY_RNG_METHODK_H
#define LEGALITY_RNG_METHODK_H

#include <cstdint>
#include <span>

#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/RNG/LeadSeed.h"

namespace Legality
{
    namespace RNG
    {
        struct MethodK
        {
            static constexpr uint8_t FORMAT = 4;
            static constexpr uint8_t FORMAT_NO_LEVEL_CHECK = 0;

            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept;
            /// The slot's own level range, with no met level check.
            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept;
            /// An evolution's level range, checked against the met level of a Gen 4 format.
            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax) noexcept;

            static int getReversalWindow(uint32_t seed, uint8_t nature) noexcept;

            static uint32_t getNature(uint32_t random) noexcept { return random % 25; }

            static LeadSeed getOriginSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t nature, int reverseCount, uint8_t levelMin, uint8_t levelMax, uint8_t format = FORMAT) noexcept;

            static bool isEncounterCheckApplicable(SlotType4 type) noexcept;

            static uint32_t skipToLevelRand(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead) noexcept;

            static bool checkEncounterActivation(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept;

            static bool tryGetMatchCuteCharm(const EncounterSlot4Parameters &encounter, std::span<const uint32_t> seeds, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format, LeadSeed &result) noexcept;

            /// True when any of the three IVs packed in a 15-bit IV word is 31.
            static constexpr bool isAny31(uint32_t ivWord) noexcept
            {
                return (ivWord & 0x1F) == 0x1F || ((ivWord >> 5) & 0x1F) == 0x1F || ((ivWord >> 10) & 0x1F) == 0x1F;
            }

            static bool isLevelRand(const EncounterSlot4Parameters &encounter) noexcept { return isLevelRandHGSS(encounter.type); }

            static uint32_t getRandomLevel(const EncounterSlot4Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept;
        };
    }
}

#endif
