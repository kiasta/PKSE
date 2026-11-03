#!/usr/bin/env python3
"""Generate include/Legality/ResultCode.h and src/Legality/ResultCodeText.cpp from PKHeX.

EVERY FINDING NAMES THE PKHeX CHECK IT IS. PKSE's legality layer is a port of PKHeX's, and the
parity harness (tools/harness/build-parity.sh: parity_diff.py over PKSE and tools/legalitydump output) compares the two per Pokemon
and per check. That comparison is only exact if a PKSE finding carries the same two facts a PKHeX
CheckResult does: which check group produced it (CheckIdentifier) and what it found
(LegalityCheckResultCode). Matching on message text would drift the first time either side reworded
a sentence.

THE NAMES ARE PKSE's, THE ORDER IS PKHeX's. PKHeX names are renamed under the coding standards
(no abbreviations, no argument suffixes): `EncTradeMatch` -> `EncounterTradeMatch`,
`MovePPTooHigh_01` -> `MovePPTooHigh`. The enumerators keep PKHeX's declaration order, and the
emitted file records each one's PKHeX name, which is also what the harness's JSON carries.

THE ENGLISH TEMPLATES ARE PKHeX's. Legality text has no in-game equivalent -- no game ever tells a
player its Pokemon is illegal -- so PKHeX's own wording is the source. A template keeps PKHeX's
`{0}` / `{1}` placeholders; the caller formats them.

Regenerate:  python tools/gen_legalitycodes.py
Pulls the PKHeX sources from GitHub on demand (tools/pkhex_source.py); PKHEX_LOCAL uses a checkout.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path, pkhex_ref  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_OUT = os.path.join(ROOT, "include", "Legality", "ResultCode.h")
TEXT_OUT = os.path.join(ROOT, "src", "Legality", "ResultCodeText.cpp")

IDENTIFIER_SOURCE = "Legality/Structures/CheckIdentifier.cs"
CODE_SOURCE = "Legality/Structures/LegalityCheckResultCode.cs"
TEMPLATE_SOURCE = "Legality/Localization/LegalityCheckResultCodeExtensions.cs"
ENGLISH_SOURCE = "Resources/localize/legality/legality_en.json"

# PKHeX's abbreviations, spelled out (coding standards section 3.2). Applied per PascalCase token.
TOKEN_RENAMES = {
    "Enc": "Encounter",
    "Evo": "Evolution",
    "Geo": "Geography",
    "Nick": "Nickname",
    "Arg": "Argument",
    "Char": "Character",
    "Distro": "Distribution",
    "Req": "Required",
    "Var": "Variable",
    "GEQ": "AtLeast",
    "LEQ": "AtMost",
    "Leq": "AtMost",
    "HTLEQ": "HandlerAtMost",
    "HT": "Handler",
    "EC": "EncryptionConstant",
    "JP": "Japan",
    "ID": "Id",
    "EVs": "EffortValues",
    "IVs": "IndividualValues",
    "GVs": "GanbaruValues",
    "AVs": "AwakeningValues",
}

# Codes the token rules cannot name: two that differ only by the argument suffix PKSE drops, the
# trainer-id group whose underscore is not an argument suffix, and an "ID" that is the word.
EXPLICIT_NAMES = {
    "OTLanguageShouldBe_0or1": "OTLanguageShouldBeEither",
    "OT_IDEqual": "OTIdEqual",
    "OT_IDs0": "OTIdsZero",
    "OT_SID0": "OTSIDZero",
    "OT_SID0Invalid": "OTSIDZeroInvalid",
    "OT_TID0": "OTTIDZero",
    "OT_IDInvalid": "OTIdInvalid",
    "BulkSharingTrainerIDs": "BulkSharingTrainerIds",
}


def split_tokens(name):
    return re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z]*|[0-9]+", name)


def pkse_name(pkhex_name):
    if pkhex_name in EXPLICIT_NAMES:
        return EXPLICIT_NAMES[pkhex_name]
    if "_" in pkhex_name and not re.fullmatch(r"[A-Za-z0-9]+_(?:[0-9]+F?|H[0-9]?|0or1)", pkhex_name):
        raise SystemExit("ERROR: %s has an underscore that is not an argument suffix; name it in EXPLICIT_NAMES" % pkhex_name)
    base = pkhex_name.split("_")[0]
    tokens = split_tokens(base)
    renamed = []
    for index, token in enumerate(tokens):
        # G3 / G4 / G6: PKHeX's shorthand for a generation
        if token == "G" and index + 1 < len(tokens) and tokens[index + 1].isdigit():
            renamed.append("Gen")
            continue
        renamed.append(TOKEN_RENAMES.get(token, token))
    return "".join(renamed)


def enum_body(text, enum_name):
    start = text.index("enum " + enum_name)
    open_brace = text.index("{", start)
    close_brace = text.index("}", open_brace)
    body = text[open_brace + 1:close_brace]
    body = re.sub(r"//.*", "", body)
    return body


def parse_enum(relpath, enum_name):
    with open(pkhex_path(relpath), encoding="utf-8") as handle:
        body = enum_body(handle.read(), enum_name)
    names = []
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        name = part.split("=")[0].strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise SystemExit("ERROR: unparsed %s entry %r" % (enum_name, part))
        names.append(name)
    return names


def parse_templates():
    with open(pkhex_path(TEMPLATE_SOURCE), encoding="utf-8") as handle:
        text = handle.read()
    pairs = {}
    for code, key in re.findall(r"^\s*([A-Za-z0-9_]+)\s*=>\s*localization\.([A-Za-z0-9_]+)\s*,", text, re.M):
        pairs[code] = key
    return pairs


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    identifiers = parse_enum(IDENTIFIER_SOURCE, "CheckIdentifier")
    all_codes = parse_enum(CODE_SOURCE, "LegalityCheckResultCode")
    # FirstWith... / FirstComplex are range markers aliased to the next code; MAX is a sentinel
    codes = [code for code in all_codes if not code.startswith("First") and code != "MAX"]
    if len(codes) != len(set(codes)):
        raise SystemExit("ERROR: duplicate PKHeX result codes")

    identifier_names = [TOKEN_RENAMES.get(name, name) for name in identifiers]
    code_names = [pkse_name(code) for code in codes]
    duplicates = sorted({name for name in code_names if code_names.count(name) > 1})
    if duplicates:
        raise SystemExit("ERROR: renamed result codes collide: %s" % ", ".join(duplicates))

    templates = parse_templates()
    with open(pkhex_path(ENGLISH_SOURCE), encoding="utf-8") as handle:
        english = json.load(handle)
    texts = []
    missing = []
    for code in codes:
        key = templates.get(code)
        if key is None or key not in english:
            missing.append(code)
            texts.append("")
        else:
            texts.append(english[key])
    # Codes with no template are the ones PKHeX humanizes in code (generated ribbon lists, egg
    # chains); they are listed so a new one is noticed rather than silently textless.
    stamp = "PKHeX %s" % pkhex_ref()

    header = []
    header.append("// AUTO-GENERATED by tools/gen_legalitycodes.py from %s. Do not edit." % stamp)
    header.append("// Sources: PKHeX.Core's %s and %s." % (IDENTIFIER_SOURCE, CODE_SOURCE))
    header.append("//")
    header.append("// CheckIdentifier is PKHeX's check group and ResultCode its LegalityCheckResultCode, in PKHeX's")
    header.append("// declaration order with PKSE's names. getResultCodeSourceName() returns PKHeX's own spelling,")
    header.append("// which is what the parity harness compares against.")
    header.append("#ifndef LEGALITY_RESULTCODE_H")
    header.append("#define LEGALITY_RESULTCODE_H")
    header.append("")
    header.append("#include <cstdint>")
    header.append("")
    header.append("namespace Legality")
    header.append("{")
    header.append("    enum class CheckIdentifier : uint8_t")
    header.append("    {")
    for name in identifier_names:
        header.append("        %s," % name)
    header.append("    };")
    header.append("")
    header.append("    constexpr int CHECK_IDENTIFIER_COUNT = %d;" % len(identifier_names))
    header.append("")
    header.append("    enum class ResultCode : uint16_t")
    header.append("    {")
    for name in code_names:
        header.append("        %s," % name)
    header.append("    };")
    header.append("")
    header.append("    constexpr int RESULT_CODE_COUNT = %d;" % len(code_names))
    header.append("")
    header.append("    /// PKHeX's English template for a code, with its {0} / {1} placeholders. Empty for the few codes")
    header.append("    /// PKHeX builds its text for in code (ribbon lists, egg breeding chains).")
    header.append("    const char *getResultCodeTemplate(ResultCode code);")
    header.append("")
    header.append("    /// PKHeX's own name for a code (`MovePPTooHigh_01`), as tools/legalitydump writes it.")
    header.append("    const char *getResultCodeSourceName(ResultCode code);")
    header.append("")
    header.append("    /// PKHeX's own name for a check group (`EC`, `EVs`), as tools/legalitydump writes it.")
    header.append("    const char *getCheckIdentifierSourceName(CheckIdentifier identifier);")
    header.append("}")
    header.append("")
    header.append("#endif")

    text = []
    text.append("// AUTO-GENERATED by tools/gen_legalitycodes.py from %s. Do not edit." % stamp)
    text.append("// Sources: PKHeX.Core's %s and %s." % (TEMPLATE_SOURCE, ENGLISH_SOURCE))
    text.append('#include "Legality/ResultCode.h"')
    text.append("")
    text.append("namespace Legality")
    text.append("{")
    text.append("    namespace")
    text.append("    {")
    text.append("        // clang-format off")
    text.append("        constexpr const char *RESULT_CODE_TEMPLATES[RESULT_CODE_COUNT] = {")
    for code, template in zip(codes, texts):
        text.append("            %s, // %s" % (cpp_string(template), code))
    text.append("        };")
    text.append("")
    text.append("        constexpr const char *RESULT_CODE_SOURCE_NAMES[RESULT_CODE_COUNT] = {")
    for code in codes:
        text.append("            %s," % cpp_string(code))
    text.append("        };")
    text.append("")
    text.append("        constexpr const char *CHECK_IDENTIFIER_SOURCE_NAMES[CHECK_IDENTIFIER_COUNT] = {")
    for name in identifiers:
        text.append("            %s," % cpp_string(name))
    text.append("        };")
    text.append("        // clang-format on")
    text.append("    }")
    text.append("")
    text.append("    const char *getResultCodeTemplate(ResultCode code)")
    text.append("    {")
    text.append("        const int codeIndex = static_cast<int>(code);")
    text.append("        if (codeIndex < 0 || codeIndex >= RESULT_CODE_COUNT) return \"\";")
    text.append("        return RESULT_CODE_TEMPLATES[codeIndex];")
    text.append("    }")
    text.append("")
    text.append("    const char *getResultCodeSourceName(ResultCode code)")
    text.append("    {")
    text.append("        const int codeIndex = static_cast<int>(code);")
    text.append("        if (codeIndex < 0 || codeIndex >= RESULT_CODE_COUNT) return \"\";")
    text.append("        return RESULT_CODE_SOURCE_NAMES[codeIndex];")
    text.append("    }")
    text.append("")
    text.append("    const char *getCheckIdentifierSourceName(CheckIdentifier identifier)")
    text.append("    {")
    text.append("        const int identifierIndex = static_cast<int>(identifier);")
    text.append("        if (identifierIndex < 0 || identifierIndex >= CHECK_IDENTIFIER_COUNT) return \"\";")
    text.append("        return CHECK_IDENTIFIER_SOURCE_NAMES[identifierIndex];")
    text.append("    }")
    text.append("}")

    with open(HEADER_OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(header) + "\n")
    with open(TEXT_OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(text) + "\n")
    print("Wrote %d check identifiers and %d result codes (%d without a template: %s)" % (len(identifier_names), len(code_names), len(missing), ", ".join(missing)))


if __name__ == "__main__":
    main()
