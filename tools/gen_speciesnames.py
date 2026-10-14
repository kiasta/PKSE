#!/usr/bin/env python3
"""Generate src/Names/SpeciesNames.cpp -- species id -> display name -- from PKHeX's
English species-name text resource.

Species id = array index, so PKHeX's flat one-name-per-line list maps directly
(line 1 = Bulbasaur). Names are taken verbatim, so the punctuation is the
game-canonical form and matches the other Names tables: Ho-Oh, Farfetch'd,
Mr. Mime, Type: Null, Flabebe, Nidoran-female/male, Tapu Koko, Iron Hands.

This replaces the older hand-written table, which had stripped every hyphen,
period, colon, apostrophe, accent, space and gender sign out of the 42 names that
have one ("HoOh", "Flabebe", "TypeNull", "IronHands"). Those sanitized spellings
reached the UI anywhere the species name is shown rather than the nickname -- the
box summary most visibly -- and were written into a Gen 3 nickname on downgrade.

Index 0 is PKSE's empty-slot label "None", NOT PKHeX's "Egg": species 0 means "no
Pokemon here" throughout PKSE, and the egg state is a flag on a real species.

Regenerate:  python tools/gen_speciesnames.py
Pulls the PKHeX text resource it reads from GitHub on demand (tools/pkhex_source.py);
no local PKHeX checkout required.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from name_languages import LANGUAGES, ENGLISH_INDEX, emit_tables  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "Names", "SpeciesNames.cpp")
SRC_TEMPLATE = "Resources/text/other/{lang}/text_Species_{lang}.txt"

EMPTY_SLOT = "None"   # index 0; PKHeX says "Egg" there


def load_entries(path):
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    if lines and lines[-1] == "":       # drop the trailing empty from the final newline
        lines = lines[:-1]
    if not lines:
        raise SystemExit("species text resource was empty")
    lines[0] = EMPTY_SLOT
    return lines


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    perLanguage = [load_entries(pkhex_path(SRC_TEMPLATE.format(lang=lang))) for lang in LANGUAGES]
    names = perLanguage[ENGLISH_INDEX]

    p = []
    p.append("// AUTO-GENERATED from PKHeX's species-name text (species id = array index).\n")
    p.append("// Source: PKHeX.Core/%s, one table per language.\n" % SRC_TEMPLATE)
    p.append("// Regenerate with tools/gen_speciesnames.py (fetches from GitHub; see tools/pkhex_source.py).\n")
    p.append("//\n")
    p.append("// Names are verbatim, so they carry the punctuation the games use -- hyphens (Ho-Oh),\n")
    p.append("// periods (Mr. Mime), a colon (Type: Null), apostrophes (Farfetch'd), an accent\n")
    p.append("// (Flabebe) and the gender signs (Nidoran). The UI font loads Noto Sans Symbols as a\n")
    p.append("// NanoVG fallback, which is what makes the gender signs drawable, and the console's\n")
    p.append("// shared fonts cover the Japanese, Korean and Chinese tables below.\n")
    p.append("//\n")
    p.append("// Index 0 is \"%s\" (PKSE's empty slot), where PKHeX has \"Egg\".\n" % EMPTY_SLOT)
    p.append("#include <cstdint>\n")
    p.append("#include <cstddef>\n")
    p.append("\n")
    p.append('#include "Names/NameLanguage.h"\n')
    p.append("\n")
    p.append("namespace Names {\n")
    emit_tables(p, "SPECIES_NAMES", perLanguage)
    p.append("    // Out of range returns \"Unknown\", which the legality checker tests for by value\n")
    p.append("    // as its unknown-species sentinel -- keep the spelling if this is ever reworked.\n")
    p.append("    //\n")
    p.append("    // THE LANGUAGE IS SOMETIMES THE RECORD'S RATHER THAN THE DISPLAY'S, which is what this\n")
    p.append("    // overload is for. A Pokemon that was never nicknamed stores its SPECIES name in the\n")
    p.append("    // nickname field, in its own language, and a format with no is-nicknamed flag -- Gen 3\n")
    p.append("    // has none -- can only say \"not nicknamed\" by holding exactly that string. So a\n")
    p.append("    // down-convert writes the name the record's own language calls it, whatever language\n")
    p.append("    // the person holding the console is reading in.\n")
    p.append("    const char* getSpeciesNameLocalized(uint16_t speciesId, size_t languageIndex) {\n")
    p.append("        constexpr size_t speciesNameCount = sizeof(SPECIES_NAMES_EN) / sizeof(SPECIES_NAMES_EN[0]);\n")
    p.append('        if (speciesId >= speciesNameCount) return "Unknown";\n')
    p.append("        const size_t tableLanguageIndex = languageIndex < LANGUAGE_COUNT ? languageIndex : LANGUAGE_INDEX_ENGLISH;\n")
    p.append("        return SPECIES_NAMES_BY_LANGUAGE[tableLanguageIndex][speciesId];\n")
    p.append("    }\n")
    p.append("\n")
    p.append("    const char* getSpeciesName(uint16_t speciesId) {\n")
    p.append("        return getSpeciesNameLocalized(speciesId, displayLanguageIndex());\n")
    p.append("    }\n")
    p.append("}\n")

    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(p))
    print("Wrote", OUT, "with", len(names), "entries x", len(LANGUAGES), "languages")


if __name__ == "__main__":
    main()
