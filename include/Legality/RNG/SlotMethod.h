/**
 * The encounter slot a Gen 3 or Gen 4 slot roll selects, and the roll range a slot covers.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/SlotMethodH.cs and
 * Gen4/SlotMethodJ.cs, SlotMethodK.cs. Every slot is in [0, X], so INVALID_SLOT never matches one.
 */
#ifndef LEGALITY_RNG_SLOTMETHOD_H
#define LEGALITY_RNG_SLOTMETHOD_H

#include <cstdint>

#include "Legality/RNG/EncounterSlotRNG.h"

namespace Legality
{
    namespace RNG
    {
        constexpr uint8_t INVALID_SLOT = 0xFF;

        struct SlotRange
        {
            uint8_t minimum;
            uint8_t maximum;
        };

        struct SlotMethodH
        {
            static uint8_t getSlot(SlotType3 type, uint32_t random) noexcept;
            static SlotRange getRange(SlotType3 type, uint8_t slotNumber) noexcept;

            static uint8_t getRegular(uint32_t roll) noexcept;
            static uint8_t getSurf(uint32_t roll) noexcept;
            static uint8_t getOldRod(uint32_t roll) noexcept;
            static uint8_t getGoodRod(uint32_t roll) noexcept;
            static uint8_t getSuperRod(uint32_t roll) noexcept;

            static SlotRange getRangeGrass(uint8_t slotNumber) noexcept;
            static SlotRange getRangeSurf(uint8_t slotNumber) noexcept;
            static SlotRange getRangeOldRod(uint8_t slotNumber) noexcept;
            static SlotRange getRangeGoodRod(uint8_t slotNumber) noexcept;
            static SlotRange getRangeSuperRod(uint8_t slotNumber) noexcept;
        };

        /// Diamond, Pearl and Platinum: the roll is rand() / 656 rather than a modulo.
        struct SlotMethodJ
        {
            static int getSlot(SlotType4 type, uint32_t random) noexcept;
            static SlotRange getRange(SlotType4 type, uint8_t slotNumber) noexcept;

            static int getSuperRod(uint32_t roll) noexcept;
            static SlotRange getRangeSuperRod(uint8_t slotNumber) noexcept;
        };

        /// HeartGold and SoulSilver.
        struct SlotMethodK
        {
            static uint8_t getSlot(SlotType4 type, uint32_t random) noexcept;
            static SlotRange getRange(SlotType4 type, uint8_t slotNumber) noexcept;

            static uint8_t getRockSmash(uint32_t roll) noexcept;
            static uint8_t getSuperRod(uint32_t roll) noexcept;
            static uint8_t getSafari(uint32_t roll) noexcept;
            static uint8_t getBugCatchingContest(uint32_t roll) noexcept;
            static uint8_t getHeadbutt(uint32_t roll) noexcept;

            static SlotRange getRangeSuperRod(uint8_t slotNumber) noexcept;
            static SlotRange getRangeRockSmash(uint8_t slotNumber) noexcept;
            static SlotRange getRangeHeadbutt(uint8_t slotNumber) noexcept;
            static SlotRange getRangeBugCatchingContest(uint8_t slotNumber) noexcept;
            static SlotRange getRangeSafari(uint8_t slotNumber) noexcept;
        };
    }
}

#endif
