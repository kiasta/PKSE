/**
 * Types come from PersonalInfoTable, the same place as the abilities and gender ratio, per
 * (species, form), because that is the only place they can be kept correct: PersonalInfoTable is
 * regenerated from PKHeX, and its form redirection is the same one every other per-form fact
 * already goes through.
 *
 * Hand-written rows scraped from a web dex cannot be right here, because they carry no form
 * dimension: every retyping form then has to be re-stated by hand, and the ones missed -- Hisuian
 * Braviary among them -- quietly return the base species' types. 149 of the 465 alternate forms
 * retype their base species.
 */

#include "Pokemon/PokemonTypes.h"
#include "Pokemon/PersonalRecord.h"

#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Gen1Tables.h"

namespace Pokemon
{

    TypePair getPokemonTypes(uint16_t speciesId, uint8_t formId, Enums::GameVersion group)
    {
        // EVERY GROUP READS ITS OWN TABLE. Typing is per-game data, not a constant of the
        // species: Steel and Dark arrive in Gen 2 and Fairy in Gen 6, so a Red/Blue Clefairy is
        // Normal, a Black/White Clefairy is STILL Normal, and only from X/Y on is it Fairy;
        // Magnemite is pure Electric before Gen 2 and Electric/Steel after. Reading any of those
        // out of the Scarlet/Violet table retypes a Pokemon into something its own game has no
        // concept of -- which is what happened to every group from Gen 2 to Gen 7 while this
        // carved out only Gens 1 and 3 and sent the rest to the modern table.
        //
        // A mono-typed species already reads TYPE_NONE (255) in slot 2: the tables collapse a
        // repeated id at generation time, which is what stops the badge row drawing
        // "Normal Normal" for Rattata.
        const PersonalRecord &record = getPersonalRecord(group, speciesId, formId);
        if (record.hp == 0 && record.atk == 0)
        {
            return {255, 255}; // no row for this species -- unknown, not "typeless"
        }
        return {record.type1, record.type2};
    }

}

namespace Pokemon
{
    // PKHeX TeraTypeUtil.GetTeraType. The order matters: a VALID override wins outright (which is
    // how a Tera Shard applied to a Stellar-capable Pokemon reads back as Stellar), an override of
    // 18 or anything else out of range is not a type and falls back to Normal, and only the
    // "never overridden" sentinel defers to the original.
    uint8_t resolveTeraType(uint8_t teraTypeOriginal, uint8_t teraTypeOverride)
    {
        const bool overrideNamesAType = teraTypeOverride <= TYPE_FAIRY || teraTypeOverride == TERA_TYPE_STELLAR;
        if (overrideNamesAType)
            return teraTypeOverride;
        if (teraTypeOverride != TERA_TYPE_OVERRIDE_NONE)
            return TYPE_NORMAL;
        if (teraTypeOriginal <= TERA_TYPE_STELLAR)
            return teraTypeOriginal;
        return TYPE_NORMAL;
    }

    uint8_t teraTypeNameIndex(uint8_t teraType)
    {
        if (teraType <= TYPE_FAIRY)
            return teraType;
        if (teraType == TERA_TYPE_STELLAR)
            return TERA_TYPE_NAME_INDEX_STELLAR;
        return TYPE_NORMAL;
    }
}
