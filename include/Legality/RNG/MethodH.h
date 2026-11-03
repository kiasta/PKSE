/**
 * Gen 3 wild encounters (Method H): walks back from the PID's seed through the nature roll, the
 * lead's ability rolls, the level and the slot, to the seed that started the encounter.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/MethodH.cs, MethodHCondition.cs and
 * MethodHWindowInfo.cs. The encounter template is an EncounterSlot3Parameters; the level range PKHeX
 * takes from its evolution criteria is passed as levelMin/levelMax.
 */
#ifndef LEGALITY_RNG_METHODH_H
#define LEGALITY_RNG_METHODH_H

#include <cstdint>

#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/RNG/LeadSeed.h"

namespace Legality
{
    namespace RNG
    {
        enum class MethodHCondition : uint8_t
        {
            Empty = 0,
            Regular,
            Emerald,
            Unown,
        };

        /// How many PID frames to step back for each lead family.
        struct MethodHWindowInfo
        {
            uint16_t countRegular = 0;
            MethodHCondition type = MethodHCondition::Empty;
            uint8_t gender = 0;
            uint16_t countCute = 0;

            /// True when the window was never computed, or Emerald's Cute Charm window used another gender ratio.
            bool shouldRevise(uint16_t species) const noexcept;
        };

        struct MethodH
        {
            /// The format whose met level is the original; any other format skips the exact met level check.
            static constexpr uint8_t FORMAT = 3;
            static constexpr uint8_t FORMAT_NO_LEVEL_CHECK = 0;

            static LeadSeed getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, bool emerald, uint8_t gender, uint8_t format) noexcept;
            /// The slot's own level range, with no met level check.
            static LeadSeed getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, bool emerald, uint8_t gender) noexcept;
            /// An evolution's level range, checked against the met level of a Gen 3 format.
            static LeadSeed getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, bool emerald, uint8_t gender, uint8_t levelMin, uint8_t levelMax) noexcept;

            /// `version` is the raw GameVersion value.
            static MethodHCondition getCondition(uint16_t species, uint8_t version) noexcept;

            static MethodHWindowInfo getReversalWindow(const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t nature, bool emerald, uint8_t gender) noexcept;

            static LeadSeed getOriginSeed(const MethodHWindowInfo &info, const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format = FORMAT) noexcept;

            static bool isEncounterCheckApplicable(SlotType3 type) noexcept;

            static uint32_t skipToLevelRand(const EncounterSlot3Parameters &encounter, uint32_t seed) noexcept;

            static bool checkEncounterActivation(const EncounterSlot3Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept;

            static uint32_t getRandomLevel(const EncounterSlot34Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept;

            static bool tryGetSeedTrainerID(uint16_t TID16, uint16_t SID16, uint32_t &seed) noexcept;

            static bool tryGetShinySID(uint16_t TID16, uint16_t &SID16, uint32_t shinyXor, uint32_t bits = 0) noexcept;
        };
    }
}

#endif
