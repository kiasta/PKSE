#!/usr/bin/env python3
"""Generate include/Legality/Verifiers/WordFilterTable.h and src/Legality/Verifiers/WordFilterTable.cpp.

PKHeX's word filter (Legality/Restrictions/WordFilter/) checks player-given nicknames and trainer
names against the consoles' own bad-word lists: badwords_switch.txt and badwords_3ds.txt are one
.NET regular expression per line, and badwords_gen5.txt is a list of whole words. PKHeX lowercases
every regex invariantly before compiling it (WordFilter.LoadPatterns) and searches a normalized,
lowercased message with it.

PKSE HAS NO REGEX ENGINE, AND THE LISTS DO NOT NEED ONE. Every line uses a small subset: the
anchors ^ and $, `.`, `.*`, a character class ([..] or [^..]), `?` after one character, the escapes
\\. and \\$, and an unquantified, unnested (a|b) group. So the generator does the expensive part:
  - each group is expanded into its alternatives, so no emitted entry carries a group;
  - an alternative that is a plain string under `.*X.*`, `^X$`, `^X.*` or `.*X$` becomes a contains /
    equals / starts-with / ends-with entry, which is all but a couple of hundred of them;
  - what remains is emitted as an expression for the small matcher in WordFilter.cpp.
Every entry keeps the index of the PKHeX line it came from, because that index is what PKHeX
reports, and entries stay in PKHeX's line order so the first match is PKHeX's first match.

The same file carries the two pieces of Unicode data the filter needs and PKSE did not have:
.NET's invariant simple case mappings (char.ToLowerInvariant / ToUpperInvariant) and the code
points char.IsNumber accepts (categories Nd, Nl and No), both from Python's unicodedata.

Regenerate:  python tools/gen_wordfilter.py
Pulls the PKHeX resources from GitHub on demand (tools/pkhex_source.py); PKHEX_LOCAL uses a checkout.
"""
import itertools
import os
import re
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path, pkhex_ref  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_OUT = os.path.join(ROOT, "include", "Legality", "Verifiers", "WordFilterTable.h")
TABLE_OUT = os.path.join(ROOT, "src", "Legality", "Verifiers", "WordFilterTable.cpp")

SWITCH_SOURCE = "Resources/text/badwords/badwords_switch.txt"
NINTENDO_3DS_SOURCE = "Resources/text/badwords/badwords_3ds.txt"
GEN5_SOURCE = "Resources/text/badwords/badwords_gen5.txt"

KIND_CONTAINS = "Contains"
KIND_EQUALS = "Equals"
KIND_STARTS_WITH = "StartsWith"
KIND_ENDS_WITH = "EndsWith"
KIND_EXPRESSION = "Expression"

SPECIAL = set(".*?[]()|^$\\")


def lower_invariant(text):
    """Per code unit, as .NET's ToLowerInvariant: a mapping to more than one character is skipped."""
    result = []
    for character in text:
        lowered = character.lower()
        result.append(lowered if len(lowered) == 1 else character)
    return "".join(result)


def read_lines(relpath):
    with open(pkhex_path(relpath), encoding="utf-8-sig") as handle:
        content = handle.read()
    # PKHeX: one regex per line, lineCount = 1 + count('\n'), so no line is dropped.
    return content.replace("\r", "").split("\n")


def expand_groups(pattern):
    """Every alternative of the pattern's unnested (a|b) groups, in left-to-right order."""
    pieces = []
    position = 0
    while position < len(pattern):
        character = pattern[position]
        if character == "\\" and position + 1 < len(pattern):
            pieces.append([pattern[position:position + 2]])
            position += 2
        elif character == "[":
            close = pattern.index("]", position + 1)
            pieces.append([pattern[position:close + 1]])
            position = close + 1
        elif character == "(":
            close = pattern.index(")", position + 1)
            body = pattern[position + 1:close]
            if "(" in body:
                raise SystemExit("nested group in word filter pattern: %r" % pattern)
            if close + 1 < len(pattern) and pattern[close + 1] in "*?+{":
                raise SystemExit("quantified group in word filter pattern: %r" % pattern)
            pieces.append(body.split("|"))
            position = close + 1
        elif character == "|":
            raise SystemExit("top-level alternation in word filter pattern: %r" % pattern)
        else:
            pieces.append([character])
            position += 1
    return ["".join(choice) for choice in itertools.product(*pieces)]


