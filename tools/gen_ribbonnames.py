#!/usr/bin/env python3
"""Generate src/Names/RibbonNames.cpp (every format's ribbon table, with PKSE's English names) and
src/Names/RibbonNamesLocalized.cpp (those names in the eight non-English languages).

THE TABLES ARE PARSED FROM PKHeX's ENTITY CLASSES, not transcribed. Each ribbon is a property of
PK3 .. PA9, and its getter names where it lives in one of three shapes:

  * `(RIBn & (1 << bit))`, where `RIBn` is a private alias for one byte (or PK3's u32) of Data;
  * `FlagUtil.GetFlag(Data, 0xNN, bit)`, the Gen 8/9 formats;
  * `(byte)((RIB0 >> shift) & 7)`, PK3's contest ranks, and `Data[0xNN]`, a memory ribbon's count,
    whose has-flag is the class's HasContestMemoryRibbon / HasBattleMemoryRibbon.

Every Ribbon property of a class must parse, and formats PKSE gives one table (PK4 and PK5; PK7 and
PB7; every Gen 8/9 class) must parse to identical rows, so an upstream layout change fails here
rather than shipping a table that writes the wrong bit. The HOME block is checked against PKHeX's
RibbonIndex enum too, because AffixedRibbon stores a row's position in it. The maxima are PKHeX's
RibbonInfo.MaxCount.

ENGLISH IS NOT PKHeX's UI TEXT. PKSE's ribbon names are the ones the GAMES show, which is not always
what PKHeX's own UI labels say:

  * `Champion` and `Hoenn Champion`, where PKHeX disambiguates for its dropdown as
    `Champion (Gen3)` and `Hoenn Champion (ORAS)`. Bulbapedia confirms these are two separately
    named ribbons in-game, so no suffix belongs on either.
  * `Cool Ribbon Super` and its nineteen siblings, where PKHeX says `Cool Super`. The Gen 3 contest
    ribbons really are called "<Category> Ribbon <Rank>" in game.

So a ribbon's English name is PKHeX's label with its disambiguator stripped, except where
pkse_aliases() spells it the games' way. The localized table is keyed by that ENGLISH STRING, and
the runtime falls back to English whenever a row is missing; every name the English table can
produce is asserted to have a row before anything is written.

THE LOOKUP KEY IS A NAME, NOT AN INDEX, which means ambiguity has to be handled rather than hoped
away: several PKHeX keys collapse to the same English label once its `(G3)` / `(G4)` suffixes are
stripped (`RibbonG3Cool`, `RibbonG4Cool` and `RibbonCountG3Cool` all become `Cool`), and they do NOT
agree in Japanese, French, Italian or Korean. Any label that is ambiguous AND whose languages
disagree is dropped rather than guessed -- PKSE reaches those ribbons through the explicit aliases
below, never through the bare label.

Regenerate:  python tools/gen_ribbonnames.py
Pulls the PKHeX sources and text resources from GitHub on demand (tools/pkhex_source.py); no local
checkout.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from name_languages import LANGUAGES, ENGLISH_INDEX, IDENTIFIER_SUFFIX, escape  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "Names", "RibbonNamesLocalized.cpp")
ENGLISH_OUT = os.path.join(ROOT, "src", "Names", "RibbonNames.cpp")
SRC_TEMPLATE = "Resources/text/other/{lang}/text_Ribbons_{lang}.txt"
RIBBON_INFO_SOURCE = "Ribbons/RibbonInfo.cs"
RIBBON_INDEX_SOURCE = "Ribbons/RibbonIndex.cs"

# One C++ table per ribbon layout, and the PKHeX classes that store it. Every class listed for a
# table must parse to the same rows. The groups are the save formats whose entities are that class.
RIBBON_TABLES = [
    ("GEN3_RIBBONS", ["PKM/PK3.cs"], ["RSE", "FRLG"],
     "PK3 (Ruby/Sapphire/Emerald, FireRed/LeafGreen): one u32 at 0x4C. The five contest ribbons are\n"
     "        // 3-bit rank fields; bit 31 is FatefulEncounter, not a ribbon."),
    ("GEN45_RIBBONS", ["PKM/PK4.cs", "PKM/PK5.cs"], ["DP", "PT", "HGSS", "BW", "B2W2"],
     "PK4 and PK5. The Hoenn contest ribbons are separate FLAGS here, one per rank."),
    ("GEN6_RIBBONS", ["PKM/PK6.cs"], ["XY", "ORAS"],
     "PK6."),
    ("GEN7_RIBBONS", ["PKM/PK7.cs", "PKM/PB7.cs"], ["SM", "USUM", "GG"],
     "PK7 and PB7: PK6's block plus four Gen 7 ribbons."),
    ("GEN89_RIBBONS", ["PKM/Shared/G8PKM.cs", "PKM/PA8.cs", "PKM/PK9.cs", "PKM/PA9.cs"], ["SWSH", "BDSP", "PLA", "SV", "ZA"],
     "The HOME block of every Gen 8/9 format, in RibbonIndex order: AffixedRibbon stores a row's\n"
     "        // position here."),
]
HOME_TABLE = "GEN89_RIBBONS"

# The C++ around the tables. Hand-written, and emitted verbatim.
RIBBON_ROW_HELPERS = """        constexpr RibbonDefinition flag(uint8_t byteOffset, uint8_t bit, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::Flag, byteOffset, bit, 0, 1, englishName, spriteStem};
        }

        constexpr RibbonDefinition contestRank(uint8_t wordOffset, uint8_t rankShift, uint8_t maxCount, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::ContestRank, wordOffset, rankShift, 0, maxCount, englishName, spriteStem};
        }

        constexpr RibbonDefinition memoryCount(uint8_t flagByteOffset, uint8_t flagBit, uint8_t countByteOffset, uint8_t maxCount, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::MemoryCount, flagByteOffset, flagBit, countByteOffset, maxCount, englishName, spriteStem};
        }
