#ifndef TRAINER_INVENTORY9_SV_H
#define TRAINER_INVENTORY9_SV_H

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

    constexpr size_t ITEM_BLOCK_SIZE9_SV = 0xBB80; // 47,872 bytes

    constexpr size_t ITEM_SIZE9_SV = 0x10; // 16 bytes per item

    // Bits of an item record's flags word (PKHeX InventoryItem9). The game sets OBTAINED on every item it
    // holds, so a held item stored without it is a record only an editor writes.
    constexpr uint8_t ITEM_NEW_FLAG9_SV = 0x01;
    constexpr uint8_t ITEM_OBTAINED_FLAG9_SV = 0x04;

    constexpr size_t MAX_ITEM_ID9_SV = ITEM_BLOCK_SIZE9_SV / ITEM_SIZE9_SV; // 2992

    constexpr size_t POUCH_COUNT9_SV = 10; // Number of pouches (in-game bag order)

    /**
     * Gen 9 Item Structure (16 bytes per item)
     * Offset 0-3: Pouch (uint32) - which pouch this item belongs to
     * Offset 4-7: Count (int32) - quantity of this item
     * Offset 8-11: Flags (uint32) - isNew, isFavorite, etc.
     * Offset 12-15: Padding (uint32) - reserved
     */
    struct InventoryItem9SV : InventoryItem
    {
        uint32_t pouchId; // Which pouch this item belongs to
        uint32_t flags;   // Flags (isNew, isFavorite, etc.)

        // Decode from 16-byte block at given item ID
        static InventoryItem9SV fromBytes(uint16_t itemId, const uint8_t *data)
        {
            InventoryItem9SV item;

            // Bytes 0-3: Pouch ID
            item.pouchId = (data[0]) | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
            // Bytes 4-7: Count (signed int32)
            item.count = (data[4]) | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            // Bytes 8-11: Flags
            item.flags = (data[8]) | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

            item.itemId = itemId;
            item.count = static_cast<uint16_t>(item.count);

            return item;
        }
    };

    /**
     * Scarlet/Violet's bag pockets, per PKHeX ItemStorage9SV.GetLegal.
     *
     * S/V has no Mega Stones pocket and does have "Other Items" and picnic Ingredients, so
     * Legends: Z-A's pouch model is not reusable here. Composition is display-only -- the item
     * write is keyed on item id, not on pouch -- so getting it wrong cannot affect a save.
     *
     * The legal id list per pouch is generated: Names::getPouchItems(GameVersion::SV, index).
     */
    // Order == the in-game bag tab order. The array below is indexed by the enum value, so the enum
    // order, the display order, and the generated getPouchItems(SV, index) order must all agree.
    enum class PouchType9SV : uint32_t
    {
        Medicine = 0,
        Balls = 1,
        BattleItems = 2,
        Berries = 3,
        Other = 4, // "Other Items" pocket -- general items
        TMs = 5,
        Material = 6, // "TM Materials" (PKHeX Candy/Material span) -- was missing entirely
        Treasure = 7,
        Ingredients = 8, // picnic ingredients + furniture; shown as "Picnic Items"
        KeyItems = 9,    // PKHeX keeps S/V key items in its `Event` span
    };

    struct PouchInfo9SV
    {
        PouchType9SV type;
        const char *name;
    };

    inline const PouchInfo9SV &getPouchInfo9SV(PouchType9SV type)
    {
        static const PouchInfo9SV pouches[] = {
            {PouchType9SV::Medicine, "Medicines"},
            {PouchType9SV::Balls, "Poké Balls"},
            {PouchType9SV::BattleItems, "Battle Items"},
            {PouchType9SV::Berries, "Berries"},
            {PouchType9SV::Other, "Other Items"},
            {PouchType9SV::TMs, "TMs"},
            {PouchType9SV::Material, "TM Materials"},
            {PouchType9SV::Treasure, "Treasures"},
            {PouchType9SV::Ingredients, "Picnic Items"},
            {PouchType9SV::KeyItems, "Key Items"}};
        return pouches[static_cast<int>(type)];
    }
}

#endif