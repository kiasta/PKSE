#ifndef TRAINER_INVENTORY8_BDSP_H
#define TRAINER_INVENTORY8_BDSP_H

#include <cstdint>
#include <vector>

#include "Trainer/Inventory.h"

namespace Trainer
{
    /**
     * Inventory8BDSP.h - Brilliant Diamond / Shining Pearl Item/Inventory Management
     *
     * Key Facts:
     * - Items are stored by ITEM ID as index (like Gen 9)
     * - Item at ID X is at offset (bag base) + (X * 0x10)
     * - Each item record is 16 bytes
     * - Record layout (PKHeX InventoryItem8b):
     *     Count      int32 @ 0x0
     *     IsNew      int32 @ 0x4
     *     IsFavorite int32 @ 0x8
     *     SortOrder  uint16 @ 0xC   (0xE alignment)
     * - Bag base offset (0x0563C) and record read/write are handled in Trainer8BDSP;
     *   this header only supplies the per-pouch names + legal item-id lists.
     * - Block size: 0xBB80 bytes (3000 item records)
     */

    // Block size for BDSP item storage (3000 records * 0x10)
    constexpr size_t ITEM_BLOCK_SIZE8BDSP = 0xBB80; // 48,000 bytes

    constexpr size_t ITEM_ENTRY_SIZE8BDSP = 0x10; // 16 bytes per item

    // Offset of the Count field (int32) within an item record
    constexpr size_t ITEM_COUNT_OFFSET8BDSP = 0x0;
    // "Seen" (int32) -- 0 means the bag shows the NEW marker (PKHeX InventoryItem8b: IsNew = value == 0)
    constexpr size_t ITEM_SEEN_OFFSET8BDSP = 0x4;
    // Bag position (u16), 1-based within the pouch; 0 = not in the bag, whatever the count says
    // (PKHeX InventoryItem8b.IsValidSaveSortNumberCount)
    constexpr size_t ITEM_SORT_ORDER_OFFSET8BDSP = 0xC;

    constexpr size_t MAX_ITEM_ID8BDSP = ITEM_BLOCK_SIZE8BDSP / ITEM_ENTRY_SIZE8BDSP; // 3000

    constexpr size_t POUCH_COUNT8BDSP = 8; // Number of pouches

    /**
     * Pouch list, in PKHeX ItemStorage8BDSP.ValidTypes order.
     * NOTE: unlike Gen 9, BDSP does not store a pouch id in the item record -
     * the pouch is derived from which legal list contains the item id. These
     * enum values are just the UI iteration / getPouchInfo index order.
     */
    // Order == the in-game bag tab order. getPouchInfo8BDSP is indexed by the enum value, and the
    // parser iterates 0..POUCH_COUNT mapping index -> enum -> getValidItemIds8BDSP, so enum order is
    // the display order. getValidItemIds8BDSP switches by enum NAME, so it needs no reordering.
    enum class PouchType8BDSP : uint32_t
    {
        Medicine = 0,
        Balls = 1,
        BattleItems = 2,
        Berries = 3,
        Items = 4, // "Other Items" -- the general pouch
        TMs = 5,
        Treasure = 6,
        KeyItems = 7,
    };