def literal_of(body):
    """The plain string a pattern body stands for, or None when it uses any regex syntax."""
    result = []
    position = 0
    while position < len(body):
        character = body[position]
        if character == "\\":
            if position + 1 >= len(body) or body[position + 1] not in ".$":
                return None
            result.append(body[position + 1])
            position += 2
            continue
        if character in SPECIAL:
            return None
        result.append(character)
        position += 1
    return "".join(result)


def classify(alternative):
    """(kind, text) for one group-free alternative."""
    anchored_start = alternative.startswith("^")
    body = alternative[1:] if anchored_start else alternative
    leading_any = body.startswith(".*")
    if leading_any:
        body = body[2:]
    anchored_end = body.endswith("$") and not body.endswith("\\$")
    if anchored_end:
        body = body[:-1]
    trailing_any = body.endswith(".*") and not body.endswith("\\.*")
    if trailing_any:
        body = body[:-2]
    literal = literal_of(body)
    if literal is not None and literal != "":
        # A search is unanchored, so a missing ^ acts like a leading .* and a missing $ like a trailing one.
        open_start = leading_any or not anchored_start
        open_end = trailing_any or not anchored_end
        if open_start and open_end:
            return KIND_CONTAINS, literal
        if not open_start and not open_end:
            return KIND_EQUALS, literal
        if not open_start:
            return KIND_STARTS_WITH, literal
        return KIND_ENDS_WITH, literal
    validate_expression(alternative)
    return KIND_EXPRESSION, alternative


def validate_expression(expression):
    """Refuse anything the C++ matcher (WordFilter.cpp matchExpression) does not implement."""
    position = 0
    while position < len(expression):
        character = expression[position]
        if character == "^" and position == 0:
            position += 1
            continue
        if character == "$" and position == len(expression) - 1:
            position += 1
            continue
        if character == "\\":
            if position + 1 >= len(expression) or expression[position + 1] not in ".$":
                raise SystemExit("unsupported escape in %r" % expression)
            position += 2
        elif character == "[":
            close = expression.index("]", position + 1)
            if "-" in expression[position + 1:close].lstrip("^"):
                raise SystemExit("character range in %r" % expression)
            position = close + 1
        elif character in "()|+{}^$*?":
            raise SystemExit("unsupported syntax %r in %r" % (character, expression))
        else:
            position += 1
        if position < len(expression) and expression[position] in "*?":
            position += 1