"""

RIBBON_RUNTIME_HELPERS = """        static_assert(sizeof(GEN3_CONTEST_RANK_NAMES) == sizeof(GEN3_CONTEST_RANK_SPRITE_SUFFIXES), "one image suffix per contest rank name");
        constexpr uint32_t GEN3_CONTEST_RANK_MASK = 7;

        /// The active language's wording for a ribbon PKSE named in English. One place, so every
        /// name the tables produce goes through the same lookup.
        std::string localize(const std::string &englishName)
        {
            const size_t languageIndex = displayLanguageIndex();
            if (languageIndex == LANGUAGE_INDEX_ENGLISH) return englishName;
            const char *localized = getRibbonNameLocalized(englishName.c_str(), languageIndex);
            return (localized != nullptr && localized[0] != '\\0') ? std::string(localized) : englishName;
        }

        uint32_t readRibbonWord(const uint8_t *entityData, uint8_t wordOffset)
        {
            const uint8_t *wordBytes = entityData + wordOffset;
            return static_cast<uint32_t>(wordBytes[0]) | (static_cast<uint32_t>(wordBytes[1]) << 8) | (static_cast<uint32_t>(wordBytes[2]) << 16) | (static_cast<uint32_t>(wordBytes[3]) << 24);
        }

        void writeRibbonWord(uint8_t *entityData, uint8_t wordOffset, uint32_t ribbonWord)
        {
            uint8_t *wordBytes = entityData + wordOffset;
            for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
            {
                wordBytes[byteIndex] = static_cast<uint8_t>(ribbonWord >> (8 * byteIndex));
            }
        }

        void setFlag(uint8_t *entityData, uint8_t byteOffset, uint8_t bit, bool flagSet)
        {
            const uint8_t mask = static_cast<uint8_t>(1u << bit);
            entityData[byteOffset] = flagSet ? static_cast<uint8_t>(entityData[byteOffset] | mask) : static_cast<uint8_t>(entityData[byteOffset] & ~mask);
        }
