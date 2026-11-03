#!/usr/bin/env python3
"""Generate include/Names/ReleasedHeldItemTable.h + src/Names/ReleasedHeldItemTable.cpp from PKHeX's
ItemRestrictions.ReleasedHeldItems_* lists: which held items a Pokemon may legitimately hold in each
entity context, for the legality checker's held-item check (PKHeX ItemVerifier).

This is NOT ItemPresence (gen_itempresence.py). That table answers "does this id exist as a holdable
item in the game", for the bank's sanitizer. This one answers PKHeX's legality question, which differs
in three ways:

  - it covers every context with held items, Gen 2 through Z-A, keyed the way PKHeX keys it:
    one list per ENTITY CONTEXT, so Diamond/Pearl, Platinum and HeartGold/SoulSilver share Gen 4's
    list (built from Platinum's, which HeartGold/SoulSilver reuse) and Sun/Moon shares Ultra Sun/Ultra
    Moon's;
  - each list has the items that were never distributed removed again (ItemStorage*.Unreleased, plus
    Sword/Shield's Dynamax Crystals and Brilliant Diamond/Shining Pearl's Underground treasures);
  - a context PKHeX gives no list (Let's Go, Legends: Arceus) permits no held item at all.

    ItemRestrictions.GetPermitList(max, allowed, disallow...): a bool[max + 1] with every allowed id
    set, then every disallowed id (or range) cleared.

Gen 2 and Gen 3 lists are in their own games' item id spaces, as PKHeX's PK2 and PK3 store them.

Regenerate with:  python tools/gen_helditems.py
Pulls the PKHeX sources it reads from GitHub on demand (tools/pkhex_source.py); PKHEX_LOCAL points it at
a checkout instead.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from gen_itempresence import Storage, strip_comments  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Names", "ReleasedHeldItemTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Names", "ReleasedHeldItemTable.cpp")

# One row per PKHeX ItemRestrictions.ReleasedHeldItems_* list:
#   (table key, Legal.cs max-id constant, GetAllHeld() class, [(class, span) to disallow],
#    disallowed (class, start const, end const) inclusive range or None, PKSE groups it answers for)
CONTEXTS = [
    ("GEN2", "MaxItemID_2", "ItemStorage2", [], None, ["GSC"]),
    ("GEN3", "MaxItemID_3_RS", "ItemStorage3RS", [("ItemStorage3RS", "Unreleased")], None, ["RSE", "FRLG"]),
    ("GEN4", "MaxItemID_4_HGSS", "ItemStorage4Pt", [("ItemStorage4", "Unreleased")], None, ["DP", "PT", "HGSS"]),
    ("GEN5", "MaxItemID_5_B2W2", "ItemStorage5", [("ItemStorage5", "Unreleased")], None, ["BW", "B2W2"]),
    ("GEN6", "MaxItemID_6_AO", "ItemStorage6AO", [("ItemStorage6XY", "Unreleased")], None, ["XY", "ORAS"]),
    ("GEN7", "MaxItemID_7_USUM", "ItemStorage7USUM", [("ItemStorage7SM", "Unreleased")], None, ["SM", "USUM"]),
    ("SWSH", "MaxItemID_8", "ItemStorage8SWSH", [("ItemStorage8SWSH", "Unreleased")], ("ItemStorage8SWSH", "DMAX_START", "DMAX_END"), ["SWSH"]),
    ("BDSP", "MaxItemID_8b", "ItemStorage8BDSP", [("ItemStorage8BDSP", "Unreleased"), ("ItemStorage8BDSP", "DisallowHeldTreasure")], None, ["BDSP"]),
    ("SV", "MaxItemID_9", "ItemStorage9SV", [("ItemStorage9SV", "Unreleased")], None, ["SV"]),
    ("ZA", "MaxItemID_9a", "ItemStorage9ZA", [("ItemStorage9ZA", "Unreleased")], None, ["ZA"]),
]
# Contexts PKHeX gives an empty list: nothing may be held.
NO_HELD_ITEMS = ["RBY", "GG", "PLA"]


class LinkedStorage(Storage):
    """A Storage that also resolves the spans a class takes from its base class or `using static`."""

    def __init__(self, cls):
        super().__init__(cls)
        with open(pkhex_path("Items/" + cls + ".cs"), encoding="utf-8") as fh:
            raw = fh.read()
        linked = re.findall(r"using static PKHeX\.Core\.(ItemStorage\w+);", raw)
        base = re.search(r"class\s+" + re.escape(cls) + r"\s*:\s*(\w+)", raw)
        if base and base.group(1).startswith("ItemStorage"):
            linked.append(base.group(1))
        self.linked = [Storage(name) for name in linked]

    def span(self, name):
        if name in self._cache:
            return self._cache[name]
        if re.search(r"ReadOnlySpan<ushort>\s+" + re.escape(name) + r"\s*=>", self.text):
            return super().span(name)
        for other in self.linked:
            if re.search(r"ReadOnlySpan<ushort>\s+" + re.escape(name) + r"\s*=>", other.text):
                self._cache[name] = other.span(name)
                return self._cache[name]
        raise SystemExit(f"{self.cls}: span {name} not found here or in {[o.cls for o in self.linked]}")


def legal_constants():
    """Resolve Legal.cs's `const int/ushort NAME = N;` and `NAME = OTHER;` declarations."""
    with open(pkhex_path("Legality/Legal.cs"), encoding="utf-8") as fh:
        text = strip_comments(fh.read())
    raw = dict(re.findall(r"const\s+\w+\s+(\w+)\s*=\s*([\w()]+)\s*;", text))

    def resolve(name):
        value = raw[name]
        return int(value) if value.isdigit() else resolve(value)
    return resolve


