// Port of PKHeX PKHeX.Core/Legality/Restrictions/WordFilter/ (WordFilter.cs, WordFilterNX.cs,
// WordFilter3DS.cs, WordFilter5.cs, TextNormalizer.cs), with PKHeX's default settings: the filter
// is on and looks back at every earlier console's list.
#include "Legality/Verifiers/WordFilter.h"

#include <cstddef>
#include <string_view>

#include "Legality/Verifiers/SpeciesName.h"
#include "Legality/Verifiers/WordFilterTable.h"

namespace Legality
{
    namespace
    {
        // TextNormalizer.cs
        constexpr char16_t HALF_WIDTH_DAKUTEN = u'ﾞ';
        constexpr char16_t HALF_WIDTH_HANDAKUTEN = u'ﾟ';
        constexpr char16_t HALF_WIDTH_KATAKANA_FIRST = u'ｦ';
        constexpr char16_t HALF_WIDTH_KATAKANA_LAST = u'ﾝ';
        constexpr char16_t IDEOGRAPHIC_SPACE = u'　';
        const std::u16string DAKUTEN_KANA = u"ｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾊﾋﾌﾍﾎ";
        const std::u16string HANDAKUTEN_KANA = u"ﾊﾋﾌﾍﾎ";
        const std::u16string FULL_WIDTH_KANA = u"ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン";
        const std::u16string SMALL_KANA = u"ァィゥェォッャュョヮ";

        constexpr uint16_t MAX_SPECIES_GENERATION_6 = 721;
        constexpr uint16_t MAX_SPECIES_ULTRA_SUN_MOON = 807;

        char16_t mapCharacter(const CaseMapping *mappings, size_t mappingCount, char16_t character)
        {
            size_t lowIndex = 0;
            size_t highIndex = mappingCount;
            while (lowIndex < highIndex)
            {
                const size_t middleIndex = (lowIndex + highIndex) / 2;
                if (mappings[middleIndex].from < character)
                {
                    lowIndex = middleIndex + 1;
                }
                else
                {
                    highIndex = middleIndex;
                }
            }
            if (lowIndex < mappingCount && mappings[lowIndex].from == character)
            {
                return mappings[lowIndex].to;
            }
            return character;
        }

        /// PKHeX TextNormalizer.Normalize: the folding the 3DS and Switch filters apply before matching.
        std::u16string normalizeForConsoleFilter(const std::u16string &message)
        {
            std::u16string normalized;
            normalized.reserve(message.size());
            for (size_t characterIndex = 0; characterIndex < message.size(); ++characterIndex)
            {
                char16_t character = message[characterIndex];
                if (character == u' ' || character == IDEOGRAPHIC_SPACE || character == HALF_WIDTH_DAKUTEN || character == HALF_WIDTH_HANDAKUTEN)
                {
                    continue;
                }
                uint16_t kanaOffset = 0;
                if (character >= HALF_WIDTH_KATAKANA_FIRST && character <= HALF_WIDTH_KATAKANA_LAST && characterIndex + 1 < message.size())
                {
                    const char16_t nextCharacter = message[characterIndex + 1];
                    if (nextCharacter == HALF_WIDTH_DAKUTEN && DAKUTEN_KANA.find(character) != std::u16string::npos)
                    {
                        kanaOffset = 1;
                    }
                    else if (nextCharacter == HALF_WIDTH_HANDAKUTEN && HANDAKUTEN_KANA.find(character) != std::u16string::npos)
                    {
                        kanaOffset = 2;
                    }
                    else if (nextCharacter == HALF_WIDTH_DAKUTEN && character == u'ｳ')
                    {
                        kanaOffset = u'ヴ' - u'ウ';
                    }
                }
                character = toLowerInvariant(character);
                if (character >= u'ぁ' && character <= u'ゖ')
                {
                    character = static_cast<char16_t>(character + 0x60);
                }
                else if ((character >= u'０' && character <= u'９') || (character >= u'ａ' && character <= u'ｚ'))
                {
                    character = static_cast<char16_t>(character - 0xFEE0);
                }
                else if (character >= HALF_WIDTH_KATAKANA_FIRST && character <= HALF_WIDTH_KATAKANA_LAST)
                {
                    character = static_cast<char16_t>(FULL_WIDTH_KANA[character - HALF_WIDTH_KATAKANA_FIRST] + kanaOffset);
                }
                if (character >= u'ァ' && character <= u'ヶ')
                {
                    if (SMALL_KANA.find(character) != std::u16string::npos)
                    {
                        character = static_cast<char16_t>(character + 1);
                    }
                    else if (character == u'ヵ')
                    {
                        character = u'カ';
                    }
                    else if (character == u'ヶ')
                    {
                        character = u'ケ';
                    }
                }
                normalized.push_back(character);
            }
            return normalized;
        }