"""

RIBBON_FUNCTIONS = """
    uint8_t getRibbonValue(const uint8_t *entityData, const RibbonDefinition &ribbon)
    {
        switch (ribbon.kind)
        {
        case RibbonKind::ContestRank:
        {
            const uint8_t contestRank = static_cast<uint8_t>((readRibbonWord(entityData, ribbon.byteOffset) >> ribbon.bit) & GEN3_CONTEST_RANK_MASK);
            // 5-7 are not ranks; the game treats them as never won
            return contestRank <= ribbon.maxCount ? contestRank : 0;
        }
        case RibbonKind::MemoryCount:
            return entityData[ribbon.countByteOffset];
        case RibbonKind::Flag:
        default:
            return static_cast<uint8_t>((entityData[ribbon.byteOffset] >> ribbon.bit) & 1u);
        }
    }

    void setRibbonValue(uint8_t *entityData, const RibbonDefinition &ribbon, uint8_t ribbonValue)
    {
        const uint8_t clampedValue = ribbonValue > ribbon.maxCount ? ribbon.maxCount : ribbonValue;
        switch (ribbon.kind)
        {
        case RibbonKind::ContestRank:
        {
            const uint32_t rankMask = GEN3_CONTEST_RANK_MASK << ribbon.bit;
            writeRibbonWord(entityData, ribbon.byteOffset, (readRibbonWord(entityData, ribbon.byteOffset) & ~rankMask) | (static_cast<uint32_t>(clampedValue) << ribbon.bit));
            break;
        }
        case RibbonKind::MemoryCount:
            entityData[ribbon.countByteOffset] = clampedValue;
            setFlag(entityData, ribbon.byteOffset, ribbon.bit, clampedValue != 0);
            break;
        case RibbonKind::Flag:
        default:
            setFlag(entityData, ribbon.byteOffset, ribbon.bit, clampedValue != 0);
            break;
        }
    }

    std::string getRibbonName(const RibbonDefinition &ribbon, uint8_t ribbonValue)
    {
        if (ribbon.kind == RibbonKind::ContestRank && ribbonValue <= ribbon.maxCount)
        {
            return localize(std::string(ribbon.englishName) + GEN3_CONTEST_RANK_NAMES[ribbonValue]);
        }
        return localize(ribbon.englishName);
    }

    std::string getRibbonSpriteStem(const RibbonDefinition &ribbon, uint8_t ribbonValue, Enums::GameVersion group)
    {
        if (ribbon.kind == RibbonKind::ContestRank && ribbonValue <= ribbon.maxCount)
        {
            return std::string(ribbon.spriteStem) + GEN3_CONTEST_RANK_SPRITE_SUFFIXES[ribbonValue];
        }
        if (ribbon.kind == RibbonKind::MemoryCount)
        {
            const Enums::GameVersion gameGroup = Enums::getGameGroup(group) != Enums::GameVersion::Invalid ? Enums::getGameGroup(group) : group;
            const bool isGeneration9 = Enums::getVersionGeneration(Enums::getGroupRepVersion(gameGroup)) >= 9;
            const bool isBattleMemory = std::string_view(ribbon.spriteStem) == "ribboncountmemorybattle";
            const uint8_t goldCount = (isGeneration9 && isBattleMemory) ? ribbon.maxCount - 1 : ribbon.maxCount;
            if (ribbonValue >= goldCount)
            {
                return std::string(ribbon.spriteStem) + "2";
            }
        }
        return ribbon.spriteStem;
    }

    bool isRibbonMark(const RibbonDefinition &ribbon)
    {
        const std::string_view englishName = ribbon.englishName;
        constexpr std::string_view markSuffix = " Mark";
        return englishName.size() > markSuffix.size() && englishName.substr(englishName.size() - markSuffix.size()) == markSuffix;
    }

    int getRibbonAffixIndex(const RibbonDefinition &ribbon)
    {
        // The HOME table is in RibbonIndex order (the generator checks it against PKHeX's enum), so
        // a row's position IS its RibbonIndex.
        const RibbonDefinition *homeRows = GEN89_RIBBONS;
        const size_t homeRowCount = sizeof(GEN89_RIBBONS) / sizeof(GEN89_RIBBONS[0]);
        if (&ribbon < homeRows || &ribbon >= homeRows + homeRowCount) return -1;
        return static_cast<int>(&ribbon - homeRows);
    }

    std::vector<std::string> getMonRibbons(const uint8_t *entityData, Enums::GameVersion group)
    {
        if (entityData == nullptr) return {};
        std::vector<std::string> ribbonNames;
        for (const RibbonDefinition &ribbon : getRibbonDefinitions(group))
        {
            const uint8_t ribbonValue = getRibbonValue(entityData, ribbon);
            if (ribbonValue != 0)
            {
                ribbonNames.push_back(getRibbonName(ribbon, ribbonValue));
            }
        }
        return ribbonNames;
    }