def main():
    resolve = legal_constants()
    tables = []
    for key, max_name, held_class, disallowed, disallowed_range, groups in CONTEXTS:
        maximum = resolve(max_name)
        permitted = [False] * (maximum + 1)
        for item_id in LinkedStorage(held_class).all_held():
            if item_id > maximum:
                raise SystemExit(f"{key}: held item {item_id} above {max_name} = {maximum}")
            permitted[item_id] = True
        for storage_class, span_name in disallowed:
            for item_id in Storage(storage_class).span(span_name):
                if item_id <= maximum:
                    permitted[item_id] = False
        if disallowed_range:
            storage = Storage(disallowed_range[0])
            for item_id in range(storage.const(disallowed_range[1]), storage.const(disallowed_range[2]) + 1):
                if item_id <= maximum:
                    permitted[item_id] = False
        permitted[0] = False
        tables.append((key, maximum, permitted, groups))
        print(f"  {key:5} {sum(permitted):5} released held items (max id {maximum})")

    header = '''/**
 * Auto-generated by tools/gen_helditems.py from PKHeX's ItemRestrictions.ReleasedHeldItems_* lists.
 * DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * Which held items a Pokemon may legitimately hold in a format (PKHeX ItemRestrictions.
 * IsHeldItemAllowed), for the legality checker. Gen 2 and Gen 3 ids are in their own games' id
 * spaces; Let's Go and Legends: Arceus permit no held item.
 */
#ifndef NAMES_RELEASEDHELDITEMTABLE_H
#define NAMES_RELEASEDHELDITEMTABLE_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Names
{
    /// True when `itemId` may be held in `group`'s format; item 0 (nothing held) always may.
    bool isHeldItemReleased(uint16_t itemId, Enums::GameVersion group);
}

#endif
'''
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)

    parts = ['/**\n * Auto-generated by tools/gen_helditems.py. DO NOT EDIT BY HAND -- rerun the generator.\n */\n\n',
             '#include "Names/ReleasedHeldItemTable.h"\n\n#include <cstddef>\n\nnamespace Names\n{\n    namespace\n    {\n']
    for key, maximum, permitted, _ in tables:
        width = maximum // 8 + 1
        bits = bytearray(width)
        for item_id, allowed in enumerate(permitted):
            if allowed:
                bits[item_id >> 3] |= 1 << (item_id & 7)
        parts.append(f"        constexpr uint16_t RELEASED_HELD_ITEM_{key}_MAX_ID = {maximum};\n")
        parts.append("        // clang-format off\n")
        parts.append(f"        constexpr uint8_t RELEASED_HELD_ITEM_{key}_BITS[{width}] = {{\n")
        for row_start in range(0, width, 16):
            parts.append("            " + " ".join("0x%02X," % value for value in bits[row_start:row_start + 16]) + "\n")
        parts.append("        };\n        // clang-format on\n\n")
    parts.append("        bool isBitSet(const uint8_t *bits, uint16_t maximumItemId, uint16_t itemId)\n        {\n")
    parts.append("            return itemId <= maximumItemId && ((bits[itemId >> 3] >> (itemId & 7)) & 1) != 0;\n        }\n    }\n\n")
    parts.append("    bool isHeldItemReleased(uint16_t itemId, Enums::GameVersion group)\n    {\n")
    parts.append("        if (itemId == 0)\n        {\n            return true;\n        }\n        switch (group)\n        {\n")
    for key, _, _, groups in tables:
        for group in groups:
            parts.append(f"        case Enums::GameVersion::{group}:\n")
        parts.append(f"            return isBitSet(RELEASED_HELD_ITEM_{key}_BITS, RELEASED_HELD_ITEM_{key}_MAX_ID, itemId);\n")
    for group in NO_HELD_ITEMS:
        parts.append(f"        case Enums::GameVersion::{group}:\n")
    parts.append("        default:\n            return false;\n        }\n    }\n}\n")
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(parts))
    print("Wrote", OUT_H)
    print("Wrote", OUT_CPP)


if __name__ == "__main__":
    main()
