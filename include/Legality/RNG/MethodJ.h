/**
 * Diamond, Pearl and Platinum wild encounters (Method J): walks back from the PID's seed to the
 * seed that started the encounter, and the lead it needed.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/MethodJ.cs. Constant-range rolls are
 * rand() / ((0xFFFF / x) + 1) rather than a modulo; variable ranges (level, Static/Magnet Pull) use
 * a modulo.
 */
#ifndef LEGALITY_RNG_METHODJ_H
#define LEGALITY_RNG_METHODJ_H

#include <cstdint>
#include <span>

#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/RNG/LeadSeed.h"

namespace Legality
{
    namespace RNG
    {
        struct MethodJ
        {
            static constexpr uint8_t FORMAT = 4;
            static constexpr uint8_t FORMAT_NO_LEVEL_CHECK = 0;

            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept;
            /// The slot's own level range, with no met level check.
            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept;
            /// An evolution's level range, checked against the met level of a Gen 4 format.
            static LeadSeed getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax) noexcept;

            static uint32_t getNature(uint32_t random) noexcept { return random / 0xA3Eu; }
            /// rand(11) through the game's pre-compiled divisor.
            static uint32_t getHoneyTreeLevel(uint32_t random) noexcept { return 5 + (random / 0x1745); }

            static LeadSeed getOriginSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t nature, int reverseCount, uint8_t levelMin, uint8_t levelMax, uint8_t format = FORMAT) noexcept;

            static bool isEncounterCheckApplicable(SlotType4 type) noexcept;

            static uint32_t skipToLevelRand(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept;

            static bool checkEncounterActivation(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept;

            static bool tryGetMatchCuteCharm(const EncounterSlot4Parameters &encounter, std::span<const uint32_t> seeds, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format, LeadSeed &result) noexcept;

            static bool isLevelRand(const EncounterSlot4Parameters &encounter) noexcept { return isLevelRandDPPt(encounter.type); }

            static uint32_t getRandomLevel(const EncounterSlot4Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept;

            /// How many PID frame pairs to step back (from the second PID call) before the nature matches.
            static int getReversalWindow(uint32_t seed, uint8_t nature) noexcept;
        };
    }
}

#endif
