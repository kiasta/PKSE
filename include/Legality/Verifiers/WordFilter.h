/**
 * The consoles' profanity filters, which the games apply to every name a player types (PKHeX
 * PKHeX.Core/Legality/Restrictions/WordFilter/). A nickname or trainer name the game's filter
 * would have refused cannot have been entered legitimately.
 *
 * The bad-word lists are generated (tools/gen_wordfilter.py -> WordFilterTable.cpp). Groups stand
 * for PKHeX's EntityContext, as in SpeciesName.h.
 */
#ifndef LEGALITY_VERIFIERS_WORDFILTER_H
#define LEGALITY_VERIFIERS_WORDFILTER_H

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"

namespace Legality
{
    /// Which console's list matched (PKHeX WordFilterType).
    enum class WordFilterType : uint8_t
    {
        None,
        Generation5,
        Nintendo3DS,
        NintendoSwitch,
    };

    struct WordFilterMatch
    {
        WordFilterType type = WordFilterType::None;
        /// The matching line of PKHeX's list.
        int patternIndex = -1;
        /// The matching entry's text, for the finding.
        std::u16string patternText;
    };

    /// PKHeX WordFilter.IsFiltered(message, current, original): `currentGroup` is the format the
    /// name is in now and `originalGroup` the earliest one it was entered in.
    bool isWordFiltered(const std::u16string &message, Enums::GameVersion currentGroup, Enums::GameVersion originalGroup, WordFilterMatch &match);

    /// The list's display name, as PKHeX's finding names it.
    const char *getWordFilterTypeName(WordFilterType type);

    /// .NET char.ToLowerInvariant / ToUpperInvariant (simple case mapping).
    char16_t toLowerInvariant(char16_t character);
    char16_t toUpperInvariant(char16_t character);

    /// .NET char.IsNumber: any decimal digit, letter number or other number.
    bool isNumberCharacter(char16_t character);
}

#endif
