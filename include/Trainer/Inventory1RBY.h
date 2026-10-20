#ifndef TRAINER_INVENTORY1_RBY_H
#define TRAINER_INVENTORY1_RBY_H

#include <cstddef>

#include "Trainer/Inventory.h"

namespace Trainer
{
    /**
     * Red/Blue/Yellow bag pouches. Gen 1 has exactly two containers and ONE legal item set behind
     * both: PKHeX's ItemStorage1 exposes a single `General` span and returns it for every
     * InventoryType, because the item PC in Gen 1 accepts everything the bag does.
     *
     * Unlike every later generation, the OFFSETS are not constants here. A Gen 1 save's layout
     * depends on its locale -- a Japanese save's name fields are 6 bytes instead of 11 and every
     * offset from the trainer block onward shifts -- so the addresses live in Trainer1RBY's
     * RBYOffsets and only the pouch identity, display name and slot count are fixed.
     *
     * Each slot on disk is 2 bytes: u8 item id + u8 count, preceded by a one-byte count and
     * terminated by 0xFF. That framing is Trainer1RBY's business; this header exists so the pouch
     * names and capacities have one home, the way they do for every other game.
     */
    enum class PouchType1RBY
    {
        Items = 0, // "Items"    -- the bag, 20 slots
        PCItems,   // "PC Items" -- the item PC in the player's bedroom, 50 slots
        Count
    };

    constexpr size_t POUCH_COUNT1_RBY = static_cast<size_t>(PouchType1RBY::Count);

    // Largest stack in either pouch (PKHeX PlayerBag1). The games keep one count byte per slot
    // and never fill one past 99.
    constexpr int ITEM_MAX_COUNT1_RBY = 99;

    struct PouchInfo1RBY
    {
        PouchType1RBY type;
        const char *name;
        int maxSlots; // number of 2-byte slots the game will store
    };

    inline const PouchInfo1RBY &getPouchInfo1RBY(PouchType1RBY type)
    {
        static const PouchInfo1RBY pouches[] = {
            {PouchType1RBY::Items, "Items", 20},
            {PouchType1RBY::PCItems, "PC Items", 50},
        };
        int index = static_cast<int>(type);
        if (index < 0 || index >= static_cast<int>(POUCH_COUNT1_RBY))
            index = 0;
        return pouches[index];
    }
}

#endif // TRAINER_INVENTORY1_RBY_H
