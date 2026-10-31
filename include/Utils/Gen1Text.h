#ifndef UTILS_GEN1TEXT_H
#define UTILS_GEN1TEXT_H
/**
 * Gen 1 stores names in a bespoke single-byte encoding, not ASCII: 'A' is 0x80, 'a' is 0xA0,
 * '0' is 0xF6, the terminator is 0x50 and a space is 0x7F. Every Gen 1 name field goes through
 * it -- the PK1 nickname and OT name inside the PokeList1 wrapper, and the nickname the
 * cross-generation converter writes on a transfer up.
 *
 * This is the sibling of Gen3Text.h and exists for the same reason: one table, added to once.
 * See BUGS Issue 33 for what four independent ad-hoc subsets of a GB charset cost the Gen 3
 * reader. The mapping is PKHeX's StringConverter1.TableEN / TableJP verbatim, transcribed by
 * machine rather than by hand -- 132 EN glyphs and 186 JP glyphs is far past the point where a
 * typo stays visible, and a wrong glyph does not fail, it just reads a name back wrong.
 *
 * THREE THINGS DIFFER FROM GEN 3, and each is a trap if you carry a Gen 3 habit across:
 *
 *  1. A glyphless byte TERMINATES; it is not skipped. Gen 3's reader skips a byte with no glyph
 *     and keeps going, because Gen 3's unassigned bytes sit past the terminator. Gen 1 has
 *     glyphless bytes scattered THROUGHOUT the range (0x00-0x4F is almost entirely empty in EN),
 *     so skipping them would run a name straight through its own padding and into whatever
 *     follows. PKHeX stops at the first table entry of 0; so do we.
 *  2. There are TWO character sets, not one, and the record length is what tells them apart --
 *     an international name field is 11 bytes, a Japanese one is 6 (GBPKML.StringLength*). The
 *     same byte means different things in each: 0x80 is 'A' in EN and katakana 'A' in JP. Pass
 *     the flag; do not default it.
 *  3. Byte 0x5D AT POSITION 0 is not a character -- it marks an in-game-trade OT, and the whole
 *     name is that one marker. PKHeX renders it '*'. A pokemon whose OT is 0x5D was received in a
 *     trade inside the game, which is a legality-relevant fact, not a name.
 *
 * Five glyphs are two-tile ligatures the Game Boy draws as a single character, and PKHeX maps
 * them to ASCII stand-ins so the mapping stays 1 byte <-> 1 char: '{' = Pk, '}' = Mn, '@' = Po,
 * '#' = Ke, '%' = e-acute (Box/Mail). Kept verbatim. Do NOT "fix" these into "Pk"/"Mn" strings:
 * that turns an invertible byte<->char table into a longest-match string problem, and a second
 * display-only mapping beside the storage one is exactly the shape of BUGS Issue 33.
 *
 * Note '\u2024' at 0xE8 -- a ONE DOT LEADER, not a full stop. That is the character in MR.MIME,
 * and the UI font's Noto Sans Symbols fallback draws it. Do not normalise it to '.' (0xF2).
 */
#include <cstdint>

namespace Utils {

    /// Gen 1 name fields are terminated -- and padded -- with 0x50. 0x00 also reads as a
    /// terminator (both map to 0 in the tables), which is why the loop tests the GLYPH, not the byte.
    inline constexpr uint8_t GEN1_TERMINATOR = 0x50;

    /// Marks an in-game-trade OT when it is the FIRST byte of a name field. Renders as '*'.
    inline constexpr uint8_t GEN1_TRADE_OT = 0x5D;

    /// Space. Note this is 0x7F, not 0x00 -- a zero byte ends the name.
    inline constexpr uint8_t GEN1_SPACE = 0x7F;

    /// Name-field widths, which are also what distinguishes the two character sets.
    inline constexpr int GEN1_NAME_LEN_INT = 11;
    inline constexpr int GEN1_NAME_LEN_JP  = 6;

    /// Gen 1 byte -> UTF-16 code unit, international. 0 marks a byte with no glyph (a terminator).
    inline constexpr char16_t GEN1_EN[256] = {
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 00-0F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 10-1F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 20-2F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 30-3F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 40-4F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    u'*', 0,    0,     // 50-5F
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     // 60-6F
        u'@', u'#', u'“', u'”', 0,    u'…', 0,    0,    0,    u'┌', u'─', u'┐', u'│', u'└', u'┘', u' ',  // 70-7F
        u'A', u'B', u'C', u'D', u'E', u'F', u'G', u'H', u'I', u'J', u'K', u'L', u'M', u'N', u'O', u'P',  // 80-8F
        u'Q', u'R', u'S', u'T', u'U', u'V', u'W', u'X', u'Y', u'Z', u'(', u')', u':', u';', u'[', u']',  // 90-9F
        u'a', u'b', u'c', u'd', u'e', u'f', u'g', u'h', u'i', u'j', u'k', u'l', u'm', u'n', u'o', u'p',  // A0-AF
        u'q', u'r', u's', u't', u'u', u'v', u'w', u'x', u'y', u'z', u'à', u'è', u'é', u'ù', u'À', u'Á',  // B0-BF
        u'Ä', u'Ö', u'Ü', u'ä', u'ö', u'ü', u'È', u'É', u'Ì', u'Í', u'Ñ', u'Ò', u'Ó', u'Ù', u'Ú', u'á',  // C0-CF
        u'ì', u'í', u'ñ', u'ò', u'ó', u'ú', u'º', 0,    0,    0,    0,    0,    0,    0,    u'←', u'\'', // D0-DF
        u'’', u'{', u'}', u'-', 0,    0,    u'?', u'!', u'․', u'&', u'%', u'→', u'▷', u'▶', u'▼', u'♂',  // E0-EF
        u'¥', u'×', u'.', u'/', u',', u'♀', u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7', u'8', u'9',  // F0-FF
    };

