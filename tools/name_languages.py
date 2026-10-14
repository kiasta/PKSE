#!/usr/bin/env python3
"""The language order the generated name tables use. MIRRORS include/Names/NameLanguage.h.

Every gen_*names.py emits its tables as an array of LANGUAGE_COUNT pointers in this order, and
Names::displayLanguageIndex() is what indexes them at runtime. The order deliberately is NOT
Enums::LanguageID's numbering -- that starts at 1, reserves 0 for "unset" and leaves an unused hole
at 6, none of which an array wants.

Keeping the order in one module rather than repeating the list in each generator is the same rule
the C++ side follows: one concept, one definition. A generator that hardcoded its own order would
produce a table that indexes correctly for the language it was tested in and silently returns
Korean for Spanish everywhere else.
"""

# PKHeX resource suffixes, in table order. Index 1 is English and is the fallback.
LANGUAGES = ["ja", "en", "fr", "it", "de", "es", "ko", "zh-Hans", "zh-Hant"]
ENGLISH_INDEX = 1

# C++ identifier suffix per language -- "zh-Hans" is not a valid identifier.
IDENTIFIER_SUFFIX = {
    "ja": "JA", "en": "EN", "fr": "FR", "it": "IT", "de": "DE",
    "es": "ES", "ko": "KO", "zh-Hans": "ZH_HANS", "zh-Hant": "ZH_HANT",
}


def resource_path(template):
    """Per-language PKHeX paths for a template containing '{lang}'.

    e.g. "Resources/text/other/{lang}/text_Species_{lang}.txt" -> one path per language.
    """
    return [template.format(lang=lang) for lang in LANGUAGES]


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def emit_tables(lines, base_name, per_language_entries, element_formatter=None):
    """Emit one `const char* const NAME_<LANG>[]` per language plus the dispatch array.

    `per_language_entries` is a list of lists, parallel to LANGUAGES. Every language must supply
    the same number of entries -- they are indexed by the same id, so a short table would read off
    the end of one language while working perfectly in another. That is asserted here rather than
    left to discover at runtime, and again as a static_assert in the emitted file.
    """
    if len(per_language_entries) != len(LANGUAGES):
        raise SystemExit("%s: expected %d language tables, got %d"
                         % (base_name, len(LANGUAGES), len(per_language_entries)))
    entryCount = len(per_language_entries[ENGLISH_INDEX])
    for languageIndex, entries in enumerate(per_language_entries):
        if len(entries) != entryCount:
            raise SystemExit("%s: %s has %d entries, English has %d -- the tables are indexed by "
                             "the same id and must be the same length"
                             % (base_name, LANGUAGES[languageIndex], len(entries), entryCount))

    formatter = element_formatter or (lambda value: '        "%s",\n' % escape(value))
    for languageIndex, entries in enumerate(per_language_entries):
        suffix = IDENTIFIER_SUFFIX[LANGUAGES[languageIndex]]
        lines.append("    static const char* const %s_%s[] = {\n" % (base_name, suffix))
        for entry in entries:
            lines.append(formatter(entry))
        lines.append("    };\n")
    lines.append("\n")
    lines.append("    /// Indexed by Names::displayLanguageIndex(); see Names/NameLanguage.h.\n")
    lines.append("    static const char* const* const %s_BY_LANGUAGE[] = {\n" % base_name)
    for lang in LANGUAGES:
        lines.append("        %s_%s,\n" % (base_name, IDENTIFIER_SUFFIX[lang]))
    lines.append("    };\n")
    lines.append('    static_assert(sizeof(%s_BY_LANGUAGE) / sizeof(%s_BY_LANGUAGE[0]) == LANGUAGE_COUNT, "%s_BY_LANGUAGE must carry one table per language");\n'
                 % (base_name, base_name, base_name))
    lines.append("\n")
    return entryCount
