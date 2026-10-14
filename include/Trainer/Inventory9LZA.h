#ifndef TRAINER_INVENTORY9_LZA_H
#define TRAINER_INVENTORY9_LZA_H

#include <cstdint>
#include <vector>

#include "Trainer/Inventory.h"

namespace Trainer
{
    /**
     * Inventory9.h - Generation 9 Item/Inventory Management
     *
     * Key Facts:
     * - Items are stored by ITEM ID as index
     * - Item at ID X is at offset (X * 0x10)
     * - Each item slot is 16 bytes
     * - Block size: 0xBB80 bytes
     */

    constexpr size_t ITEM_BLOCK_SIZE9_LZA = 0xBB80; // 47,872 bytes

    constexpr size_t ITEM_SIZE9_LZA = 0x10; // 16 bytes per item

    constexpr size_t MAX_ITEM_ID9_LZA = ITEM_BLOCK_SIZE9_LZA / ITEM_SIZE9_LZA; // 2992

    constexpr size_t POUCH_COUNT9_LZA = 8; // Number of pouches

    /**
     * Gen 9 Item Structure (16 bytes per item)
     * Offset 0-3: Pouch (uint32) - which pouch this item belongs to
     * Offset 4-7: Count (int32) - quantity of this item
     * Offset 8-11: Flags (uint32) - isNew, isFavorite, etc.
     * Offset 12-15: Padding (uint32) - reserved
     */
    struct InventoryItem9LZA : InventoryItem
    {
        uint32_t pouchId; // Which pouch this item belongs to
        uint32_t flags;   // Flags (isNew, isFavorite, etc.)

        // Decode from 16-byte block at given item ID
        static InventoryItem9LZA fromBytes(uint16_t itemId, const uint8_t *data)
        {
            InventoryItem9LZA item;

            // Bytes 0-3: Pouch ID
            item.pouchId = (data[0]) | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
            // Bytes 4-7: Count (signed int32)
            item.count = (data[4]) | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            // Bytes 8-11: Flags
            item.flags = (data[8]) | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

            item.itemId = itemId;
            item.count = static_cast<uint16_t>(item.count);
            // The game's own markers (PKHeX InventoryItem9.IsNew / IsFavorite): bit 0 is the red
            // "new" dot, bit 1 the favourite. The writer ORs isNew back into bit 0, so an item read
            // with the bit clear stays clear.
            item.isNew = (item.flags & 0x1) != 0;
            item.isFavorite = (item.flags & 0x2) != 0;

            return item;
        }
    };

    /**
     * Legends: Z-A's bag pockets, per PKHeX ItemStorage9ZA.GetLegal.
     *
     * Z-A has no Battle Items pocket -- PKHeX's GetLegal has no BattleItems case for it --
     * so index 2 is the "Other Items" pocket instead. Composition is display-only:
     * the item write is keyed on item id, not on pouch, so this cannot affect a save.
     *
     * The legal id list per pouch is generated: Names::getPouchItems(GameVersion::ZA, index).
     */
    // Order == the in-game bag tab order (PKHeX ItemStorage9ZA "Display Order"). The array below is
    // indexed by the enum value, so enum order, display order, and getPouchItems(ZA, index) must agree.
    enum class PouchType9LZA : uint32_t
    {
        Medicine = 0,
        Balls = 1,
        Berries = 2,
        Other = 3, // "Other Items" -- Z-A has no Battle Items pocket
        TMs = 4,
        MegaStones = 5,
        Treasure = 6,
        KeyItems = 7,
    };

    struct PouchInfo9LZA
    {
        PouchType9LZA type;
        const char *name;
    };

    inline const PouchInfo9LZA &getPouchInfo9LZA(PouchType9LZA type)
    {
        static const PouchInfo9LZA pouches[] = {
            {PouchType9LZA::Medicine, "Medicines"},
            {PouchType9LZA::Balls, "Poké Balls"},
            {PouchType9LZA::Berries, "Berries"},
            {PouchType9LZA::Other, "Other Items"},
            {PouchType9LZA::TMs, "TMs"},
            {PouchType9LZA::MegaStones, "Mega Stones"},
            {PouchType9LZA::Treasure, "Treasures"},
            {PouchType9LZA::KeyItems, "Key Items"}};
        return pouches[static_cast<int>(type)];
    }
}

#endif