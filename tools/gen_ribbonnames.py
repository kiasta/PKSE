#!/usr/bin/env python3
"""Generate src/Names/RibbonNamesLocalized.cpp -- ribbon names in the eight non-English languages.

Like the form tables, ENGLISH IS NOT GENERATED. PKSE's ribbon names are the ones the GAMES show,
which is not always what PKHeX's own UI labels say:

  * `Champion` and `Hoenn Champion`, where PKHeX disambiguates for its dropdown as
    `Champion (Gen3)` and `Hoenn Champion (ORAS)`. Bulbapedia confirms these are two separately
    named ribbons in-game, so no suffix belongs on either.
  * `Cool Ribbon Super` and its nineteen siblings, where PKHeX says `Cool Super`. The Gen 3 contest
    ribbons really are called "<Category> Ribbon <Rank>" in game.

So this supplies only the other languages, keyed by the ENGLISH STRING PKSE already produces, and
the runtime falls back to English whenever a row is missing. PKSE's own naming stays authoritative.

THE LOOKUP KEY IS A NAME, NOT AN INDEX, which means ambiguity has to be handled rather than hoped
away: several PKHeX keys collapse to the same English label once its `(G3)` / `(G4)` suffixes are
stripped (`RibbonG3Cool`, `RibbonG4Cool` and `RibbonCountG3Cool` all become `Cool`), and they do NOT
agree in Japanese, French, Italian or Korean. Any label that is ambiguous AND whose languages
disagree is dropped rather than guessed -- PKSE reaches those ribbons through the explicit aliases
below, never through the bare label.

Regenerate:  python tools/gen_ribbonnames.py
Pulls the PKHeX text resources from GitHub on demand (tools/pkhex_source.py); no local checkout.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from name_languages import LANGUAGES, ENGLISH_INDEX, IDENTIFIER_SUFFIX, escape  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "Names", "RibbonNamesLocalized.cpp")
SRC_TEMPLATE = "Resources/text/other/{lang}/text_Ribbons_{lang}.txt"

CONTEST_CATEGORIES = ["Cool", "Beauty", "Cute", "Smart", "Tough"]
CONTEST_RANKS = ["", "Super", "Hyper", "Master"]


def pkse_aliases():
    """PKSE's English spelling -> the PKHeX key that carries its translations.

    Only for the names where PKSE deliberately differs from PKHeX's label; everything else is
    matched on the label itself.
    """
    aliases = {
        # The games call these two simply "Champion Ribbon" and "Hoenn Champion Ribbon".
        "Champion": "RibbonChampionG3",
        "Hoenn Champion": "RibbonChampionG6Hoenn",
    }
    # "Beauty Master" is the ONE Gen 8 contest Master ribbon that needs an explicit alias. Three
    # PKHeX keys carry that exact English -- RibbonMasterBeauty (Gen 8), RibbonG3BeautyMaster and
    # RibbonG4BeautyMaster -- and they disagree in Japanese, French, Italian and Korean, so the
    # label path refuses to guess and would leave it untranslated. PKSE's entry is the Gen 8/9 one:
    # it sits in GEN89_RIBBONS and GG_RIBBONS, not in the Gen 3 block.
    #
    # Its four siblings need no alias because BDSP names those contests differently -- PKSE and
    # PKHeX both say "Coolness Master", "Cuteness Master", "Cleverness Master" and "Toughness
    # Master", which collide with nothing.
    aliases["Beauty Master"] = "RibbonMasterBeauty"

    # Gen 3 contest ribbons: PKSE composes "<Category> Ribbon[ <Rank>]", PKHeX labels them
    # "<Category>[ <Rank>]". Twenty entries, built rather than typed so they cannot drift.
    for category in CONTEST_CATEGORIES:
        for rank in CONTEST_RANKS:
            pkseName = "%s Ribbon%s" % (category, (" " + rank) if rank else "")
            aliases[pkseName] = "RibbonG3%s%s" % (category, rank)
    return aliases


def load_ribbons(path):
    """key -> label. The file is tab-separated: `RibbonChampionKalos\tKalos Champion`."""
    with open(path, "rb") as handle:
        raw = handle.read()
    # Decode by BOM, never by trying encodings in order -- see gen_itemnames.py for why.
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8-sig")
    entries = {}
    for line in text.replace("\r\n", "\n").split("\n"):
        if "\t" not in line:
            continue
        key, label = line.split("\t", 1)
        entries[key] = label
    if not entries:
        raise SystemExit("no ribbon entries in " + path)
    return entries


def strip_disambiguator(label):
    """Drop PKHeX's trailing `(G3)` / `(ORAS)` style suffix -- its own UI disambiguation."""
    return re.sub(r"\s*\([^)]*\)\s*$", "", label).strip()


def wrap_cells(cells, indent, prefix, suffix, width=112):
    """Emit `prefix{cells}suffix` wrapped to `width`, so a generated row stays readable.

    Size is not a constraint for this project (PKSE runs under title override), so the table is
    wrapped rather than packed -- a reader checking a translation should not have to scroll.
    """
    out = []
    line = indent + prefix
    for index, cell in enumerate(cells):
        piece = cell + ("," if index + 1 < len(cells) else "")
        if len(line) + len(piece) + 1 > width and line.strip() != prefix.strip():
            out.append(line.rstrip() + "\n")
            line = indent + " " * len(prefix)
        line += piece + " "
    out.append(line.rstrip() + suffix + "\n")
    return out