"""

CONTEST_CATEGORIES = ["Cool", "Beauty", "Cute", "Smart", "Tough"]
CONTEST_RANKS = ["", "Super", "Hyper", "Master"]
SINNOH_CONTEST_RANKS = ["", "Great", "Ultra", "Master"]
SINNOH_SHARED_RANKS = ["", "Master"]


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
            pkse_name = "%s Ribbon%s" % (category, (" " + rank) if rank else "")
            aliases[pkse_name] = "RibbonG3%s%s" % (category, rank)

    # Gen 4 contest ribbons, held beside the Hoenn ones in a PK4/PK5. The games name both sets'
    # Normal and Master ranks alike, so PKSE marks the Sinnoh pair " (Sinnoh)"; Great and Ultra are
    # Sinnoh's alone. The other languages have no such suffix to carry.
    for category in CONTEST_CATEGORIES:
        for rank in SINNOH_CONTEST_RANKS:
            suffix = (" " + rank) if rank else ""
            if rank in SINNOH_SHARED_RANKS:
                suffix += " (Sinnoh)"
            aliases["%s Ribbon%s" % (category, suffix)] = "RibbonG4%s%s" % (category, rank)
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


def read_source(relative_path):
    with open(pkhex_path(relative_path), encoding="utf-8-sig") as handle:
        return handle.read()


def parse_ribbon_class(relative_path):
    """Every ribbon one PKHeX entity class stores, as (property, kind, byteOffset, bit, countByteOffset).

    kind is "flag", "rank" or "count". A flag's (byteOffset, bit) is the bit itself, a rank's is the
    ribbon word and the field's shift, and a count's is its has-flag.
    """
    source = read_source(relative_path)
    # private byte RIB0 { get => Data[0x24]; ... }   /   private uint RIB0 { get => ReadUInt32LittleEndian(Data[0x4C..]); ... }
    aliases = {}
    for match in re.finditer(r"private (?:byte|uint) (RIB\w+)\s*\{\s*get => (?:Data\[(0x[0-9A-Fa-f]+)\]|ReadUInt32LittleEndian\(Data\[(0x[0-9A-Fa-f]+)\.\.\]\))", source):
        aliases[match.group(1)] = int(match.group(2) or match.group(3), 16)

    def bit_location(getter):
        """(byteOffset, bit) of a one-bit getter, or None."""
        aliased = re.match(r"\((RIB\w+) & \(1 << (\d+)\)\)", getter)
        if aliased:
            bit_position = int(aliased.group(2))
            return aliases[aliased.group(1)] + bit_position // 8, bit_position % 8
        direct = re.match(r"FlagUtil\.GetFlag\(Data, (0x[0-9A-Fa-f]+), (\d+)\)", getter)
        if direct:
            return int(direct.group(1), 16), int(direct.group(2))
        return None

    has_flags = {}
    for match in re.finditer(r"public (?:override )?bool Has(Contest|Battle)MemoryRibbon\s*\{\s*get => ([^;]+);", source):
        location = bit_location(match.group(2).strip())
        if location is None:
            raise SystemExit("%s: cannot read Has%sMemoryRibbon" % (relative_path, match.group(1)))
        has_flags[match.group(1)] = location

    ribbons = []
    for match in re.finditer(r"public (?:override )?(bool|byte) (Ribbon\w+)\s*\{\s*get => ([^;]+);", source):
        value_type, property_name, getter = match.group(1), match.group(2), match.group(3).strip()
        if value_type == "bool":
            location = bit_location(getter)
            if location is None:
                raise SystemExit("%s: cannot read %s from `%s`" % (relative_path, property_name, getter))
            ribbons.append((property_name, "flag", location[0], location[1], 0))
            continue
        rank = re.match(r"\(byte\)\(\((RIB\w+) >> (\d+)\) & 7\)", getter)
        if rank:
            ribbons.append((property_name, "rank", aliases[rank.group(1)], int(rank.group(2)), 0))
            continue
        count = re.match(r"Data\[(0x[0-9A-Fa-f]+)\]", getter)
        memory = re.match(r"RibbonCountMemory(Contest|Battle)$", property_name)
        if count and memory and memory.group(1) in has_flags:
            flag_offset, flag_bit = has_flags[memory.group(1)]
            ribbons.append((property_name, "count", flag_offset, flag_bit, int(count.group(1), 16)))
            continue
        raise SystemExit("%s: cannot read %s from `%s`" % (relative_path, property_name, getter))
    if not ribbons:
        raise SystemExit("no ribbon properties in " + relative_path)

    # Storage order: by the bit each row starts at. A rank field's shift counts from its word.
    ribbons.sort(key=lambda ribbon: ribbon[2] * 8 + ribbon[3])
    return ribbons


def parse_max_counts():
    """PKHeX's RibbonInfo.MaxCount: property name -> maximum, and the default (the contest ranks)."""
    source = read_source(RIBBON_INFO_SOURCE)
    maxima = {match.group(1): int(match.group(2)) for match in re.finditer(r"nameof\(\w+\.(\w+)\) => (\d+)", source)}
    default = re.search(r"_ => (\d+),", source)
    if not maxima or not default:
        raise SystemExit("cannot read MaxCount from " + RIBBON_INFO_SOURCE)
    return maxima, int(default.group(1))


