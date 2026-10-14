#ifndef TRAINER_INVENTORY_H
#define TRAINER_INVENTORY_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Utils/HelperUtilities.h"

namespace Trainer
{
    struct InventoryItem
    {
        uint16_t itemId;
        uint16_t count;
        // Initialised: a reader that leaves them unset must not hand the writers, which set the
        // game's "new" marker from isNew, whatever was on the stack.
        bool isNew = false;
        bool isFavorite = false;
    };

    /// Where an item was read from, so an entry nobody touched can go back to its own slot.
    struct ItemOrigin
    {
        uint16_t slot;
        uint16_t itemId;
    };

    /// Where one item is written, and which recorded origin it was matched to. `slot` equal to the
    /// pouch's slot count means the entry is not written at all; `originIndex` equal to the origin
    /// list's size means it matched nothing that was read, so it is new to this pouch.
    struct ItemPlacement
    {
        size_t slot;
        size_t originIndex;
    };

    /**
     * Which slot of a POSITIONAL pouch each item goes back to, whatever a slot's encoding.
     *
     * Two passes: an item that was in the pouch when it was read goes back to its own slot, so an
     * untouched pouch reproduces itself exactly; everything else takes the lowest free slot.
     *
     * **ITEMS ARE MATCHED TO THEIR ORIGIN BY ID, NOT BY LIST POSITION.** Removing an entry shifts
     * every later one down a place, so matching by position would find a different item at each of
     * those positions and send the whole tail of the pouch to new slots -- on a real Sword save,
     * removing the fourth medicine moved a used-up Max Potion from slot 59 to slot 16. Where a
     * pouch holds the same id more than once, the nth entry takes the nth recorded slot, which
     * reproduces an untouched pouch exactly as position matching did.
     */
    inline std::vector<ItemPlacement> placePouchPositional(size_t slots, const std::vector<InventoryItem> &items,
                                                           const std::vector<ItemOrigin> &origin)
    {
        std::vector<bool> taken(slots, false);
        std::vector<bool> originTaken(origin.size(), false);
        std::vector<ItemPlacement> placement(items.size(), ItemPlacement{slots, origin.size()});

        for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            if (items[itemIndex].itemId == 0)
                continue;
            for (size_t originIndex = 0; originIndex < origin.size(); ++originIndex)
            {
                if (originTaken[originIndex] || origin[originIndex].itemId != items[itemIndex].itemId)
                    continue;
                const size_t slotIndex = origin[originIndex].slot;
                if (slotIndex >= slots || taken[slotIndex])
                    continue;
                placement[itemIndex] = ItemPlacement{slotIndex, originIndex};
                originTaken[originIndex] = true;
                taken[slotIndex] = true;
                break;
            }
        }
        size_t next = 0;
        for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            if (items[itemIndex].itemId == 0 || placement[itemIndex].slot != slots)
                continue;
            while (next < slots && taken[next])
                ++next;
            if (next >= slots)
                break;
            placement[itemIndex].slot = next;
            taken[next] = true;
        }
        return placement;
    }

    /**
     * Writes a pouch of `u16 id, u16 count` entries back POSITIONALLY (Gen 4 and Gen 5).
     *
     * THESE POUCHES ARE NOT PACKED. PKHeX's InventoryPouch4 reads and writes every slot of the
     * pouch rather than stopping at the first empty one, and Gen 6/7 -- which share this codec --
     * demonstrably park used-up entries after a long run of empty slots. Re-packing on write
     * would silently reorder a bag nobody edited, and stopping at the first hole on read would
     * lose everything past it.
     */
    inline void writePouchPositional(uint8_t *pouch, size_t slots, const std::vector<InventoryItem> &items,
                                     const std::vector<ItemOrigin> &origin)
    {
        const std::vector<ItemPlacement> placement = placePouchPositional(slots, items, origin);

        // Every slot is four bytes and every placed entry overwrites all four, so clearing the
        // whole pouch first leaves exactly the untaken slots at zero.
        std::memset(pouch, 0, slots * 4);
        for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            if (placement[itemIndex].slot == slots)
                continue;
            uint8_t *entry = pouch + placement[itemIndex].slot * 4;
            Utils::writeUInt16LittleEndian(entry, items[itemIndex].itemId);
            Utils::writeUInt16LittleEndian(entry + 2, items[itemIndex].count);
        }
    }
}

#endif