def main():
    perLanguage = [load_ribbons(pkhex_path(SRC_TEMPLATE.format(lang=lang))) for lang in LANGUAGES]
    english = perLanguage[ENGLISH_INDEX]
    aliases = pkse_aliases()

    # English label -> the keys that produce it, so ambiguity is detected rather than assumed away.
    keysByLabel = {}
    for key, label in english.items():
        keysByLabel.setdefault(strip_disambiguator(label), []).append(key)

    rows = {}      # PKSE English name -> [localized per language]
    ambiguous = 0

    def add(pkseName, key):
        names = []
        for languageIndex, table in enumerate(perLanguage):
            names.append(strip_disambiguator(table.get(key, "")))
        rows[pkseName] = names

    for pkseName, key in aliases.items():
        if key not in english:
            raise SystemExit("alias %r points at %r, which PKHeX does not define" % (pkseName, key))
        add(pkseName, key)

    for label, keys in keysByLabel.items():
        if label in rows:
            continue        # an alias already claimed this spelling
        if len(keys) > 1:
            # Same English, several keys. Safe only if every language agrees on the wording.
            variants = {tuple(strip_disambiguator(table.get(key, "")) for key in keys)
                        for table in perLanguage}
            distinct = [set(v) for v in zip(*variants)] if variants else []
            if any(len(s) > 1 for s in distinct):
                ambiguous += 1
                continue
        add(label, keys[0])

    lines = []
    lines.append("// AUTO-GENERATED ribbon names for the eight non-English languages.\n")
    lines.append("// Regenerate with tools/gen_ribbonnames.py (see tools/pkhex_source.py).\n")
    lines.append("// Source: PKHeX.Core/%s.\n" % SRC_TEMPLATE)
    lines.append("//\n")
    lines.append("// ENGLISH IS DELIBERATELY ABSENT: PKSE's ribbon names are the ones the GAMES show,\n")
    lines.append("// which is not always PKHeX's UI label -- \"Champion\" rather than \"Champion (Gen3)\",\n")
    lines.append("// \"Cool Ribbon Super\" rather than \"Cool Super\". Looked up BY the English string\n")
    lines.append("// RibbonNames.cpp produces; a miss falls back to that string unchanged.\n")
    lines.append("#include <cstddef>\n")
    lines.append("#include <cstring>\n")
    lines.append("\n")
    lines.append('#include "Names/NameLanguage.h"\n')
    lines.append('#include "Names/RibbonNames.h"\n')
    lines.append("\n")
    lines.append("namespace Names\n")
    lines.append("{\n")
    lines.append("    namespace\n")
    lines.append("    {\n")
    lines.append("        struct LocalizedRibbonName\n")
    lines.append("        {\n")
    lines.append("            const char *english;\n")
    lines.append("            const char *names[LANGUAGE_COUNT];\n")
    lines.append("        };\n")
    lines.append("\n")
    lines.append("        // Sorted by the English name so the lookup can binary-search.\n")
    lines.append("        const LocalizedRibbonName LOCALIZED_RIBBON_NAMES[] = {\n")
    for pkseName in sorted(rows):
        cells = []
        for languageIndex in range(len(LANGUAGES)):
            if languageIndex == ENGLISH_INDEX:
                cells.append("nullptr")   # English is RibbonNames.cpp's own wording
            else:
                cells.append('"%s"' % escape(rows[pkseName][languageIndex]))
        lines.extend(wrap_cells(cells, "            ",
                                '{"%s", {' % escape(pkseName), "}},"))
    lines.append("        };\n")
    lines.append("    }\n")
    lines.append("\n")
    lines.append("    const char *getRibbonNameLocalized(const char *englishName, size_t languageIndex)\n")
    lines.append("    {\n")
    lines.append("        if (englishName == nullptr || languageIndex >= LANGUAGE_COUNT)\n")
    lines.append("            return nullptr;\n")
    lines.append("        constexpr size_t count = sizeof(LOCALIZED_RIBBON_NAMES) / sizeof(LOCALIZED_RIBBON_NAMES[0]);\n")
    lines.append("        size_t low = 0, high = count;\n")
    lines.append("        while (low < high)\n")
    lines.append("        {\n")
    lines.append("            const size_t middle = low + (high - low) / 2;\n")
    lines.append("            const int order = std::strcmp(LOCALIZED_RIBBON_NAMES[middle].english, englishName);\n")
    lines.append("            if (order < 0)\n")
    lines.append("                low = middle + 1;\n")
    lines.append("            else if (order == 0)\n")
    lines.append("                return LOCALIZED_RIBBON_NAMES[middle].names[languageIndex];\n")
    lines.append("            else\n")
    lines.append("                high = middle;\n")
    lines.append("        }\n")
    lines.append("        return nullptr;\n")
    lines.append("    }\n")
    lines.append("}\n")

    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(lines))
    print("Wrote %s with %d names x %d languages (%d ambiguous labels skipped)"
          % (OUT, len(rows), len(LANGUAGES) - 1, ambiguous))


if __name__ == "__main__":
    main()