def parse_ribbon_index():
    """PKHeX's RibbonIndex enum, as the property names it stands for, in order."""
    source = read_source(RIBBON_INDEX_SOURCE)
    body = re.search(r"enum RibbonIndex : byte\s*\{(.*?)\bMAX_COUNT\b", source, re.S)
    if not body:
        raise SystemExit("cannot read RibbonIndex from " + RIBBON_INDEX_SOURCE)
    members = re.findall(r"^\s*(\w+)\s*,", re.sub(r"//[^\n]*", "", body.group(1)), re.M)
    return ["Ribbon" + member for member in members]


def english_ribbon_names(english, aliases):
    """PKHeX property -> PKSE's English name: the alias where PKSE spells it the games' way, else
    PKHeX's label without its disambiguator. Gen 3 contest ranks are named by their base ribbon."""
    names_by_property = {key: pkse_name for pkse_name, key in aliases.items()}
    for key, label in english.items():
        names_by_property.setdefault(key, strip_disambiguator(label))
    return names_by_property


def sprite_stem(property_name):
    """PKHeX.Drawing.Misc names the image after the property in lower case. A Gen 3 rank field's
    images are the four flag ribbons it stands for, so its base is RibbonG3<Category>."""
    return re.sub(r"^RibbonCountG3", "RibbonG3", property_name).lower()


