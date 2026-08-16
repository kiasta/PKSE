/**
 * FormInfo.h - Which forms are permanent, and which only exist during battle
 *
 * The personal table's formCount counts every form entry the game defines, including Mega
 * Evolution and the forms that exist only mid-battle (or, for the box legends, while being
 * ridden). Those are fine to *display* -- FormNames names them and FormSpriteMapping draws them,
 * because a buffer can contain one -- but offering them as an edit writes a form the game
 * discards on load.
 */

#pragma once

#include <cstdint>

namespace Pokemon {

    /**
     * True when (species, form) is a TEMPORARY form -- one the game creates during battle or
     * while riding and then discards. The Form picker never offers these, with no override:
     * "Allow illegal edits" does not lift it, because unlike an out-of-range EV the game will
     * overwrite the value regardless, so offering it would misrepresent what was saved.
     *
     * **Mega Evolution and Primal Reversion count as temporary**, in every game including
     * Legends: Z-A. (PKHeX exposes megas in its form dropdown for Z-A; that is editor
     * permissiveness, not a statement that they persist -- PKHeX's own tables class every mega
     * as battle-only. PKSE follows the tables, not the dropdown.)
     *
     * A mon's current form is still shown by the picker even when this returns true, so an
     * already-temporary form stays visible and reversible.
     */
    bool isBattleOnlyForm(uint16_t species, uint8_t form);

    /**
     * True when the species has a **Mega Evolution** at all. Ported from PKHeX
     * `FormInfo.HasMegaForm` (membership of its `BattleMegas` list).
     *
     * Kyogre and Groudon are deliberately excluded: they revert to *Primal* forms, which PKHeX
     * tracks apart from Megas -- and Legends: Z-A's Pokedex "seen mega" flag follows that split.
     */
    bool hasMegaForm(uint16_t species);

    /**
     * True when (species, form) IS a Mega form. Ported from PKHeX `FormInfo.IsMegaForm`.
     *
     * Narrower than isBattleOnlyForm(), which also covers Zen, Busted, School, Ultra and the rest --
     * temporary forms that are not Megas. Used by the Legends: Z-A Pokedex, whose entry records a
     * "seen mega" bit separate from the plain form flags.
     */
    bool isMegaForm(uint16_t species, uint8_t form);

    /**
     * True when (species, form) is a Legends: Arceus **Lord/Lady** form -- the five noble bosses.
     * Ported from PKHeX `FormInfo.IsLordForm`.
     *
     * These are a third category, separate from both of the questions above. A Lord form is not
     * temporary (the game does not overwrite it) and it IS flagged present in Legends: Arceus, so
     * neither the battle-only filter nor the per-game presence bit excludes it -- but the player
     * never catches a noble, so no save can legitimately contain one. PKHeX agrees in its own way:
     * it bars these from breeding, and its Legends: Arceus verifier marks a noble-flagged entity
     * invalid outright.
     *
     * As with the other filters, a mon already IN one of these forms still shows it, so an existing
     * one stays visible and reversible; it just can never be applied to anything else.
     */
    bool isLordForm(uint16_t species, uint8_t form);

    /**
     * True for the four species that encode GENDER as the form index: Meowstic, Indeedee,
     * Basculegion and Oinkologne. Ported from PKHeX `SpeciesCategory.IsFormGenderSpecific`.
     *
     * These are dual-gender species whose two genders happen to be stored as two form entries
     * (even form = male, odd form = female), so the personal table gives form 0 a male-only
     * ratio and form 1 a female-only one. Read per form that says "this Meowstic can never be
     * female", which is false about the species -- it just means the female one is form 1.
     *
     * So gender and form move TOGETHER for these: both genders are always offered, and picking
     * one rewrites the form (and vice versa). `genderLinkedForm` does that rewrite.
     *
     * The rule is the low bit only, never a whole-form assignment: Legends: Z-A gives Meowstic a
     * gendered Mega at forms 2 and 3, so flipping a Mega Meowstic-M has to land on 3 (Mega
     * Meowstic-F), not on 1. PKHeX's FormVerifier states the same invariant from the other side --
     * `(form & 1) != gender` is what it rejects.
     *
     * NOT the same thing as a form that merely happens to be single-gender. Cap Pikachu, Ash
     * Greninja and Bloodmoon Ursaluna are male-only forms of dual-gender species, and there is no
     * female counterpart to flip to -- for those the form dictates the gender one-way. The
     * discriminator is that here form 0 ITSELF is single-gender; for those three it is not.
     */
    bool isFormGenderSpecific(uint16_t species);

    /**
     * The form `species` must be in to have `gender` (0 male / 1 female), preserving everything
     * the form index encodes besides gender. Returns `currentForm` unchanged when the species is
     * not gender-specific, or when `gender` is not male/female.
     */
    uint8_t genderLinkedForm(uint16_t species, uint8_t currentForm, uint8_t gender);

    /**
     * True when the species can freely change form AFTER it was caught — a held plate, a
     * memory disc, a baber's trim, the season, a form-change item. Ported from PKHeX
     * `FormInfo.IsFormChangeable`'s `FormChange` list.
     * 
     * Layer 3 needs it because an encounter template records the form that SPAWNED, and for
     * these species that says nothing about the form now stored. A Rotom caught in its base
     * form and later put in a washing machine is still the same encounter; comparing the form
     * would report a mismatch that is not one.
     * 
     * PKHeX's own version takes the old new form plus both contexts, because Zygarde and
     * Deerling changed rules between generations. Every game PKSE supports is Gen 8 or later
     * (or Gen 3, which has none of these species, bar Deoxys), and in that gen both are freely
     * changeable, so the extra arguments would only ever narrow to `true`.
     */
    bool isFormChangeable(uint16_t species);

    /**
     * An EncryptionConstant that satisfies the species' **form correlation**, given the form it is
     * being put into. Returns `ec` unchanged for every species that has no such correlation, and for
     * one that already satisfies it.
     *
     * Two species decide their form from `EC % 100` rather than storing it independently -- the form
     * byte and the EC have to agree or the Pokemon is illegal, and the games will not produce the
     * mismatch. Editing the form alone is therefore only half the edit.
     *
     * The correlation is **inverted between the two**, which is the whole trap:
     *
     * | species     | `EC % 100 == 0` | otherwise |
     * |-------------|-----------------|-----------|
     * | Maushold    | Family of Three (form 0) | Family of Four (form 1)  |
     * | Dudunsparce | Three-Segment (form 1)   | Two-Segment (form 0)     |
     *
     * So the rare roll gives Maushold its form **0** and Dudunsparce its form **1**. (That is also
     * why PokeAPI's default Maushold render is Family of Four -- the common outcome -- while the
     * game numbers Family of Three as form 0.) Matches PKHeX EvolutionRestrictions
     * GetIsExpectedEvolveFormEC100 and the setter in CommonEdits.
     *
     * Where a change is needed the low two digits are replaced and the rest of the EC is kept, as
     * PKHeX does: an EC that must become non-zero mod 100 takes a value derived from the EC itself
     * rather than a random one, so the result is reproducible and re-applying is a no-op.
     */
    uint32_t correctEncryptionConstantForForm(uint16_t species, uint8_t form, uint32_t ec);

}