    /// Gen 1 byte -> UTF-16 code unit, Japanese. 0 marks a byte with no glyph (a terminator).
    inline constexpr char16_t GEN1_JP[256] = {
        0   , 0   , 0   , 0   , 0   , u'ガ', u'ギ', u'グ', u'ゲ', u'ゴ', u'ザ', u'ジ', u'ズ', u'ゼ', u'ゾ', u'ダ',  // 00-0F
        u'ヂ', u'ヅ', u'デ', u'ド', 0   , 0   , 0   , 0   , 0   , u'バ', u'ビ', u'ブ', u'ボ', 0   , 0   , 0   ,  // 10-1F
        0   , 0   , 0   , 0   , 0   , 0   , u'が', u'ぎ', u'ぐ', u'げ', u'ご', u'ざ', u'じ', u'ず', u'ぜ', u'ぞ',  // 20-2F
        u'だ', u'ぢ', u'づ', u'で', u'ど', 0   , 0   , 0   , 0   , 0   , u'ば', u'び', u'ぶ', u'ベ', u'ぼ', 0   ,  // 30-3F
        u'パ', u'ピ', u'プ', u'ポ', u'ぱ', u'ぴ', u'ぷ', u'ペ', u'ぽ', 0   , 0   , 0   , 0   , 0   , 0   , 0   ,  // 40-4F
        0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , u'*', 0   , 0   ,  // 50-5F
        0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , 0   , u'ぃ', u'ぅ',  // 60-6F
        u'「', u'」', u'『', u'』', u'・', u'⋯', u'ぁ', u'ぇ', u'ぉ', 0   , 0   , 0   , 0   , 0   , 0   , u'　',  // 70-7F
        u'ア', u'イ', u'ウ', u'エ', u'オ', u'カ', u'キ', u'ク', u'ケ', u'コ', u'サ', u'シ', u'ス', u'セ', u'ソ', u'タ',  // 80-8F
        u'チ', u'ツ', u'テ', u'ト', u'ナ', u'ニ', u'ヌ', u'ネ', u'ノ', u'ハ', u'ヒ', u'フ', u'ホ', u'マ', u'ミ', u'ム',  // 90-9F
        u'メ', u'モ', u'ヤ', u'ユ', u'ヨ', u'ラ', u'ル', u'レ', u'ロ', u'ワ', u'ヲ', u'ン', u'ッ', u'ャ', u'ュ', u'ョ',  // A0-AF
        u'ィ', u'あ', u'い', u'う', u'え', u'お', u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ',  // B0-BF
        u'た', u'ち', u'つ', u'て', u'と', u'な', u'に', u'ぬ', u'ね', u'の', u'は', u'ひ', u'ふ', u'ヘ', u'ほ', u'ま',  // C0-CF
        u'み', u'む', u'め', u'も', u'や', u'ゆ', u'よ', u'ら', u'リ', u'る', u'れ', u'ろ', u'わ', u'を', u'ん', u'っ',  // D0-DF
        u'ゃ', u'ゅ', u'ょ', u'ー', u'ﾟ', u'ﾞ', u'？', u'！', u'。', u'ァ', u'ゥ', u'ェ', 0   , 0   , 0   , u'♂',  // E0-EF
        u'¥', u'×', u'．', u'／', u'ォ', u'♀', u'０', u'１', u'２', u'３', u'４', u'５', u'６', u'７', u'８', u'９',  // F0-FF
    };

    /// Gen 1 byte -> UTF-16 code unit for the selected character set; 0 when the byte has no
    /// glyph. A 0 return MUST end the name -- see trap 1 in the file comment.
    inline char16_t gen1ToChar(uint8_t storedByte, bool japanese) noexcept {
        return japanese ? GEN1_JP[storedByte] : GEN1_EN[storedByte];
    }

    /// UTF-16 code unit -> Gen 1 byte, or GEN1_TERMINATOR when the character has no Gen 1 glyph.
    /// Neither table contains a duplicate glyph, so the reverse lookup is unambiguous.
    inline uint8_t charToGen1(char16_t character, bool japanese) noexcept {
        if (character == 0) return GEN1_TERMINATOR;   // must precede the scan -- index 0 holds 0, not a glyph
        if (japanese) {
            // PKHeX's user-friendly remap. The JP table carries KATAKANA in four slots where the
            // hiragana is what a user would type, so the hiragana forms have no direct entry:
            // be, pe, he and ri. Katakana sits 0x60 above hiragana in Unicode.
            if (character == u'\u3079' || character == u'\u307A' || character == u'\u3078' || character == u'\u308A')
                character = static_cast<char16_t>(character + 0x60);
        }
        const char16_t* glyphTable = japanese ? GEN1_JP : GEN1_EN;
        for (int byteValue = 0; byteValue < 256; ++byteValue) {
            if (glyphTable[byteValue] == character) return static_cast<uint8_t>(byteValue);
        }
        return GEN1_TERMINATOR;
    }
}

#endif  // UTILS_GEN1TEXT_H
