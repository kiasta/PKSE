/**
 * `u16 id, u16 count`, exactly Gen 4's codec (PKHeX InventoryPouch4), and positional in the same
 * way. NOT Gen 7's -- Gen 7 packs a whole slot into one u32, and the two look interchangeable
 * until a Gen 7 bag is read as pairs of u16s.
 *
 * Pouches are contiguous, so each one's size is the span to the next.
 * Offsets are relative to the MyItem block (block 1).
 */
#ifndef TRAINER_INVENTORY6_ORAS_H
#define TRAINER_INVENTORY6_ORAS_H

#include <cstddef>
#include <cstdint>

namespace Trainer
{
    /// One pouch's home in the bag: where it starts, how many 4-byte slots it holds, and the
    /// per-slot cap the game enforces.
    struct PouchLayout6ORAS
    {
        const char *name;
        size_t offset; // from the MyItem block
        size_t slots;
        int maxCount;
    };

    inline constexpr int POUCH_COUNT6_ORAS = 5;

    inline constexpr PouchLayout6ORAS POUCHES6_ORAS[POUCH_COUNT6_ORAS] = {
        {"Items", 0x000, 400, 999},
        {"Key Items", 0x640, 96, 1},
        {"TMs/HMs", 0x7c0, 108, 1},
        {"Medicine", 0x970, 64, 999},
        {"Berries", 0xa70, 67, 999}, // PKHeX ItemStorage6XY.Berry: the bytes after it are not the pouch
    };

}

#endif
