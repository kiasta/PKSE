/**
 * The same `u16 id, u16 count` codec as Gen 4, and positional in exactly the same way. FIVE
 * pouches rather than Gen 4's eight -- Gen 5 folds Poke Balls and battle items back into Items.
 *
 * Offsets are relative to the Inventory block (block 25).
 */
#ifndef TRAINER_INVENTORY5_B2W2_H
#define TRAINER_INVENTORY5_B2W2_H

#include <cstddef>
#include <cstdint>

namespace Trainer
{
    /// One pouch's home in the bag: where it starts, how many 4-byte slots it holds, and the
    /// per-slot cap the game enforces.
    struct PouchLayout5B2W2
    {
        const char *name;
        size_t offset; // from the Inventory block
        size_t slots;
        int maxCount;
    };

    inline constexpr int POUCH_COUNT5_B2W2 = 5;

    inline constexpr PouchLayout5B2W2 POUCHES5_B2W2[POUCH_COUNT5_B2W2] = {
        {"Items", 0x000, 310, 999},
        {"Key Items", 0x4d8, 83, 1},
        {"TMs/HMs", 0x624, 109, 1},
        {"Medicine", 0x7d8, 48, 999},
        {"Berries", 0x898, 64, 999}, // PKHeX ItemStorage5.Berry: the bytes after it are not the pouch
    };

}

#endif