    /**
     * Valid item IDs for each pouch (from PKHeX ItemStorage8BDSP.GetLegal).
     * Values transcribed as plain decimal (PKHeX's leading-zero literals are
     * C# decimals, which would be octal in C++).
     */
    inline const std::vector<uint16_t> &getValidItemIds8BDSP(PouchType8BDSP pouch)
    {
        static const std::vector<uint16_t> items = {
            45, 46, 47, 48, 49, 50, 51, 52, 53, 72, 73, 74, 75, 76, 77, 78,
            79, 80, 81, 82, 83, 84, 85, 93, 94, 107, 108, 109, 110, 111, 112, 135,
            136, 213, 214, 215, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228,
            229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244,
            245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260,
            261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276,
            277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292,
            293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308,
            309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324,
            325, 326, 327, 537, 565, 566, 567, 568, 569, 570, 644, 645, 849,
            1231, 1232, 1233, 1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241, 1242, 1243, 1244,
            1245, 1246, 1247, 1248, 1249, 1250, 1251, 1606};
        static const std::vector<uint16_t> keyItems = {
            428, 431, 432, 433, 438, 439, 440, 443, 445, 446, 447, 448, 449, 450, 451, 452,
            453, 454, 455, 459, 460, 461, 462, 463, 464, 466, 467, 631, 632,
            1267, 1278, 1822};
        static const std::vector<uint16_t> tmHms = {
            328, 329, 330, 331, 332, 333, 334, 335, 336, 337,
            338, 339, 340, 341, 342, 343, 344, 345, 346, 347,
            348, 349, 350, 351, 352, 353, 354, 355, 356, 357,
            358, 359, 360, 361, 362, 363, 364, 365, 366, 367,
            368, 369, 370, 371, 372, 373, 374, 375, 376, 377,
            378, 379, 380, 381, 382, 383, 384, 385, 386, 387,
            388, 389, 390, 391, 392, 393, 394, 395, 396, 397,
            398, 399, 400, 401, 402, 403, 404, 405, 406, 407,
            408, 409, 410, 411, 412, 413, 414, 415, 416, 417,
            418, 419,
            420, 421, 422, 423, 424, 425, 426, 427};
        static const std::vector<uint16_t> medicine = {
            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
            38, 39, 40, 41, 42, 43, 44, 54};
        static const std::vector<uint16_t> berries = {
            149, 150, 151, 152, 153, 154, 155, 156, 157, 158,
            159, 160, 161, 162, 163, 164, 165, 166, 167, 168,
            169, 170, 171, 172, 173, 174, 175, 176, 177, 178,
            179, 180, 181, 182, 183, 184, 185, 186, 187, 188,
            189, 190, 191, 192, 193, 194, 195, 196, 197, 198,
            199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
            209, 210, 211, 212, 686};
        static const std::vector<uint16_t> balls = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            492, 493, 494, 495, 496, 497, 498, 499, 500,
            576,
            851};
        static const std::vector<uint16_t> battleItems = {
            55, 56, 57, 58, 59, 60, 61, 62, 63};
        static const std::vector<uint16_t> treasure = {
            86, 87, 88, 89, 90, 91, 92, 99, 100, 101, 102, 103, 104, 105, 106, 795, 796,
            1808, 1809, 1810, 1811, 1812, 1813, 1814, 1815, 1816, 1817, 1818, 1819, 1820, 1821};

        switch (pouch)
        {
        case PouchType8BDSP::Items:
            return items;
        case PouchType8BDSP::KeyItems:
            return keyItems;
        case PouchType8BDSP::TMs:
            return tmHms;
        case PouchType8BDSP::Medicine:
            return medicine;
        case PouchType8BDSP::Berries:
            return berries;
        case PouchType8BDSP::Balls:
            return balls;
        case PouchType8BDSP::BattleItems:
            return battleItems;
        case PouchType8BDSP::Treasure:
            return treasure;
        default:
        {
            static const std::vector<uint16_t> empty;
            return empty;
        }
        }
    }

    struct PouchInfo8BDSP
    {
        PouchType8BDSP type;
        const char *name;
    };

    inline const PouchInfo8BDSP &getPouchInfo8BDSP(PouchType8BDSP type)
    {
        static const PouchInfo8BDSP pouches[] = {
            {PouchType8BDSP::Medicine, "Medicines"},
            {PouchType8BDSP::Balls, "Poké Balls"},
            {PouchType8BDSP::BattleItems, "Battle Items"},
            {PouchType8BDSP::Berries, "Berries"},
            {PouchType8BDSP::Items, "Other Items"},
            {PouchType8BDSP::TMs, "TMs"},
            {PouchType8BDSP::Treasure, "Treasures"},
            {PouchType8BDSP::KeyItems, "Key Items"}};
        return pouches[static_cast<int>(type)];
    }
}

#endif
