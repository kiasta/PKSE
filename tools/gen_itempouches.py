#!/usr/bin/env python3
"""Generate include/Names/ItemPouches.h + src/Names/ItemPouches.cpp -- the list of
item ids that legally belong in each POUCH of each game.

#31 (gen_itempresence.py) answers "can a Pokemon HOLD this item". This answers the
different question the item editor needs: "which items may be ADDED to the pouch the
user is looking at" -- so a Master Ball can't be created in the medicine pocket.

Source of truth is the same PKHeX ItemStorage classes, but a different accessor:
GetItems(InventoryType) / GetLegal(InventoryType) rather than GetAllHeld(). The
mapping below pairs each PKSE pouch, IN ITS OWN ENUM ORDER, with the PKHeX span it
corresponds to -- PKSE's per-game pouch enums are not in a shared order, so this has
to be spelled out per game rather than derived.

Notes on the awkward corners, all verified against the PKHeX sources:
  * FRLG's storage class pulls General/Balls/Berry/Machine from ItemStorage3RS via
    `using static`; only Key is FRLG-specific. Hence the Class.Span syntax below.
  * FRLG's PC Items pouch holds the same set as the main Items pouch (PKHeX maps
    both InventoryType.Items and InventoryType.PCItems to General).
  * PLA's Stored pouch is the overflow box for the satchel, so same set as Regular.
  * S/V has no MegaStones pouch and Z-A has no BattleItems pouch. PKSE's 9SV/9LZA enums
    used to claim both (they were copies of each other) -- now corrected, so each
    game now carries its real pockets and every entry below maps to a real PKHeX span.
  * S/V's key items live in PKHeX's `Event` span, not a `Key` one.

It also writes isKeyItem(), for "Add All Items": a pouch named Key Items is not the
only place key items live. Gen 2's and Ruby/Sapphire's PC accepts them, Let's Go
keeps them in its general pouch, and Gen 1 has no key-items pouch at all.

Regenerate with:  python tools/gen_itempouches.py
Reuses gen_itempresence's PKHeX parser, which fetches from GitHub on demand
(tools/pkhex_source.py); no local PKHeX checkout required.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_itempresence import Storage  # noqa: E402  (same C# span parser)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Names", "ItemPouches.h")
OUT_CPP = os.path.join(ROOT, "src", "Names", "ItemPouches.cpp")

# game key -> (default ItemStorage class, GameVersion ids, [(PKSE pouch name, span or None)])
# The pouch list order MUST match that game's PouchType* enum in include/Trainer/.
GAMES = [
    # Gen 1 has exactly two pouches and ONE legal set behind both: ItemStorage1 exposes a single
    # `General` span and returns it for every InventoryType, because the PC in Gen 1 holds the same
    # items the bag does. The set is narrower than "has a name" -- 84-195 are unused slots, and 7/44
    # are named but unobtainable -- which is precisely why the picker needs this rather than a range.
    ("RBY", "ItemStorage1", ["RBY", "RD", "GN", "BU", "YW"], [
        ("Items",       "General"),
        ("PCItems",     "General"),
    ]),
    # Gen 2 has FIVE pouches and two key-item sets: Crystal has four key items Gold/Silver do
    # not, so GSC's KeyItems uses the Crystal superset -- the picker offering a Crystal-only key
    # item in a GS bag is a legality finding, offering NOTHING is a broken editor. Same reasoning
    # for PC Items, whose Crystal list is the superset.
    ("GSC", "ItemStorage2", ["GSC", "GD", "SI", "C"], [
        ("TMHM",        "Machine"),
        ("Items",       "General"),
        ("KeyItems",    "KeyCrystal"),
        ("Balls",       "Balls"),
        # PKHeX computes PCItemsC as General+Balls+Machine+KeyCrystal rather than spelling it
        # out, so it is composed here the same way.
        ("PCItems",     ["General", "Balls", "Machine", "KeyCrystal"]),
    ]),
    # Gen 4: eight pouches. Diamond/Pearl, Platinum and HeartGold/SoulSilver differ in the general
    # list (Pt added items), the key list (three different ones) and the ball list -- which is why
    # PKHeX has three storage classes and why these are three rows, not one.
    ("DP", "ItemStorage4DP", ["DP", "D", "P"], [
        ("Items",       "ItemStorage4.GeneralDP"),
        ("KeyItems",    "ItemStorage4.Key"),
        ("TMHM",        "ItemStorage4.Machine"),
        ("Mail",        "ItemStorage4.Mail"),
        ("Medicine",    "ItemStorage4.Medicine"),
        ("Berries",     "ItemStorage4.Berry"),
        ("Balls",       "ItemStorage4.BallsDPPt"),
        ("Battle",      "ItemStorage4.Battle"),
    ]),
    ("PT", "ItemStorage4Pt", ["PT", "Pt"], [
        ("Items",       "ItemStorage4.GeneralPt"),
        ("KeyItems",    "KeyPt"),
        ("TMHM",        "ItemStorage4.Machine"),
        ("Mail",        "ItemStorage4.Mail"),
        ("Medicine",    "ItemStorage4.Medicine"),
        ("Berries",     "ItemStorage4.Berry"),
        ("Balls",       "ItemStorage4.BallsDPPt"),
        ("Battle",      "ItemStorage4.Battle"),
    ]),
    ("HGSS", "ItemStorage4HGSS", ["HGSS", "HG", "SS"], [
        ("Items",       "ItemStorage4.GeneralPt"),
        ("KeyItems",    "KeyHGSS"),
        ("TMHM",        "ItemStorage4.Machine"),
        ("Mail",        "ItemStorage4.Mail"),
        ("Medicine",    "ItemStorage4.Medicine"),
        ("Berries",     "ItemStorage4.Berry"),
        ("Balls",       "BallsHGSS"),
        ("Battle",      "ItemStorage4.Battle"),
    ]),
    # Gen 5: five pouches -- balls and battle items fold back into Items. Only the key list
    # differs between BW and B2W2.
    ("BW", "ItemStorage5BW", ["BW", "B", "W"], [
        ("Items",       "ItemStorage5.General"),
        ("KeyItems",    "Key"),
        ("TMHM",        "ItemStorage5.Machine"),
        ("Medicine",    "ItemStorage5.Medicine"),
        ("Berries",     "ItemStorage5.Berry"),
    ]),
    ("B2W2", "ItemStorage5B2W2", ["B2W2", "B2", "W2"], [
        ("Items",       "ItemStorage5.General"),
        ("KeyItems",    "Key"),
        ("TMHM",        "ItemStorage5.Machine"),
        ("Medicine",    "ItemStorage5.Medicine"),
        ("Berries",     "ItemStorage5.Berry"),
    ]),
    # Gen 6: same five pouches. XY and ORAS have entirely separate lists (ORAS added items and
    # two HMs), so neither borrows from the other.
    ("XY", "ItemStorage6XY", ["XY", "X", "Y"], [
        ("Items",       "General"),
        ("KeyItems",    "Key"),
        ("TMHM",        "Machine"),
        ("Medicine",    "Medicine"),
        ("Berries",     "Berry"),
    ]),
    # ORAS pulls Berry from XY via `using static`; only General/Key/Machine/Medicine are its own.
    ("ORAS", "ItemStorage6AO", ["ORAS", "OR", "AS"], [
        ("Items",       "General"),
        ("KeyItems",    "Key"),
        ("TMHM",        "Machine"),
        ("Medicine",    "Medicine"),
        ("Berries",     "ItemStorage6XY.Berry"),
    ]),
    # Gen 7 adds a Z-Crystal pouch, and USUM a Roto/battle-item pouch on top. The pouch ORDER here
    # is the on-disk order in Inventory67.h, not the in-game bag order.
    ("SM", "ItemStorage7SM", ["SM", "SN", "MN"], [
        ("Items",       "General"),
        ("Medicine",    "Medicine"),
        ("TMHM",        "Machine"),
        ("Berries",     "Berry"),
        ("KeyItems",    "Key"),
        ("ZCrystals",   "ZCrystalKey"),
    ]),
    ("USUM", "ItemStorage7USUM", ["USUM", "US", "UM"], [
        ("Items",       "ItemStorage7SM.General"),
        ("Medicine",    "ItemStorage7SM.Medicine"),
        ("TMHM",        "ItemStorage7SM.Machine"),
        ("Berries",     "ItemStorage7SM.Berry"),
        ("KeyItems",    "Key"),
        ("ZCrystals",   "ZCrystalKey"),
        ("Battle",      "Roto"),
    ]),
    ("FRLG", "ItemStorage3RS", ["FRLG", "FR", "LG"], [
        ("Items",       "General"),
        ("KeyItems",    "ItemStorage3FRLG.Key"),   # FR/LG key items differ from R/S
        ("Balls",       "Balls"),
        ("TMHM",        "Machine"),
        ("Berries",     "Berry"),
        ("PCItems",     "General"),                # same set as Items
    ]),
    # Ruby/Sapphire/Emerald. PKSE has ONE Hoenn group, so where R/S and Emerald differ the
    # pouch takes the WIDER set -- narrowing would refuse an item the user legitimately has.
    #   * KeyItems comes from ItemStorage3E, whose list is a strict superset of R/S's: it is
    #     the R/S key items plus FR/LG's three and Emerald's two.
    #   * PCItems follows R/S rather than Emerald. R/S's PC accepts key items and Emerald's
    #     does not, so R/S's is the union; ItemPresence is what says whether a given id is
    #     legal, and this list only decides what the picker offers.
    ("RSE", "ItemStorage3RS", ["RSE", "RU", "SA", "EM"], [
        ("Items",       "General"),
        ("KeyItems",    "ItemStorage3E.Key"),
        ("Balls",       "Balls"),
        ("TMHM",        "Machine"),
        ("Berries",     "Berry"),
        ("PCItems",     ["General", "Key", "Berry", "Balls", "Machine"]),
    ]),
    ("GG", "ItemStorage7GG", ["GG", "GP", "GE"], [
        ("Medicine",    "Medicine"),
        ("TMs",         "Machine"),
        ("PowerUp",     "PowerUp"),
        ("Candy",       "Candy"),
        ("Balls",       "Catching"),
        ("Battle",      "Battle"),
        # Let's Go has no separate key-item pocket -- PKHeX's Items pouch holds regular AND key
        # items mixed, and PKSE displays it as "Other Items" despite the enum name.
        ("KeyItems",    ["General", "Key"]),
    ]),
    ("SWSH", "ItemStorage8SWSH", ["SWSH", "SW", "SH"], [
        ("Medicine",    "Medicine"),
        ("Balls",       "Balls"),
        ("Battle",      "Battle"),
        ("Berries",     "Berry"),
        ("Items",       "General"),
        ("TMs",         "MachineRecord"),
        ("Treasures",   "Treasure"),
        ("Ingredients", "Ingredients"),
        ("KeyItems",    "Key"),
    ]),
    ("PLA", "ItemStorage8LA", ["PLA"], [
        ("Regular",     "General"),
        ("KeyItems",    "Key"),
        ("Stored",      "General"),                # storage box = same set
        ("Recipes",     "Recipe"),
    ]),
    # Order == the in-game bag tab order (Inventory8BDSP.h enum). The "Items"/General pouch shows as
    # "Other Items". BDSP's add-item picker reads getPouchItems (this table); the parser reads the
    # header's getValidItemIds8BDSP -- both must map index i to the same pouch as the enum.
    ("BDSP", "ItemStorage8BDSP", ["BDSP", "BD", "SP"], [
        ("Medicine",    "Medicine"),
        ("Balls",       "Balls"),
        ("BattleItems", "Battle"),
        ("Berries",     "Berry"),
        ("Items",       "General"),
        ("TMs",         "Machine"),
        ("Treasure",    "Treasure"),
        ("KeyItems",    "Key"),
    ]),
    # Order == the in-game bag order (Inventory9SV.h enum). "TM Materials" (Candy->Material in PKHeX)
    # is a real S/V pocket; omitting it drops every TM-material item.
    ("SV", "ItemStorage9SV", ["SV", "SL", "VL"], [
        ("Medicine",    "Medicine"),               # "Medicines"
        ("Balls",       "Balls"),                  # "Poke Balls"
        ("BattleItems", "Battle"),                 # "Battle Items"
        ("Berries",     "Berry"),
        ("Other",       "Other"),                  # "Other Items"
        ("TMs",         "Machine"),
        ("Material",    "Material"),                # "TM Materials" (PKHeX Candy span)
        ("Treasure",    "Treasure"),               # "Treasures"
        ("Ingredients", "Picnic"),                 # "Picnic Items" (picnic ingredients + furniture)
        ("KeyItems",    "Event"),                  # S/V keeps key items in `Event`
    ]),
    # Order == the in-game bag order (Inventory9LZA.h enum / PKHeX ItemStorage9ZA "Display Order").
    ("ZA", "ItemStorage9ZA", ["ZA"], [
        ("Medicine",    "Medicine"),               # "Medicines"
        ("Balls",       "Balls"),                  # "Poke Balls"
        ("Berries",     "Berry"),
        ("Other",       "Other"),                  # "Other Items" (Z-A has no Battle Items pocket)
        ("TMs",         "TM"),
        ("MegaStones",  "MegaStones"),             # "Mega Stones"
        ("Treasure",    "Treasure"),               # "Treasures"
        ("KeyItems",    "Key"),
    ]),
]

# game key -> the spans holding that game's key items, as resolve() reads them. Mostly the game's
# own KeyItems pouch, but not always, because the KeyItems pouch is not always key items only.
KEY_ITEMS = {
    # PKHeX has no Gen 1 key-item list: ItemStorage1 is one `General` span for every pouch. These
    # come from the game itself instead: the ROM's KeyItemFlags table, identical in Red/Blue and
    # Yellow (pret/pokered and pret/pokeyellow, data/items/key_items.asm), plus HM01-HM05 (196-200),
    # which IsKeyItem_ treats as key items before it reads that table.
    "RBY":  [5, 6, 7, 8, 9, 21, 22, 23, 24, 25, 26, 27, 28, 31, 41, 42, 43, 44, 45, 48, 63, 64,
             69, 70, 71, 72, 73, 74, 76, 77, 78, 196, 197, 198, 199, 200],
    "GSC":  "KeyCrystal",
    "DP":   "ItemStorage4.Key",
    "PT":   "KeyPt",
    "HGSS": "KeyHGSS",
    "BW":   "Key",
    "B2W2": "Key",
    "XY":   "Key",
    "ORAS": "Key",
    "SM":   "Key",
    "USUM": "Key",
    "FRLG": "ItemStorage3FRLG.Key",
    "RSE":  "ItemStorage3E.Key",
    # Let's Go's KeyItems pouch is General + Key, so only the Key half is key items.
    "GG":   "Key",
    "SWSH": "Key",
    "PLA":  "Key",
    "BDSP": "Key",
    "SV":   "Event",
    "ZA":   "Key",
}

_cache = {}


def resolve(default_cls, spec):
    """`Span` on the game's own class, `OtherClass.Span` to borrow from another, or a LIST
    of either when one PKSE pouch corresponds to several PKHeX spans."""
    if spec is None:
        return []
    if isinstance(spec, int):
        return [spec]
    if isinstance(spec, (list, tuple)):
        out, seen = [], set()
        for part in spec:
            for i in resolve(default_cls, part):
                if i not in seen:
                    seen.add(i); out.append(i)
        return out
    cls, _, name = spec.rpartition(".")
    cls = cls or default_cls
    if cls not in _cache:
        _cache[cls] = Storage(cls)
    return [i for i in _cache[cls].span(name) if i > 0]


def main():
    data = {}
    for key, cls, _, pouches in GAMES:
        rows = [(pname, resolve(cls, spec)) for pname, spec in pouches]
        data[key] = rows
        total = sum(len(ids) for _, ids in rows)
        detail = "  ".join(f"{p}:{len(i)}" for p, i in rows)
        print(f"  {key:5} {total:5} ids over {len(rows)} pouches   {detail}")
    key_items = {}
    for key, cls, _, _ in GAMES:
        key_items[key] = sorted(resolve(cls, KEY_ITEMS[key]))
        print(f"  {key:5} {len(key_items[key]):5} key items")

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write('''/**
 * Auto-generated by tools/gen_itempouches.py from PKHeX's ItemStorage classes.
 * DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * Answers "what may be ADDED here", for the item editor's create flow -- distinct
 * from ItemPresence.h, which answers "what may a Pokemon HOLD". Pouch indices are
 * that game's own PouchType* enum order (include/Trainer/Inventory*.h).
 *
 * A pouch a game does not actually have (S/V MegaStones, Z-A BattleItems) returns
 * an empty span rather than a guess.
 */
#ifndef NAMES_ITEM_POUCHES_H
#define NAMES_ITEM_POUCHES_H

#include <cstdint>
#include <cstddef>
#include <span>

#include "Enums/GameVersion.h"

namespace Names {
    /// Legal item ids for `pouchIndex` of `group`, in that game's PouchType* order.
    /// Empty when the group is unknown, the index is out of range, or the game has
    /// no such pouch.
    std::span<const uint16_t> getPouchItems(Enums::GameVersion group, size_t pouchIndex);

    /// Number of pouches the table carries for `group` (0 if unknown).
    size_t getPouchCount(Enums::GameVersion group);

    /// True if `itemId` is one of `group`'s key items, in whichever pouch it sits. False for an
    /// unknown group as well as for an ordinary item.
    bool isKeyItem(Enums::GameVersion group, uint16_t itemId);
}

#endif  // NAMES_ITEM_POUCHES_H
''')

    p = []
    p.append('/**\n'
             ' * Auto-generated by tools/gen_itempouches.py from PKHeX ItemStorage\n'
             ' * GetItems()/GetLegal(). DO NOT EDIT BY HAND -- rerun the generator.\n'
             ' */\n\n')
    p.append('#include <algorithm>\n\n')
    p.append('#include "Names/ItemPouches.h"\n\n')
    p.append("namespace Names {\n\n")

    for key, _, _, _ in GAMES:
        for pname, ids in data[key]:
            if not ids:
                continue
            p.append(f"    static const uint16_t POUCH_{key}_{pname}[] = {{\n")
            for r in range(0, len(ids), 16):
                p.append("        " + " ".join("%d," % i for i in ids[r:r + 16]) + "\n")
            p.append("    };\n")
        p.append("\n")

    for key, _, _, _ in GAMES:
        p.append(f"    static const std::span<const uint16_t> POUCHES_{key}[] = {{\n")
        for pname, ids in data[key]:
            if ids:
                p.append(f"        POUCH_{key}_{pname},\n")
            else:
                p.append(f"        {{}},   // {pname}: not a pouch in this game\n")
        p.append("    };\n\n")

    # Sorted, so isKeyItem() can binary search.
    for key, _, _, _ in GAMES:
        ids = key_items[key]
        p.append(f"    static const uint16_t KEY_ITEMS_{key}[] = {{\n")
        for r in range(0, len(ids), 16):
            p.append("        " + " ".join("%d," % i for i in ids[r:r + 16]) + "\n")
        p.append("    };\n")
    p.append("\n")

    p.append("    static std::span<const uint16_t> keyItemsFor(Enums::GameVersion group) {\n")
    p.append("        switch (group) {\n")
    for key, _, versions, _ in GAMES:
        for v in versions:
            p.append(f"            case Enums::GameVersion::{v}:\n")
        p.append(f"                return KEY_ITEMS_{key};\n")
    p.append("            default:\n")
    p.append("                return {};\n")
    p.append("        }\n")
    p.append("    }\n\n")

    p.append("    static std::span<const std::span<const uint16_t>> tableFor(Enums::GameVersion g) {\n")
    p.append("        switch (g) {\n")
    for key, _, versions, _ in GAMES:
        for v in versions:
            p.append(f"            case Enums::GameVersion::{v}:\n")
        p.append(f"                return POUCHES_{key};\n")
    p.append("            default:\n")
    p.append("                return {};\n")
    p.append("        }\n")
    p.append("    }\n\n")

    p.append("    std::span<const uint16_t> getPouchItems(Enums::GameVersion group, size_t pouchIndex) {\n")
    p.append("        const auto pouchTable = tableFor(group);\n")
    p.append("        return pouchIndex < pouchTable.size() ? pouchTable[pouchIndex] : std::span<const uint16_t>{};\n")
    p.append("    }\n\n")
    p.append("    size_t getPouchCount(Enums::GameVersion group) {\n")
    p.append("        return tableFor(group).size();\n")
    p.append("    }\n\n")
    p.append("    bool isKeyItem(Enums::GameVersion group, uint16_t itemId) {\n")
    p.append("        const auto keyItems = keyItemsFor(group);\n")
    p.append("        return std::binary_search(keyItems.begin(), keyItems.end(), itemId);\n")
    p.append("    }\n")
    p.append("}\n")

    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(p))

    print("\nWrote", OUT_H)
    print("Wrote", OUT_CPP)


if __name__ == "__main__":
    main()
