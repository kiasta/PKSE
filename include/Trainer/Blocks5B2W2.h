/**
 * GENERATED -- do not hand-edit. Black 2/White 2 save block table.
 *
 * Regenerate with `python tools/gen_blocktables.py`. Source: PKHeX SaveBlockAccessor5*.
 *
 * ONE GROUP, ONE TABLE. Black 2/White 2 does not borrow a sibling group's geometry, even where the two
 * block orders agree for a while (Black/White and Black 2/White 2 agree up to #27 and diverge at
 * #32): a shared table would read one game's save with the other's offsets past that point. The
 * named indices below are looked up in THIS table by block name, so they cannot drift from the
 * rows above them.
 *
 * See BlockTable.h for the row type, and for why only six groups have a table at all.
 */
#ifndef TRAINER_BLOCKS5B2W2_H
#define TRAINER_BLOCKS5B2W2_H

#include "Trainer/BlockTable.h"

namespace Trainer
{
    inline constexpr size_t SAVE_SIZE_5B2W2 = 0x26000;
    inline constexpr size_t CHECKSUM_BLOCK_LENGTH_5B2W2 = 0x94;

    inline constexpr BlockEntryNDS BLOCKS_5B2W2[] = {
        { 0x00000, 0x03e0, 0x003e2, 0x25f00 },  // 00 Box Names
        { 0x00400, 0x0ff0, 0x013f2, 0x25f02 },  // 01 Box 1
        { 0x01400, 0x0ff0, 0x023f2, 0x25f04 },  // 02 Box 2
        { 0x02400, 0x0ff0, 0x033f2, 0x25f06 },  // 03 Box 3
        { 0x03400, 0x0ff0, 0x043f2, 0x25f08 },  // 04 Box 4
        { 0x04400, 0x0ff0, 0x053f2, 0x25f0a },  // 05 Box 5
        { 0x05400, 0x0ff0, 0x063f2, 0x25f0c },  // 06 Box 6
        { 0x06400, 0x0ff0, 0x073f2, 0x25f0e },  // 07 Box 7
        { 0x07400, 0x0ff0, 0x083f2, 0x25f10 },  // 08 Box 8
        { 0x08400, 0x0ff0, 0x093f2, 0x25f12 },  // 09 Box 9
        { 0x09400, 0x0ff0, 0x0a3f2, 0x25f14 },  // 10 Box 10
        { 0x0a400, 0x0ff0, 0x0b3f2, 0x25f16 },  // 11 Box 11
        { 0x0b400, 0x0ff0, 0x0c3f2, 0x25f18 },  // 12 Box 12
        { 0x0c400, 0x0ff0, 0x0d3f2, 0x25f1a },  // 13 Box 13
        { 0x0d400, 0x0ff0, 0x0e3f2, 0x25f1c },  // 14 Box 14
        { 0x0e400, 0x0ff0, 0x0f3f2, 0x25f1e },  // 15 Box 15
        { 0x0f400, 0x0ff0, 0x103f2, 0x25f20 },  // 16 Box 16
        { 0x10400, 0x0ff0, 0x113f2, 0x25f22 },  // 17 Box 17
        { 0x11400, 0x0ff0, 0x123f2, 0x25f24 },  // 18 Box 18
        { 0x12400, 0x0ff0, 0x133f2, 0x25f26 },  // 19 Box 19
        { 0x13400, 0x0ff0, 0x143f2, 0x25f28 },  // 20 Box 20
        { 0x14400, 0x0ff0, 0x153f2, 0x25f2a },  // 21 Box 21
        { 0x15400, 0x0ff0, 0x163f2, 0x25f2c },  // 22 Box 22
        { 0x16400, 0x0ff0, 0x173f2, 0x25f2e },  // 23 Box 23
        { 0x17400, 0x0ff0, 0x183f2, 0x25f30 },  // 24 Box 24
        { 0x18400, 0x09ec, 0x18dee, 0x25f32 },  // 25 Inventory
        { 0x18e00, 0x0534, 0x19336, 0x25f34 },  // 26 Party Pokémon
        { 0x19400, 0x00b0, 0x194b2, 0x25f36 },  // 27 Trainer Data
        { 0x19500, 0x00a8, 0x195aa, 0x25f38 },  // 28 Trainer Position
        { 0x19600, 0x1338, 0x1a93a, 0x25f3a },  // 29 Unity Tower and survey stuff
        { 0x1aa00, 0x07c4, 0x1b1c6, 0x25f3c },  // 30 Pal Pad Player Data
        { 0x1b200, 0x0d54, 0x1bf56, 0x25f3e },  // 31 Pal Pad Friend Data
        { 0x1c000, 0x0094, 0x1c096, 0x25f40 },  // 32 Options / Skin Info
        { 0x1c100, 0x0658, 0x1c75a, 0x25f42 },  // 33 Trainer Card
        { 0x1c800, 0x0a94, 0x1d296, 0x25f44 },  // 34 Mystery Gift
        { 0x1d300, 0x01ac, 0x1d4ae, 0x25f46 },  // 35 Dream World Stuff (Catalog)
        { 0x1d500, 0x03ec, 0x1d8ee, 0x25f48 },  // 36 Chatter
        { 0x1d900, 0x005c, 0x1d95e, 0x25f4a },  // 37 Adventure data
        { 0x1da00, 0x01e0, 0x1dbe2, 0x25f4c },  // 38 Trainer Card Records
        { 0x1dc00, 0x00a8, 0x1dcaa, 0x25f4e },  // 39 ???
        { 0x1dd00, 0x0460, 0x1e162, 0x25f50 },  // 40 Mail
        { 0x1e200, 0x1400, 0x1f602, 0x25f52 },  // 41 Overworld State
        { 0x1f700, 0x02a4, 0x1f9a6, 0x25f54 },  // 42 Musical
        { 0x1fa00, 0x00e0, 0x1fae2, 0x25f56 },  // 43 White Forest + Black City Data, Fused Reshiram/Zekrom Storage
        { 0x1fb00, 0x034c, 0x1fe4e, 0x25f58 },  // 44 IR
        { 0x1ff00, 0x04e0, 0x203e2, 0x25f5a },  // 45 EventWork
        { 0x20400, 0x00f8, 0x204fa, 0x25f5c },  // 46 GTS
        { 0x20500, 0x02fc, 0x207fe, 0x25f5e },  // 47 Regulation Tournament
        { 0x20800, 0x0094, 0x20896, 0x25f60 },  // 48 Gimmick
        { 0x20900, 0x035c, 0x20c5e, 0x25f62 },  // 49 Battle Box
        { 0x20d00, 0x01d4, 0x20ed6, 0x25f64 },  // 50 Daycare
        { 0x20f00, 0x01e0, 0x210e2, 0x25f66 },  // 51 Strength Boulder Status
        { 0x21100, 0x00f0, 0x211f2, 0x25f68 },  // 52 Misc (Badge Flags, Money, Trainer Sayings)
        { 0x21200, 0x01b4, 0x213b6, 0x25f6a },  // 53 Entralink (Level & Powers etc)
        { 0x21400, 0x04dc, 0x218de, 0x25f6c },  // 54 Pokedex
        { 0x21900, 0x0034, 0x21936, 0x25f6e },  // 55 Encount (Swarm and other overworld info - 2C - swarm, 2D - repel steps, 2E repel type)
        { 0x21a00, 0x003c, 0x21a3e, 0x25f70 },  // 56 Battle Subway Play Info
        { 0x21b00, 0x01ac, 0x21cae, 0x25f72 },  // 57 Battle Subway Score Info
        { 0x21d00, 0x0b90, 0x22892, 0x25f74 },  // 58 Battle Subway Wi-Fi Info
        { 0x22900, 0x00ac, 0x229ae, 0x25f76 },  // 59 Online Records
        { 0x22a00, 0x0850, 0x23252, 0x25f78 },  // 60 Entralink Forest pokémon data
        { 0x23300, 0x0284, 0x23586, 0x25f7a },  // 61 Answered Questions
        { 0x23600, 0x0010, 0x23612, 0x25f7c },  // 62 Unity Tower
        { 0x23700, 0x00a8, 0x237aa, 0x25f7e },  // 63 Battle Institute & PWT related data
        { 0x23800, 0x016c, 0x2396e, 0x25f80 },  // 64 ???
        { 0x23a00, 0x0080, 0x23a82, 0x25f82 },  // 65 ???
        { 0x23b00, 0x00fc, 0x23bfe, 0x25f84 },  // 66 Hollow/Rival Block
        { 0x23c00, 0x16a8, 0x252aa, 0x25f86 },  // 67 Join Avenue Block
        { 0x25300, 0x0498, 0x2579a, 0x25f88 },  // 68 Medal
        { 0x25800, 0x0060, 0x25862, 0x25f8a },  // 69 Key-related data
        { 0x25900, 0x00fc, 0x259fe, 0x25f8c },  // 70 Festa Missions
        { 0x25a00, 0x03e4, 0x25de6, 0x25f8e },  // 71 Pokestar Studios
        { 0x25e00, 0x00f0, 0x25ef2, 0x25f90 },  // 72 ???
        { 0x25f00, 0x0094, 0x25fa2, 0x25fa2 },  // 73 Checksum Block
    };
    inline constexpr size_t BLOCK_COUNT_5B2W2 = sizeof(BLOCKS_5B2W2) / sizeof(BlockEntryNDS);

    // Block indices the save layer reaches for, resolved by NAME out of the table above.
    inline constexpr size_t BLOCK_BOX_NAMES_5B2W2 = 0;
    inline constexpr size_t BLOCK_BOX_FIRST_5B2W2 = 1;
    inline constexpr size_t BLOCK_INVENTORY_5B2W2 = 25;
    inline constexpr size_t BLOCK_PARTY_5B2W2 = 26;
    inline constexpr size_t BLOCK_TRAINER_5B2W2 = 27;
    inline constexpr size_t BOX_BLOCK_COUNT_5B2W2 = 24;
}

#endif  // TRAINER_BLOCKS5B2W2_H
