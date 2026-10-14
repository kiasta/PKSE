/**
 * FireRed/LeafGreen do NOT store the National Dex number in an entity's species field
 * (u16 @ 0x20). National 1-251 are aligned, internal 252-276 are unused slots, and the
 * Hoenn species (National 252-386) live at internal 277-411 in the games' OWN order --
 * which is scrambled, not sorted. Exactly the same problem Gen 9 has from #917 on; see
 * SpeciesConverter9.h, which this mirrors.
 *
 * PKHeX reference (source of truth):
 *   - PK3.cs: `Species => SpeciesConverter.GetNational3(SpeciesInternal)`
 *   - PKHeX.Core/PKM/Util/Conversion/SpeciesConverter.cs:
 *       FirstUnalignedNational3 = 252; FirstUnalignedInternal3 = 277;
 *       Table3NationalToInternal / Table3InternalToNational
 *
 * This replaced a hand-derived "+25 for everything past 251" shortcut. The shift is real for
 * 26 of the 135 Hoenn species and wrong for the other **109**, which is why it survived a
 * spot-check: Tentacool, Unown and Granbull are all below 252 and were never affected, while a
 * created Trapinch was stored as Plusle, Flygon as Mawile and Deoxys as Chimecho. Nothing
 * errored -- the id was valid, just a different Pokemon.
 *
 * Both tables are delta tables: result = input + table[input - first]. Do not hand-edit the
 * deltas; they are copied verbatim from PKHeX.
 *
 * One DELIBERATE difference from PKHeX: a National id past 386 returns **0** (not obtainable in
 * Gen 3) where PKHeX returns the input unchanged. Callers rely on 0 as the "no Gen 3 equivalent"
 * signal -- the bank uses it to refuse a transfer as NotInDex.
 */

#ifndef POKEMON_SPECIES_CONVERTER3_H
#define POKEMON_SPECIES_CONVERTER3_H

#include <cstdint>

namespace Pokemon
{

    /// First National id whose Gen 3 internal index diverges (everything below is aligned).
    inline constexpr uint16_t GEN3_FIRST_UNALIGNED_NATIONAL = 252;
    /// Where the Hoenn block starts internally; 252-276 are unused slots with no species.
    inline constexpr uint16_t GEN3_FIRST_UNALIGNED_INTERNAL = 277;
    /// Highest National id Gen 3 can represent.
    inline constexpr uint16_t GEN3_MAX_NATIONAL = 386;

    /// Deltas indexed by (national - 252): internal = national + delta. PKHeX Table3NationalToInternal.
    inline constexpr int8_t GEN3_NATIONAL_TO_INTERNAL[135] = {
         25,  25,  25,  25,  25,  25,  25,  25,  25,  25,
         25,  25,  25,  25,  25,  25,  25,  25,  25,  25,
         25,  25,  25,  25,  28,  28,  31,  31, 112, 112,
        112,  28,  28,  21,  21,  77,  77,  77,  11,  11,
         11,  77,  77,  77,  39,  39,  52,  21,  15,  15,
         20,  52,  78,  78,  78,  49,  49,  28,  28,  42,
         42,  73,  73,  48,  51,  51,  12,  12,  -7,  -7,
         17,  17,  -3,  26,  26, -19,   4,   4,   4,  13,
         13,  25,  25,  45,  43,  11,  11, -16, -16, -15,
        -15, -25, -25,  43,  43,  43,  43, -21, -21,  34,
        -35,  24,  24,   6,   6,  12,  53,  17,   0, -15,
        -15, -22, -22, -22,   7,   7,   7,  12, -45,  24,
         24,  24,  24,  24,  24,  24,  24,  24,  27,  27,
         22,  22,  22,  24,  24,
    };