def build_ribbon_tables(english, aliases):
    """[(table name, groups, comment, rows)], each row (kind, byteOffset, bit, countByteOffset,
    maxCount, englishName, spriteStem, propertyName)."""
    maxima, rank_maximum = parse_max_counts()
    names_by_property = english_ribbon_names(english, aliases)
    tables = []
    for table_name, sources, groups, comment in RIBBON_TABLES:
        parsed = [parse_ribbon_class(source) for source in sources]
        for source, ribbons in zip(sources[1:], parsed[1:]):
            if ribbons != parsed[0]:
                raise SystemExit("%s and %s no longer store the same ribbons; %s needs its own table" % (sources[0], source, source))
        rows = []
        for property_name, kind, byte_offset, bit, count_byte_offset in parsed[0]:
            name_key = sprite_stem(property_name) if kind == "rank" else property_name.lower()
            matches = [name for key, name in names_by_property.items() if key.lower() == name_key]
            if len(matches) != 1:
                raise SystemExit("%s: no English name for %s" % (table_name, property_name))
            max_count = 1 if kind == "flag" else (maxima[property_name] if kind == "count" else rank_maximum)
            rows.append((kind, byte_offset, bit, count_byte_offset, max_count, matches[0], sprite_stem(property_name), property_name))
        if table_name == HOME_TABLE:
            ribbon_index = parse_ribbon_index()
            if [row[7] for row in rows] != ribbon_index:
                raise SystemExit("%s is not in RibbonIndex order; AffixedRibbon would name the wrong ribbon" % table_name)
        tables.append((table_name, groups, comment, rows))
    return tables


def gen3_rank_names():
    """One suffix per Gen 3 contest rank value: 0 (none) and 1 (Normal) name the base ribbon."""
    return [""] + [(" " + rank) if rank else "" for rank in CONTEST_RANKS]


def write_english(tables):
    lines = []
    lines.append("// AUTO-GENERATED ribbon tables, one per save format's ribbon layout, with PKSE's English names.\n")
    lines.append("// Regenerate with tools/gen_ribbonnames.py (see tools/pkhex_source.py). Sources: PKHeX.Core's\n")
    lines.append("// %s, %s and %s.\n" % (", ".join(sorted({source for _, sources, _, _ in RIBBON_TABLES for source in sources})), RIBBON_INFO_SOURCE, RIBBON_INDEX_SOURCE))
    lines.append("//\n")
    lines.append("// A flag is bit `bit` of the byte at `byteOffset` in the decrypted entity buffer, exactly like\n")
    lines.append("// PKHeX's FlagUtil.GetFlag(Data, byteOffset, bit). Names are the games' own, with the word\n")
    lines.append("// \"Ribbon\" dropped where the games append it; marks keep a trailing \" Mark\". The sprite stem is\n")
    lines.append("// the PKHeX property name in lower case, which is how PKHeX.Drawing.Misc names the image.\n")
    lines.append('#include "Names/RibbonNames.h"\n')
    lines.append('#include "Names/NameLanguage.h"\n')
    lines.append("\n")
    lines.append("#include <cstddef>\n")
    lines.append("#include <string_view>\n")
    lines.append("\n")
    lines.append("namespace Names\n")
    lines.append("{\n")
    lines.append("    namespace\n")
    lines.append("    {\n")
    lines.append(RIBBON_ROW_HELPERS)
    for table_name, groups, comment, rows in tables:
        lines.append("\n")
        lines.append("        // %s\n" % comment)
        lines.append("        const RibbonDefinition %s[] = {\n" % table_name)
        for kind, byte_offset, bit, count_byte_offset, max_count, english_name, stem, property_name in rows:
            if kind == "flag":
                lines.append('            flag(0x%02X, %d, "%s", "%s"),\n' % (byte_offset, bit, escape(english_name), stem))
            elif kind == "rank":
                lines.append('            contestRank(0x%02X, %d, %d, "%s", "%s"),\n' % (byte_offset, bit, max_count, escape(english_name), stem))
            else:
                lines.append('            memoryCount(0x%02X, %d, 0x%02X, %d, "%s", "%s"),\n' % (byte_offset, bit, count_byte_offset, max_count, escape(english_name), stem))
        lines.append("        };\n")
    lines.append("\n")
    lines.append("        // Indexed by a Gen 3 contest rank. The field stores only the HIGHEST rank reached.\n")
    lines.append("        const char *const GEN3_CONTEST_RANK_NAMES[] = {%s};\n" % ", ".join('"%s"' % name for name in gen3_rank_names()))
    lines.append("        const char *const GEN3_CONTEST_RANK_SPRITE_SUFFIXES[] = {%s};\n" % ", ".join('"%s"' % name.strip().lower() for name in gen3_rank_names()))
    lines.append(RIBBON_RUNTIME_HELPERS)
    lines.append("    }\n")
    lines.append("\n")
    lines.append("    std::span<const RibbonDefinition> getRibbonDefinitions(Enums::GameVersion group)\n")
    lines.append("    {\n")
    lines.append("        // A game id is taken to its group; a group id is not a game and getGameGroup() refuses it.\n")
    lines.append("        const Enums::GameVersion gameGroup = Enums::getGameGroup(group) != Enums::GameVersion::Invalid ? Enums::getGameGroup(group) : group;\n")
    lines.append("        switch (gameGroup)\n")
    lines.append("        {\n")
    for table_name, groups, _, _ in tables:
        for group_name in groups:
            lines.append("        case Enums::GameVersion::%s:\n" % group_name)
        lines.append("            return %s;\n" % table_name)
    lines.append("        default:\n")
    lines.append("            // Gen 1 and Gen 2 store no ribbons.\n")
    lines.append("            return {};\n")
    lines.append("        }\n")
    lines.append("    }\n")
    lines.append(RIBBON_FUNCTIONS)
    lines.append("}\n")
    with open(ENGLISH_OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(lines))
    print("Wrote %s with %s" % (ENGLISH_OUT, ", ".join("%s (%d)" % (table_name, len(rows)) for table_name, _, _, rows in tables)))