        /// Where the expression atom starting at `atomIndex` ends.
        size_t getAtomEnd(std::u16string_view expression, size_t atomIndex)
        {
            if (expression[atomIndex] == u'\\' && atomIndex + 1 < expression.size())
            {
                return atomIndex + 2;
            }
            if (expression[atomIndex] == u'[')
            {
                const size_t closeIndex = expression.find(u']', atomIndex + 1);
                return closeIndex == std::u16string::npos ? expression.size() : closeIndex + 1;
            }
            return atomIndex + 1;
        }

        bool atomMatches(std::u16string_view expression, size_t atomIndex, size_t atomEnd, char16_t character)
        {
            const char16_t atomCharacter = expression[atomIndex];
            if (atomCharacter == u'.')
            {
                return character != u'\n';
            }
            if (atomCharacter == u'\\')
            {
                return character == expression[atomIndex + 1];
            }
            if (atomCharacter == u'[')
            {
                size_t memberIndex = atomIndex + 1;
                const bool isNegated = memberIndex < atomEnd && expression[memberIndex] == u'^';
                if (isNegated)
                {
                    ++memberIndex;
                }
                bool isMember = false;
                for (; memberIndex + 1 < atomEnd; ++memberIndex)
                {
                    if (expression[memberIndex] == character)
                    {
                        isMember = true;
                        break;
                    }
                }
                return isMember != isNegated;
            }
            return atomCharacter == character;
        }

        /// A backtracking match of the expression subset tools/gen_wordfilter.py admits: literals,
        /// `.`, classes, the escapes \. and \$, `*` and `?` on one atom, and a final `$`.
        bool matchHere(std::u16string_view expression, size_t expressionIndex, const std::u16string &text, size_t textIndex)
        {
            if (expressionIndex == expression.size())
            {
                return true;
            }
            if (expression[expressionIndex] == u'$' && expressionIndex + 1 == expression.size())
            {
                return textIndex == text.size();
            }
            const size_t atomEnd = getAtomEnd(expression, expressionIndex);
            const char16_t quantifier = atomEnd < expression.size() ? expression[atomEnd] : u'\0';
            if (quantifier == u'*')
            {
                size_t repeatEnd = textIndex;
                while (repeatEnd < text.size() && atomMatches(expression, expressionIndex, atomEnd, text[repeatEnd]))
                {
                    ++repeatEnd;
                }
                for (size_t tryEnd = repeatEnd + 1; tryEnd-- > textIndex;)
                {
                    if (matchHere(expression, atomEnd + 1, text, tryEnd))
                    {
                        return true;
                    }
                }
                return false;
            }
            if (quantifier == u'?')
            {
                if (textIndex < text.size() && atomMatches(expression, expressionIndex, atomEnd, text[textIndex]) && matchHere(expression, atomEnd + 1, text, textIndex + 1))
                {
                    return true;
                }
                return matchHere(expression, atomEnd + 1, text, textIndex);
            }
            return textIndex < text.size() && atomMatches(expression, expressionIndex, atomEnd, text[textIndex]) && matchHere(expression, atomEnd, text, textIndex + 1);
        }

        bool matchExpression(std::u16string_view expression, const std::u16string &text)
        {
            if (!expression.empty() && expression[0] == u'^')
            {
                return matchHere(expression, 1, text, 0);
            }
            for (size_t startIndex = 0; startIndex <= text.size(); ++startIndex)
            {
                if (matchHere(expression, 0, text, startIndex))
                {
                    return true;
                }
            }
            return false;
        }

