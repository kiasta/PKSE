/**
 * RibbonNames.h - Ribbon & Mark lookup and editing (per save format)
 *
 * Every save format that stores ribbons has one table of RibbonDefinition rows, and everything
 * PKSE does with a ribbon -- naming it, drawing it, reading it and setting it -- goes through that
 * row. Ribbons are stored at fixed byte offsets in the DECRYPTED entity buffer (the same buffer
 * PKHeX indexes as Data[...]), in three shapes:
 *
 *   - A FLAG: one bit. Almost every ribbon and every mark.
 *   - A Gen 3 CONTEST RANK: the five contest ribbons of a PK3 are 3-bit fields in the u32 at 0x4C
 *     holding the highest rank reached (0 none, 1 Normal .. 4 Master), not four flags. The same
 *     u32's bit 31 is FatefulEncounter, so a rank write must leave the rest of the word alone.
 *   - A MEMORY COUNT: Contest Memory and Battle Memory (Gen 6 on) are a count byte PLUS a "has"
 *     flag elsewhere in the block. Setting the count sets the flag to (count != 0), as PKHeX's
 *     Gen 8/9 setters do and as the games keep them.
 *
 * Tables, generated from PKHeX's entity classes by tools/gen_ribbonnames.py:
 *   - PK3 (Ruby/Sapphire/Emerald, FireRed/LeafGreen): the u32 at 0x4C.
 *   - PK4 / PK5 (Gen 4 and Gen 5): 0x24..0x27, 0x3C..0x3F and 0x60..0x62, identical in both.
 *   - PK6, and PK7 / PB7 (Gen 7, Let's Go): 0x30..0x36, counts at 0x38/0x39.
 *   - G8PKM / PA8 / PK9 / PA9 (every Gen 8/9 format): the HOME block 0x34..0x45, counts at
 *     0x3C/0x3D. Its rows are in RibbonIndex order, which is what AffixedRibbon stores.
 * Gen 1 and Gen 2 store no ribbons; their table is empty.
 *
 * The byte-value AffixedRibbon field is not a ribbon and has no row; see getRibbonAffixIndex().
 */

#ifndef NAMES_RIBBON_NAMES_H
#define NAMES_RIBBON_NAMES_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Enums/GameVersion.h"

namespace Names
{
    enum class RibbonKind : uint8_t
    {
        Flag,
        ContestRank,
        MemoryCount,
    };

    struct RibbonDefinition
    {
        RibbonKind kind;
        /// Flag and MemoryCount: the byte holding the (has-)flag. ContestRank: the u32 at 0x4C.
        uint8_t byteOffset;
        /// Flag and MemoryCount: the flag's bit in byteOffset. ContestRank: the rank field's shift.
        uint8_t bit;
        /// MemoryCount only: the byte holding the count.
        uint8_t countByteOffset;
        /// The largest value the ribbon takes: 1 for a flag, 4 for a contest rank, 40 or 8 for a count.
        uint8_t maxCount;
        /// What the GAMES call it, without the word "Ribbon" where the games append it. A rank
        /// ribbon's name is its base ("Cool Ribbon"); getRibbonName() appends the rank.
        const char *englishName;
        /// The romfs:/sprites/ribbons/ file stem (tools/gen_ribbons.py): PKHeX's property name in
        /// lower case. A rank or memory ribbon's stem is its base; getRibbonSpriteStem() picks the
        /// variant for a value.
        const char *spriteStem;
    };

    /**
     * A ribbon name in one specific language, or nullptr when this table has none for it.
     *
     * Generated from PKHeX (tools/gen_ribbonnames.py) and carries the EIGHT NON-ENGLISH languages
     * only. PKSE's English names are the ones the GAMES show, which is not always PKHeX's UI label
     * -- "Champion" rather than "Champion (Gen3)", "Cool Ribbon Super" rather than "Cool Super" --
     * so English is never taken from here. Looked up BY the English string, which is why every name
     * getRibbonName() can produce is asserted to have a row.
     */
    const char *getRibbonNameLocalized(const char *englishName, size_t languageIndex);

    /// Every ribbon the format of `group` stores, in a stable order (the HOME block's rows are in
    /// RibbonIndex order). Accepts a group id or an individual game id. Empty when the format
    /// stores no ribbons.
    std::span<const RibbonDefinition> getRibbonDefinitions(Enums::GameVersion group);

    /// The ribbon's value in a decrypted entity buffer: 0/1 for a flag, the rank (0-4) or the count.
    uint8_t getRibbonValue(const uint8_t *entityData, const RibbonDefinition &ribbon);

    /// Store `ribbonValue`, clamped to the ribbon's maxCount, touching only the ribbon's own bits
    /// (and, for a memory count, its has-flag). The caller refreshes the checksum.
    void setRibbonValue(uint8_t *entityData, const RibbonDefinition &ribbon, uint8_t ribbonValue);

    /// The ribbon's display name in the active language. A rank ribbon is named for `ribbonValue`'s
    /// rank ("Cool Ribbon Hyper"); rank 0 names the base ribbon.
    std::string getRibbonName(const RibbonDefinition &ribbon, uint8_t ribbonValue);

    /// The sprite stem drawn for `ribbonValue`: the rank's own image for a Gen 3 contest ribbon,
    /// and the gold image once a memory count reaches its maximum. PKHeX's ribbon editor turns
    /// Battle Memory gold at seven rather than eight in the Gen 9 formats, and so does this.
    std::string getRibbonSpriteStem(const RibbonDefinition &ribbon, uint8_t ribbonValue, Enums::GameVersion group);

    /// True for a mark rather than a ribbon. Marks arrived with Sword/Shield.
    bool isRibbonMark(const RibbonDefinition &ribbon);

    /// The AffixedRibbon value that displays this ribbon -- its RibbonIndex -- or -1 when `ribbon`
    /// is not a row of the HOME block, the only block AffixedRibbon indexes.
    int getRibbonAffixIndex(const RibbonDefinition &ribbon);

    /**
     * Returns the display names of every ribbon/mark set on a pokemon.
     *
     * @param entityData Pointer to the pokemon's DECRYPTED entity buffer, indexed
     *                   from offset 0 (entityData[0x34] is PKHeX's Data[0x34]).
     *                   Must be non-null and cover the group's ribbon block.
     * @param group      The pokemon's game group or game id.
     * @return Names of every SET ribbon/mark, in getRibbonDefinitions() order.
     *         Empty vector when none are set, the buffer is null, or the format
     *         stores no ribbons.
     */
    std::vector<std::string> getMonRibbons(const uint8_t *entityData, Enums::GameVersion group);
}

#endif
