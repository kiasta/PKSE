/**
 * What an unedited write of an SCBlock save (Sword/Shield, Legends: Arceus, Scarlet/Violet, Legends:
 * Z-A) is allowed to change in its box block, shared by the self test's save suite and the host
 * harness so both judge a write by the same arithmetic.
 *
 * The box writer stamps one canonical encrypted blank over every EMPTY slot, so those bytes may
 * change. An OCCUPIED slot must come back byte for byte. Deciding which slot a changed byte belongs
 * to needs the format's real record stride, and it cannot be worked out from the block: Scarlet/
 * Violet's box block holds 990 records for its 960 visible slots, so dividing the block's size by
 * the slot count gives 354 bytes instead of 344. Every slot after the first then sits further from
 * its true offset, and the normalised blanks were reported as changes to the Pokemon beside them.
 * The stride is therefore each format's own constant, the one its Trainer class writes with.
 */
#ifndef TEST_SCBLOCKREWRITE_H
#define TEST_SCBLOCKREWRITE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Enums/GameVersion.h"
#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"

namespace Test
{
    /// Where an SCBlock save keeps the blocks an unedited write may touch.
    struct SCBlockLayout
    {
        uint32_t boxBlockKey = 0;
        size_t boxSlotStride = 0; ///< bytes from one box record to the next, gap included
        std::vector<uint32_t> pokedexBlockKeys;
    };

    /// Fills `layout` for an SCBlock format and returns true, or returns false for any other format.
    inline bool scBlockLayout(Enums::GameVersion group, SCBlockLayout &layout)
    {
        switch (group)
        {
        case Enums::GameVersion::SWSH:
            layout.boxBlockKey = Trainer::BOX8_SWSH;
            layout.boxSlotStride = Encryption::SIZE_PARTY8_SWSH;
            layout.pokedexBlockKeys = {Trainer::SAVE_REVISION8_SWSH, Trainer::SAVE_REVISION8_R1_SWSH, Trainer::SAVE_REVISION8_R2_SWSH};
            return true;
        case Enums::GameVersion::PLA:
            layout.boxBlockKey = Trainer::BOX8_LA;
            layout.boxSlotStride = Encryption::SIZE_STORED8_LA;
            layout.pokedexBlockKeys = {Trainer::POKEDEX8_LA};
            return true;
        case Enums::GameVersion::SV:
            layout.boxBlockKey = Trainer::BOX9_SV;
            layout.boxSlotStride = Encryption::SIZE_PARTY9_SV;
            layout.pokedexBlockKeys = {Trainer::ZUKAN9_SV_PALDEA, Trainer::ZUKAN9_SV_KITAKAMI};
            return true;
        case Enums::GameVersion::ZA:
            layout.boxBlockKey = Trainer::BOX9_LZA;
            layout.boxSlotStride = Encryption::BOX_SLOT_SIZE9_LZA;
            layout.pokedexBlockKeys = {Trainer::POKEDEX9_LZA};
            return true;
        default:
            return false;
        }
    }

    /// The changed bytes of a box block, sorted by whether the slot they sit in holds a Pokemon.
    struct BoxSlotChanges
    {
        size_t occupiedSlotByteCount = 0;
        size_t firstOccupiedSlotIndex = 0; ///< storage index (box * slots per box + slot)
        size_t emptySlotByteCount = 0;
    };

    /// Sorts every byte that differs between two copies of a box block by the slot it belongs to,
    /// judged against `trainer`'s boxes. A record past the last visible slot counts as empty.
    ///
    /// Returns false when the blocks cannot be compared slot by slot: the copies differ in size, the
    /// stride is 0, or the block is too small to hold every visible slot at that stride.
    inline bool countBoxSlotChanges(const Trainer::Trainer &trainer, const std::vector<uint8_t> &beforeBoxData, const std::vector<uint8_t> &afterBoxData, size_t boxSlotStride, BoxSlotChanges &changes)
    {
        changes = BoxSlotChanges{};
        const size_t slotsPerBox = trainer.getSlotsPerBox();
        if (boxSlotStride == 0 || slotsPerBox == 0 || beforeBoxData.size() != afterBoxData.size() || beforeBoxData.size() < trainer.getBoxCount() * slotsPerBox * boxSlotStride)
        {
            return false;
        }
        for (size_t byteIndex = 0; byteIndex < beforeBoxData.size(); ++byteIndex)
        {
            if (beforeBoxData[byteIndex] == afterBoxData[byteIndex])
            {
                continue;
            }
            const size_t storageSlotIndex = byteIndex / boxSlotStride;
            const size_t boxIndex = storageSlotIndex / slotsPerBox;
            const size_t slotIndex = storageSlotIndex % slotsPerBox;
            const bool slotIsEmpty = boxIndex >= trainer.boxes.size() || slotIndex >= Trainer::BOX_SLOTS || isEmptySlot(trainer.boxes[boxIndex][slotIndex].get());
            if (slotIsEmpty)
            {
                ++changes.emptySlotByteCount;
                continue;
            }
            if (changes.occupiedSlotByteCount == 0)
            {
                changes.firstOccupiedSlotIndex = storageSlotIndex;
            }
            ++changes.occupiedSlotByteCount;
        }
        return true;
    }
}

#endif // TEST_SCBLOCKREWRITE_H