        bool patternMatches(const WordFilterPattern &pattern, const std::u16string &normalized)
        {
            const std::u16string_view patternText(pattern.text);
            switch (pattern.kind)
            {
            case WordFilterPatternKind::Contains:
                return normalized.find(patternText) != std::u16string::npos;
            case WordFilterPatternKind::Equals:
                return normalized == patternText;
            case WordFilterPatternKind::StartsWith:
                return normalized.size() >= patternText.size() && normalized.compare(0, patternText.size(), patternText) == 0;
            case WordFilterPatternKind::EndsWith:
                return normalized.size() >= patternText.size() && normalized.compare(normalized.size() - patternText.size(), patternText.size(), patternText) == 0;
            case WordFilterPatternKind::Expression:
                return matchExpression(patternText, normalized);
            }
            return false;
        }

        /// PKHeX WordFilter.TryMatch.
        bool tryMatchPatterns(const std::u16string &message, const WordFilterPattern *patterns, size_t patternCount, WordFilterMatch &match)
        {
            const std::u16string normalized = normalizeForConsoleFilter(message);
            for (size_t entryIndex = 0; entryIndex < patternCount; ++entryIndex)
            {
                if (patternMatches(patterns[entryIndex], normalized))
                {
                    match.patternIndex = patterns[entryIndex].patternIndex;
                    match.patternText = patterns[entryIndex].text;
                    return true;
                }
            }
            return false;
        }

        /// PKHeX GameVersion.GetMaxSpeciesID of EntityContext.GetSingleGameVersion.
        uint16_t getContextMaxSpecies(Enums::GameVersion contextGroup)
        {
            switch (contextGroup)
            {
            case Enums::GameVersion::GG:
                return 809;
            case Enums::GameVersion::SWSH:
                return 898;
            case Enums::GameVersion::PLA:
                return 905;
            case Enums::GameVersion::BDSP:
                return 493;
            case Enums::GameVersion::SV:
                return 1025;
            case Enums::GameVersion::ZA:
                return 1000;
            default:
                break;
            }
            switch (getContextGeneration(contextGroup))
            {
            case 1:
                return 151;
            case 2:
                return 251;
            case 3:
                return 386;
            case 4:
                return 493;
            case 5:
                return 649;
            case 6:
                return 721;
            case 7:
                return MAX_SPECIES_ULTRA_SUN_MOON;
            default:
                return 0;
            }
        }

        /// PKHeX WordFilterNX.IsFiltered.
        bool isFilteredNintendoSwitch(const std::u16string &message, Enums::GameVersion originalGroup, WordFilterMatch &match)
        {
            uint16_t speciesId = 0;
            if (tryGetSpeciesAnyLanguageIgnoringCase(message, originalGroup, speciesId) && speciesId <= getContextMaxSpecies(originalGroup))
            {
                return false;
            }
            return tryMatchPatterns(message, WORD_FILTER_PATTERNS_SWITCH, WORD_FILTER_PATTERNS_SWITCH_COUNT, match);
        }

        /// PKHeX WordFilter3DS.IsFiltered: Gen 6 compares species names case-sensitively, Gen 7 does not.
        bool isFiltered3DS(const std::u16string &message, Enums::GameVersion originalGroup, WordFilterMatch &match)
        {
            uint16_t speciesId = 0;
            if (getContextGeneration(originalGroup) == 6)
            {
                if (tryGetSpeciesAnyLanguage(message, originalGroup, speciesId) && speciesId <= MAX_SPECIES_GENERATION_6)
                {
                    return false;
                }
            }
            else if (tryGetSpeciesAnyLanguageIgnoringCase(message, Enums::GameVersion::USUM, speciesId) && speciesId <= MAX_SPECIES_ULTRA_SUN_MOON)
            {
                return false;
            }
            return tryMatchPatterns(message, WORD_FILTER_PATTERNS_3DS, WORD_FILTER_PATTERNS_3DS_COUNT, match);
        }