def english_names_produced(tables):
    """Every English name getRibbonName() can return, a rank ribbon's composed names included."""
    names = set()
    for _, _, _, rows in tables:
        for kind, _, _, _, max_count, english_name, _, _ in rows:
            if kind == "rank":
                names.update(english_name + suffix for suffix in gen3_rank_names()[1:max_count + 1])
            else:
                names.add(english_name)
    return names


def main():
    per_language = [load_ribbons(pkhex_path(SRC_TEMPLATE.format(lang=lang))) for lang in LANGUAGES]
    english = per_language[ENGLISH_INDEX]
    aliases = pkse_aliases()

    # English label -> the keys that produce it, so ambiguity is detected rather than assumed away.
    keys_by_label = {}
    for key, label in english.items():
        keys_by_label.setdefault(strip_disambiguator(label), []).append(key)

    rows = {}      # PKSE English name -> [localized per language]
    ambiguous = 0

    def add(pkse_name, key):
        names = []
        for language_index, table in enumerate(per_language):
            names.append(strip_disambiguator(table.get(key, "")))
        rows[pkse_name] = names

    for pkse_name, key in aliases.items():
        if key not in english:
            raise SystemExit("alias %r points at %r, which PKHeX does not define" % (pkse_name, key))
        add(pkse_name, key)

    for label, keys in keys_by_label.items():
        if label in rows:
            continue        # an alias already claimed this spelling
        if len(keys) > 1:
            # Same English, several keys. Safe only if every language agrees on the wording.
            variants = {tuple(strip_disambiguator(table.get(key, "")) for key in keys)
                        for table in per_language}
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
    for pkse_name in sorted(rows):
        cells = []
        for language_index in range(len(LANGUAGES)):
            if language_index == ENGLISH_INDEX:
                cells.append("nullptr")   # English is RibbonNames.cpp's own wording
            else:
                cells.append('"%s"' % escape(rows[pkse_name][language_index]))
        lines.extend(wrap_cells(cells, "            ",
                                '{"%s", {' % escape(pkse_name), "}},"))
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

    tables = build_ribbon_tables(english, aliases)
    untranslated = sorted(name for name in english_names_produced(tables) if name not in rows)
    if untranslated:
        raise SystemExit("ribbon names with no localized row: " + ", ".join(untranslated))

    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(lines))
    print("Wrote %s with %d names x %d languages (%d ambiguous labels skipped)"
          % (OUT, len(rows), len(LANGUAGES) - 1, ambiguous))
    write_english(tables)


if __name__ == "__main__":
    main()
