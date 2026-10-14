/**
 * From Black/White on, the games store text as plain little-endian UTF-16 in a fixed-width field,
 * so there is no character table -- but there are still two per-generation details, and both are
 * silent when wrong:
 *
 *  1. THE TERMINATOR DIFFERS. Gen 5 writes 0xFFFF (PKHeX StringConverter5.Terminator) and stops
 *     reading on either 0xFFFF or 0. Gen 6 and 7 write 0. Writing a 0 terminator into a Gen 5
 *     field leaves the field's tail as zeros where the game wrote 0xFFFF -- a round-trip diff on
 *     a save nobody edited.
 *  2. THE GENDER SIGNS ARE PRIVATE-USE. Gen 5 stores them half-width, U+246D and U+246E, and
 *     PKHeX normalises them to U+2642/U+2640 on the way in and back on the way out. A Nidoran♀
 *     read without that conversion shows a circled-number glyph. Gen 6/7 store the real ones.
 *
 * Field policy is the caller's: how many units the field holds, and whether the tail past the
 * terminator is cleared, are decided by whoever owns the offset.
 */
#ifndef UTILS_UTF16TEXT_H
#define UTILS_UTF16TEXT_H

#include <cstdint>
#include <string>

#include "Utils/HelperUtilities.h"

namespace Utils {

    inline constexpr uint16_t UTF16_TERM_G5  = 0xFFFF;
    inline constexpr uint16_t UTF16_TERM_G67 = 0x0000;

    inline constexpr char16_t HALF_WIDTH_MALE   = 0x246D;
    inline constexpr char16_t HALF_WIDTH_FEMALE = 0x246E;
    inline constexpr char16_t FULL_WIDTH_MALE   = 0x2642;   // the ♂ the name tables use
    inline constexpr char16_t FULL_WIDTH_FEMALE = 0x2640;   // ♀

    inline char16_t normalizeGenderSymbol(char16_t character) noexcept {
        if (character == HALF_WIDTH_MALE)   return FULL_WIDTH_MALE;
        if (character == HALF_WIDTH_FEMALE) return FULL_WIDTH_FEMALE;
        return character;
    }

    inline char16_t unNormalizeGenderSymbol(char16_t character) noexcept {
        if (character == FULL_WIDTH_MALE)   return HALF_WIDTH_MALE;
        if (character == FULL_WIDTH_FEMALE) return HALF_WIDTH_FEMALE;
        return character;
    }

    /// Reads at most `units` UTF-16 code units, stopping at 0 or 0xFFFF. Both terminate in every
    /// generation -- Gen 5 writes 0xFFFF but PKHeX stops on either, and a Gen 6 field that has
    /// never been written can hold 0xFFFF slack.
    inline std::u16string readUtf16Field(const uint8_t* bytes, size_t units, bool halfWidthGender) {
        std::u16string out;
        out.reserve(units);
        for (size_t index = 0; index < units; ++index) {
            const uint16_t value = readUInt16LittleEndian(bytes + index * 2);
            if (value == 0 || value == 0xFFFF) break;
            out.push_back(halfWidthGender ? normalizeGenderSymbol(static_cast<char16_t>(value)) : static_cast<char16_t>(value));
        }
        return out;
    }

    /// Writes `s` into a `units`-wide field, terminates it, and fills the remainder with `fill`.
    ///
    /// `fill` is what the games leave in the slack, and it is NOT always the terminator: Gen 5
    /// clears the field to zero and then writes ONE 0xFFFF, so the tail is zeros. Passing the
    /// terminator as the fill would stamp 0xFFFF over all of it.
    inline void writeUtf16Field(uint8_t* bytes, size_t units, const std::u16string& text, uint16_t terminator, bool halfWidthGender, uint16_t fill = 0) {
        size_t unitIndex = 0;
        for (; unitIndex < units && unitIndex < text.size(); ++unitIndex) {
            char16_t character = text[unitIndex];
            if (halfWidthGender) character = unNormalizeGenderSymbol(character);
            writeUInt16LittleEndian(bytes + unitIndex * 2, static_cast<uint16_t>(character));
        }
        if (unitIndex < units) writeUInt16LittleEndian(bytes + unitIndex++ * 2, terminator);
        for (; unitIndex < units; ++unitIndex) writeUInt16LittleEndian(bytes + unitIndex * 2, fill);
    }
}

#endif