        /// PKHeX WordFilter5.IsFiltered: a whole-word lookup after folding case and kana.
        bool isFilteredGeneration5(const std::u16string &message, WordFilterMatch &match)
        {
            std::u16string normalized = message;
            for (char16_t &character : normalized)
            {
                character = toUpperInvariant(character);
                if (character >= u'ァ' && character <= u'ヶ')
                {
                    character = static_cast<char16_t>(character - 0x60);
                }
                else if (character >= u'Ａ' && character <= u'Ｚ')
                {
                    character = static_cast<char16_t>(character - 0xFEE0);
                }
            }
            // PKHeX's dictionary keeps the last index of a repeated word.
            for (size_t wordIndex = WORD_FILTER_WORDS_GEN5_COUNT; wordIndex-- > 0;)
            {
                if (normalized == WORD_FILTER_WORDS_GEN5[wordIndex])
                {
                    match.patternIndex = static_cast<int>(wordIndex);
                    match.patternText = WORD_FILTER_WORDS_GEN5[wordIndex];
                    return true;
                }
            }
            return false;
        }

        /// .NET char.IsWhiteSpace.
        bool isWhiteSpaceCharacter(char16_t character)
        {
            return (character >= u'\t' && character <= u'\r') || character == u' ' || character == u'\u0085' || character == u' ' || character == u' ' || (character >= u' ' && character <= u' ') || character == u' ' || character == u' ' || character == u' ' || character == u' ' || character == IDEOGRAPHIC_SPACE;
        }
    }

    char16_t toLowerInvariant(char16_t character)
    {
        return mapCharacter(LOWER_CASE_MAPPINGS, LOWER_CASE_MAPPINGS_COUNT, character);
    }

    char16_t toUpperInvariant(char16_t character)
    {
        return mapCharacter(UPPER_CASE_MAPPINGS, UPPER_CASE_MAPPINGS_COUNT, character);
    }

    bool isNumberCharacter(char16_t character)
    {
        size_t lowIndex = 0;
        size_t highIndex = NUMBER_CHARACTER_RANGES_COUNT;
        while (lowIndex < highIndex)
        {
            const size_t middleIndex = (lowIndex + highIndex) / 2;
            if (NUMBER_CHARACTER_RANGES[middleIndex].last < character)
            {
                lowIndex = middleIndex + 1;
            }
            else
            {
                highIndex = middleIndex;
            }
        }
        return lowIndex < NUMBER_CHARACTER_RANGES_COUNT && NUMBER_CHARACTER_RANGES[lowIndex].first <= character;
    }

    const char *getWordFilterTypeName(WordFilterType type)
    {
        switch (type)
        {
        case WordFilterType::Generation5:
            return "Gen5";
        case WordFilterType::Nintendo3DS:
            return "Nintendo3DS";
        case WordFilterType::NintendoSwitch:
            return "NintendoSwitch";
        default:
            return "None";
        }
    }

    bool isWordFiltered(const std::u16string &message, Enums::GameVersion currentGroup, Enums::GameVersion originalGroup, WordFilterMatch &match)
    {
        static_cast<void>(currentGroup); // only the current-console-only setting reads it, and PKHeX's default looks back
        match = WordFilterMatch{};
        bool isWhiteSpace = true;
        for (const char16_t character : message)
        {
            if (!isWhiteSpaceCharacter(character))
            {
                isWhiteSpace = false;
                break;
            }
        }
        if (isWhiteSpace || message.size() <= 1)
        {
            return false;
        }

        // PKHeX WordFilter.IsFilteredLookBack: every name reached the Switch filter first.
        if (isFilteredNintendoSwitch(message, originalGroup, match))
        {
            match.type = WordFilterType::NintendoSwitch;
            return true;
        }
        if (getContextGeneration(originalGroup) >= 8 || originalGroup == Enums::GameVersion::GG)
        {
            return false;
        }
        if (isFiltered3DS(message, originalGroup, match))
        {
            match.type = WordFilterType::Nintendo3DS;
            return true;
        }
        if (getContextGeneration(originalGroup) == 5 && isFilteredGeneration5(message, match))
        {
            match.type = WordFilterType::Generation5;
            return true;
        }
        return false;
    }
}
