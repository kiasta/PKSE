/**
 * GENERATED -- do not hand-edit. Black/White save block table.
 *
 * Regenerate with `python tools/gen_blocktables.py`. Source: PKHeX SaveBlockAccessor5*.
 *
 * ONE GROUP, ONE TABLE. Black/White does not borrow a sibling group's geometry, even where the two
 * block orders agree for a while (Black/White and Black 2/White 2 agree up to #27 and diverge at
 * #32): a shared table would read one game's save with the other's offsets past that point. The
 * named indices below are looked up in THIS table by block name, so they cannot drift from the
 * rows above them.
 *
 * See BlockTable.h for the row type, and for why only six groups have a table at all.
 */
#ifndef TRAINER_BLOCKS5BW_H
#define TRAINER_BLOCKS5BW_H

#include "Trainer/BlockTable.h"

namespace Trainer
{
    inline constexpr size_t SAVE_SIZE_5BW = 0x24000;
    inline constexpr size_t CHECKSUM_BLOCK_LENGTH_5BW = 0x8c;

    inline constexpr BlockEntryNDS BLOCKS_5BW[] = {
        { 0x00000, 0x03e0, 0x003e2, 0x23f00 },  // 00 Box Names
        { 0x00400, 0x0ff0, 0x013f2, 0x23f02 },  // 01 Box 1
        { 0x01400, 0x0ff0, 0x023f2, 0x23f04 },  // 02 Box 2
        { 0x02400, 0x0ff0, 0x033f2, 0x23f06 },  // 03 Box 3
        { 0x03400, 0x0ff0, 0x043f2, 0x23f08 },  // 04 Box 4
        { 0x04400, 0x0ff0, 0x053f2, 0x23f0a },  // 05 Box 5
        { 0x05400, 0x0ff0, 0x063f2, 0x23f0c },  // 06 Box 6
        { 0x06400, 0x0ff0, 0x073f2, 0x23f0e },  // 07 Box 7
        { 0x07400, 0x0ff0, 0x083f2, 0x23f10 },  // 08 Box 8
        { 0x08400, 0x0ff0, 0x093f2, 0x23f12 },  // 09 Box 9
        { 0x09400, 0x0ff0, 0x0a3f2, 0x23f14 },  // 10 Box 10
        { 0x0a400, 0x0ff0, 0x0b3f2, 0x23f16 },  // 11 Box 11
        { 0x0b400, 0x0ff0, 0x0c3f2, 0x23f18 },  // 12 Box 12
        { 0x0c400, 0x0ff0, 0x0d3f2, 0x23f1a },  // 13 Box 13
        { 0x0d400, 0x0ff0, 0x0e3f2, 0x23f1c },  // 14 Box 14
        { 0x0e400, 0x0ff0, 0x0f3f2, 0x23f1e },  // 15 Box 15
        { 0x0f400, 0x0ff0, 0x103f2, 0x23f20 },  // 16 Box 16
        { 0x10400, 0x0ff0, 0x113f2, 0x23f22 },  // 17 Box 17
        { 0x11400, 0x0ff0, 0x123f2, 0x23f24 },  // 18 Box 18
        { 0x12400, 0x0ff0, 0x133f2, 0x23f26 },  // 19 Box 19
        { 0x13400, 0x0ff0, 0x143f2, 0x23f28 },  // 20 Box 20
        { 0x14400, 0x0ff0, 0x153f2, 0x23f2a },  // 21 Box 21
        { 0x15400, 0x0ff0, 0x163f2, 0x23f2c },  // 22 Box 22
        { 0x16400, 0x0ff0, 0x173f2, 0x23f2e },  // 23 Box 23
        { 0x17400, 0x0ff0, 0x183f2, 0x23f30 },  // 24 Box 24
        { 0x18400, 0x09c0, 0x18dc2, 0x23f32 },  // 25 Inventory
        { 0x18e00, 0x0534, 0x19336, 0x23f34 },  // 26 Party Pokémon
        { 0x19400, 0x0068, 0x1946a, 0x23f36 },  // 27 Trainer Data
        { 0x19500, 0x009c, 0x1959e, 0x23f38 },  // 28 Trainer Position
        { 0x19600, 0x1338, 0x1a93a, 0x23f3a },  // 29 Unity Tower and survey stuff
        { 0x1aa00, 0x07c4, 0x1b1c6, 0x23f3c },  // 30 Pal Pad Player Data
        { 0x1b200, 0x0d54, 0x1bf56, 0x23f3e },  // 31 Pal Pad Friend Data
        { 0x1c000, 0x002c, 0x1c02e, 0x23f40 },  // 32 Skin Info
        { 0x1c100, 0x0658, 0x1c75a, 0x23f42 },  // 33 ??? Gym badge data
        { 0x1c800, 0x0a94, 0x1d296, 0x23f44 },  // 34 Mystery Gift
        { 0x1d300, 0x01ac, 0x1d4ae, 0x23f46 },  // 35 Dream World Stuff (Catalog)
        { 0x1d500, 0x03ec, 0x1d8ee, 0x23f48 },  // 36 Chatter
        { 0x1d900, 0x005c, 0x1d95e, 0x23f4a },  // 37 Adventure Info
        { 0x1da00, 0x01e0, 0x1dbe2, 0x23f4c },  // 38 Trainer Card Records
        { 0x1dc00, 0x00a8, 0x1dcaa, 0x23f4e },  // 39 ???
        { 0x1dd00, 0x0460, 0x1e162, 0x23f50 },  // 40 Mail
        { 0x1e200, 0x1400, 0x1f602, 0x23f52 },  // 41 Overworld State
        { 0x1f700, 0x02a4, 0x1f9a6, 0x23f54 },  // 42 Musical
        { 0x1fa00, 0x02dc, 0x1fcde, 0x23f56 },  // 43 White Forest + Black City Data
        { 0x1fd00, 0x034c, 0x2004e, 0x23f58 },  // 44 IR
        { 0x20100, 0x03ec, 0x204ee, 0x23f5a },  // 45 EventWork
        { 0x20500, 0x00f8, 0x205fa, 0x23f5c },  // 46 GTS
        { 0x20600, 0x02fc, 0x208fe, 0x23f5e },  // 47 Regulation Tournament
        { 0x20900, 0x0094, 0x20996, 0x23f60 },  // 48 Gimmick
        { 0x20a00, 0x035c, 0x20d5e, 0x23f62 },  // 49 Battle Box
        { 0x20e00, 0x01cc, 0x20fce, 0x23f64 },  // 50 Daycare
        { 0x21000, 0x0168, 0x2116a, 0x23f66 },  // 51 Strength Boulder Status
        { 0x21200, 0x00ec, 0x212ee, 0x23f68 },  // 52 Badge Flags, Money, Trainer Sayings
        { 0x21300, 0x01b0, 0x214b2, 0x23f6a },  // 53 Entralink (Level & Powers etc)
        { 0x21500, 0x001c, 0x2151e, 0x23f6c },  // 54 ???
        { 0x21600, 0x04d4, 0x21ad6, 0x23f6e },  // 55 Pokedex
        { 0x21b00, 0x0034, 0x21b36, 0x23f70 },  // 56 Encount Swarm and other overworld info - 2C - swarm, 2D - repel steps, 2E repel type
        { 0x21c00, 0x003c, 0x21c3e, 0x23f72 },  // 57 Battle Subway Play Info
        { 0x21d00, 0x01ac, 0x21eae, 0x23f74 },  // 58 Battle Subway Score Info
        { 0x21f00, 0x0b90, 0x22a92, 0x23f76 },  // 59 Battle Subway Wi-Fi Info
        { 0x22b00, 0x009c, 0x22b9e, 0x23f78 },  // 60 Online Records
        { 0x22c00, 0x0850, 0x23452, 0x23f7a },  // 61 Entralink Forest pokémon data
        { 0x23500, 0x0028, 0x2352a, 0x23f7c },  // 62 ???
        { 0x23600, 0x0284, 0x23886, 0x23f7e },  // 63 Answered Questions
        { 0x23900, 0x0010, 0x23912, 0x23f80 },  // 64 Unity Tower
        { 0x23a00, 0x005c, 0x23a5e, 0x23f82 },  // 65 Battle Institute
        { 0x23b00, 0x016c, 0x23c6e, 0x23f84 },  // 66 ???
        { 0x23d00, 0x0040, 0x23d42, 0x23f86 },  // 67 ???
        { 0x23e00, 0x00fc, 0x23efe, 0x23f88 },  // 68 ???
        { 0x23f00, 0x008c, 0x23f9a, 0x23f9a },  // 69 Checksums */
    };
    inline constexpr size_t BLOCK_COUNT_5BW = sizeof(BLOCKS_5BW) / sizeof(BlockEntryNDS);

    // Block indices the save layer reaches for, resolved by NAME out of the table above.
    inline constexpr size_t BLOCK_BOX_NAMES_5BW = 0;
    inline constexpr size_t BLOCK_BOX_FIRST_5BW = 1;
    inline constexpr size_t BLOCK_INVENTORY_5BW = 25;
    inline constexpr size_t BLOCK_PARTY_5BW = 26;
    inline constexpr size_t BLOCK_TRAINER_5BW = 27;
    inline constexpr size_t BOX_BLOCK_COUNT_5BW = 24;
}

#endif  // TRAINER_BLOCKS5BW_H