def cpp_text(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    # Keep every character literal except control characters, which a pattern never holds.
    return 'u"%s"' % escaped


def build_entries(relpath):
    lines = read_lines(relpath)
    entries = []
    for line_index, line in enumerate(lines):
        pattern = lower_invariant(line)
        for alternative in expand_groups(pattern):
            kind, text = classify(alternative)
            entries.append((text, kind, line_index))
    return entries, len(lines)


def case_mappings(to_upper):
    pairs = []
    for code_point in range(0x10000):
        if 0xD800 <= code_point <= 0xDFFF:
            continue
        character = chr(code_point)
        mapped = character.upper() if to_upper else character.lower()
        if len(mapped) == 1 and mapped != character and ord(mapped) < 0x10000:
            pairs.append((code_point, ord(mapped)))
    return pairs


def number_ranges():
    ranges = []
    for code_point in range(0x10000):
        if 0xD800 <= code_point <= 0xDFFF:
            continue
        if unicodedata.category(chr(code_point)) in ("Nd", "Nl", "No"):
            if ranges and ranges[-1][1] == code_point - 1:
                ranges[-1][1] = code_point
            else:
                ranges.append([code_point, code_point])
    return ranges


def emit_patterns(parts, name, entries):
    parts.append("    const WordFilterPattern %s[] = {\n" % name)
    for text, kind, line_index in entries:
        parts.append("        {%s, WordFilterPatternKind::%s, %d},\n" % (cpp_text(text), kind, line_index))
    parts.append("    };\n")
    parts.append("    const size_t %s_COUNT = sizeof(%s) / sizeof(%s[0]);\n\n" % (name, name, name))


def main():
    switch_entries, switch_line_count = build_entries(SWITCH_SOURCE)
    nintendo_3ds_entries, nintendo_3ds_line_count = build_entries(NINTENDO_3DS_SOURCE)
    gen5_words = [line for line in read_lines(GEN5_SOURCE) if line != ""]
    lower_pairs = case_mappings(False)
    upper_pairs = case_mappings(True)
    numbers = number_ranges()
    stamp = "PKHeX %s, Unicode %s" % (pkhex_ref(), unicodedata.unidata_version)

    header = []
    header.append("// AUTO-GENERATED by tools/gen_wordfilter.py from %s. Do not edit.\n" % stamp)
    header.append("// Sources: PKHeX.Core/%s, %s and %s.\n" % (SWITCH_SOURCE, NINTENDO_3DS_SOURCE, GEN5_SOURCE))
    header.append("#ifndef LEGALITY_VERIFIERS_WORDFILTERTABLE_H\n")
    header.append("#define LEGALITY_VERIFIERS_WORDFILTERTABLE_H\n\n")
    header.append("#include <cstddef>\n")
    header.append("#include <cstdint>\n\n")
    header.append("namespace Legality\n{\n")
    header.append("    /// How an entry is matched against a normalized message. Expression entries hold a group-free\n")
    header.append("    /// pattern for WordFilter.cpp's matcher; the rest hold the plain text the pattern stands for.\n")
    header.append("    enum class WordFilterPatternKind : uint8_t\n    {\n")
    for kind in (KIND_CONTAINS, KIND_EQUALS, KIND_STARTS_WITH, KIND_ENDS_WITH, KIND_EXPRESSION):
        header.append("        %s,\n" % kind)
    header.append("    };\n\n")
    header.append("    struct WordFilterPattern\n    {\n")
    header.append("        const char16_t *text;\n")
    header.append("        WordFilterPatternKind kind;\n")
    header.append("        /// The PKHeX list line this entry came from, which is what PKHeX reports.\n")
    header.append("        uint16_t patternIndex;\n")
    header.append("    };\n\n")
    header.append("    struct CaseMapping\n    {\n        char16_t from;\n        char16_t to;\n    };\n\n")
    header.append("    struct CharacterRange\n    {\n        char16_t first;\n        char16_t last;\n    };\n\n")
    header.append("    /// PKHeX badwords_switch.txt, %d lines.\n" % switch_line_count)
    header.append("    extern const WordFilterPattern WORD_FILTER_PATTERNS_SWITCH[];\n")
    header.append("    extern const size_t WORD_FILTER_PATTERNS_SWITCH_COUNT;\n")
    header.append("    /// PKHeX badwords_3ds.txt, %d lines.\n" % nintendo_3ds_line_count)
    header.append("    extern const WordFilterPattern WORD_FILTER_PATTERNS_3DS[];\n")
    header.append("    extern const size_t WORD_FILTER_PATTERNS_3DS_COUNT;\n")
    header.append("    /// PKHeX badwords_gen5.txt: whole words, upper case.\n")
    header.append("    extern const char16_t *const WORD_FILTER_WORDS_GEN5[];\n")
    header.append("    extern const size_t WORD_FILTER_WORDS_GEN5_COUNT;\n\n")
    header.append("    /// .NET char.ToLowerInvariant / ToUpperInvariant, sorted by `from`; unlisted characters map to themselves.\n")
    header.append("    extern const CaseMapping LOWER_CASE_MAPPINGS[];\n")
    header.append("    extern const size_t LOWER_CASE_MAPPINGS_COUNT;\n")
    header.append("    extern const CaseMapping UPPER_CASE_MAPPINGS[];\n")
    header.append("    extern const size_t UPPER_CASE_MAPPINGS_COUNT;\n")
    header.append("    /// .NET char.IsNumber: Unicode categories Nd, Nl and No, sorted.\n")
    header.append("    extern const CharacterRange NUMBER_CHARACTER_RANGES[];\n")
    header.append("    extern const size_t NUMBER_CHARACTER_RANGES_COUNT;\n")
    header.append("}\n\n#endif\n")

    table = []
    table.append("// AUTO-GENERATED by tools/gen_wordfilter.py from %s. Do not edit.\n" % stamp)
    table.append("// The word lists are the consoles' own profanity filters, kept verbatim from PKHeX.\n")
    table.append("#include \"Legality/Verifiers/WordFilterTable.h\"\n\n")
    table.append("namespace Legality\n{\n")
    table.append("    // clang-format off\n")
    emit_patterns(table, "WORD_FILTER_PATTERNS_SWITCH", switch_entries)
    emit_patterns(table, "WORD_FILTER_PATTERNS_3DS", nintendo_3ds_entries)
    table.append("    const char16_t *const WORD_FILTER_WORDS_GEN5[] = {\n")
    for word in gen5_words:
        table.append("        %s,\n" % cpp_text(word))
    table.append("    };\n")
    table.append("    const size_t WORD_FILTER_WORDS_GEN5_COUNT = sizeof(WORD_FILTER_WORDS_GEN5) / sizeof(WORD_FILTER_WORDS_GEN5[0]);\n\n")
    for name, pairs in (("LOWER_CASE_MAPPINGS", lower_pairs), ("UPPER_CASE_MAPPINGS", upper_pairs)):
        table.append("    const CaseMapping %s[] = {\n" % name)
        for index in range(0, len(pairs), 8):
            chunk = pairs[index:index + 8]
            table.append("        " + " ".join("{0x%04X, 0x%04X}," % pair for pair in chunk) + "\n")
        table.append("    };\n")
        table.append("    const size_t %s_COUNT = sizeof(%s) / sizeof(%s[0]);\n\n" % (name, name, name))
    table.append("    const CharacterRange NUMBER_CHARACTER_RANGES[] = {\n")
    for index in range(0, len(numbers), 8):
        chunk = numbers[index:index + 8]
        table.append("        " + " ".join("{0x%04X, 0x%04X}," % (first, last) for first, last in chunk) + "\n")
    table.append("    };\n")
    table.append("    const size_t NUMBER_CHARACTER_RANGES_COUNT = sizeof(NUMBER_CHARACTER_RANGES) / sizeof(NUMBER_CHARACTER_RANGES[0]);\n")
    table.append("    // clang-format on\n")
    table.append("}\n")

    for path, parts in ((HEADER_OUT, header), (TABLE_OUT, table)):
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("".join(parts))
    kinds = {}
    for _, kind, _ in switch_entries + nintendo_3ds_entries:
        kinds[kind] = kinds.get(kind, 0) + 1
    print("Wrote", HEADER_OUT, "and", TABLE_OUT)
    print("  switch: %d lines -> %d entries; 3ds: %d lines -> %d entries; gen5: %d words" % (switch_line_count, len(switch_entries), nintendo_3ds_line_count, len(nintendo_3ds_entries), len(gen5_words)))
    print("  kinds:", kinds)
    print("  case mappings: %d lower, %d upper; number ranges: %d" % (len(lower_pairs), len(upper_pairs), len(numbers)))


if __name__ == "__main__":
    main()