    /// Deltas indexed by (internal - 277): national = internal + delta. PKHeX Table3InternalToNational.
    inline constexpr int8_t GEN3_INTERNAL_TO_NATIONAL[135] = {
         -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,
         -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,
         -25,  -25,  -25,  -25,  -11,  -11,  -11,  -28,  -28,  -21,
         -21,   19,  -31,  -31,  -28,  -28,    7,    7,  -15,  -15,
          35,   25,   25,  -21,    3,  -20,   16,   16,   45,   15,
          15,   21,   21,  -12,  -12,   -4,   -4,   -4,  -39,  -39,
         -28,  -28,  -17,  -17,   22,   22,   22,  -13,  -13,   15,
          15,  -11,  -11,  -52,  -26,  -26,  -42,  -42,  -52,  -49,
         -49,  -25,  -25,    0,   -6,   -6,  -48,  -77,  -77,  -77,
         -51,  -51,  -12,  -77,  -77,  -77,   -7,   -7,   -7,  -17,
         -24,  -24,  -43,  -45,  -12,  -78,  -78,  -78,  -34,  -73,
         -73,  -43,  -43,  -43,  -43, -112, -112, -112,  -24,  -24,
         -24,  -24,  -24,  -24,  -24,  -24,  -24,  -22,  -22,  -22,
         -27,  -27,  -24,  -24,  -53,
    };

    /// Raw stored species (Gen 3 internal index) -> National Dex number.
    /// 0 for an unused internal slot (252-276) or anything past the table -- there is no species there.
    inline constexpr uint16_t gen3InternalToNational(uint16_t raw) noexcept
    {
        if (raw < GEN3_FIRST_UNALIGNED_NATIONAL)
            return raw; // 0-251 aligned (0 = empty slot)
        const uint32_t shift = static_cast<uint32_t>(raw) - GEN3_FIRST_UNALIGNED_INTERNAL;
        if (shift >= sizeof(GEN3_INTERNAL_TO_NATIONAL))
            return 0; // 252-276 wrap huge -> unused slot
        return static_cast<uint16_t>(raw + GEN3_INTERNAL_TO_NATIONAL[shift]);
    }

    /// National Dex number -> raw stored species (Gen 3 internal index).
    /// 0 when the species has no Gen 3 equivalent (past #386) -- see the header note.
    inline constexpr uint16_t gen3NationalToInternal(uint16_t national) noexcept
    {
        if (national < GEN3_FIRST_UNALIGNED_NATIONAL)
            return national;
        const uint32_t shift = static_cast<uint32_t>(national) - GEN3_FIRST_UNALIGNED_NATIONAL;
        if (shift >= sizeof(GEN3_NATIONAL_TO_INTERNAL))
            return 0; // not in Gen 3 at all
        return static_cast<uint16_t>(national + GEN3_NATIONAL_TO_INTERNAL[shift]);
    }

    // Compile-time spot checks: each pairs a National id with the Gen 3 internal id the games store
    // for it.
    static_assert(gen3NationalToInternal(328) == 332, "Gen3 species: Trapinch -> 332");
    static_assert(gen3InternalToNational(353) == 311, "Gen3 species: 353 was Plusle, not Trapinch");
    static_assert(gen3NationalToInternal(330) == 334, "Gen3 species: Flygon -> 334");
    static_assert(gen3InternalToNational(355) == 303, "Gen3 species: 355 was Mawile, not Flygon");
    static_assert(gen3NationalToInternal(351) == 385, "Gen3 species: Castform -> 385");
    static_assert(gen3InternalToNational(376) == 359, "Gen3 species: 376 was Absol, not Castform");
    static_assert(gen3NationalToInternal(386) == 410, "Gen3 species: Deoxys -> 410");
    static_assert(gen3InternalToNational(411) == 358, "Gen3 species: 411 was Chimecho, not Deoxys");
    // The aligned range, the boundaries, and the sentinels.
    static_assert(gen3NationalToInternal(201) == 201, "Gen3 species: Unown aligned below 252");
    static_assert(gen3NationalToInternal(251) == 251, "Gen3 species: last aligned id");
    static_assert(gen3NationalToInternal(252) == 277, "Gen3 species: Treecko starts the Hoenn block");
    static_assert(gen3NationalToInternal(387) == 0, "Gen3 species: Turtwig has no Gen 3 id");
    static_assert(gen3InternalToNational(276) == 0, "Gen3 species: 276 is an unused slot");
    static_assert(gen3InternalToNational(277) == 252, "Gen3 species: 277 is Treecko");
    static_assert(gen3InternalToNational(412) == 0, "Gen3 species: past the last real internal id");
    static_assert(gen3NationalToInternal(0) == 0 && gen3InternalToNational(0) == 0, "Gen3 species: empty slot stays empty");
}

#endif
