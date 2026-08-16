/**
 * FormInfo.cpp - Which forms are permanent, and which only exist during battle
 *
 * The rule PKSE follows: the Form picker offers PERMANENT forms only. A temporary form -- one the
 * game creates during battle (or while riding) and discards afterwards -- is never selectable,
 * with no override. Mega Evolution and Primal Reversion are temporary in every game that has
 * them, Legends: Z-A included.
 *
 * This is stricter than PKHeX, deliberately. PKHeX's form dropdown is unfiltered and defers to its
 * legality checker; PKSE would rather not offer an edit that the game silently undoes. PKHeX
 * remains the source of truth for the form INDICES themselves (FormConverter), just not for
 * editor policy.
 *
 * Being filtered is not the same as being unrepresentable: every form here is still named by
 * FormNames and still drawn by FormSpriteMapping, because a save can legitimately contain one
 * (a mid-battle save, a traded-in edit, a mon from another tool). The picker also still shows a
 * mon's CURRENT form even when temporary -- otherwise it would be both invisible and unfixable.
 */

#include "Pokemon/FormInfo.h"

namespace {

    /**
     * Species whose only Mega form is form 1. The exceptions -- Mega X/Y, Mega Z, the gendered and
     * per-form megas -- are handled explicitly in the switch below. Derived from the form labels in
     * FormNames.cpp; 89 species have a Mega or Primal form in total.
     */
    bool isSingleMegaSpecies(uint16_t s) {
        switch (s) {
        case 3: case 9: case 15: case 18: case 36: case 65: case 71: case 80: case 94: case 115:
        case 121: case 127: case 130: case 142: case 149: case 154: case 160: case 181: case 208:
        case 212: case 214: case 227: case 229: case 248: case 254: case 257: case 260: case 282:
        case 302: case 303: case 306: case 308: case 310: case 319: case 323: case 334: case 354:
        case 358: case 362: case 373: case 376: case 380: case 381: case 382: case 383: case 384:
        case 398: case 428: case 460: case 475: case 478: case 485: case 491: case 500: case 530:
        case 531: case 545: case 560: case 604: case 609: case 623: case 652: case 655: case 668:
        case 687: case 689: case 691: case 701: case 719: case 740: case 768: case 780: case 807:
        case 870: case 952: case 970: case 998:
            return true;
        default:
            return false;
        }
    }

}

namespace Pokemon {

    bool isBattleOnlyForm(uint16_t species, uint8_t form) {
        switch (species) {
            // --- Species mixing permanent and temporary forms -------------------------------
            // Read these as "which forms are TEMPORARY"; everything else for that species is
            // permanent and stays selectable.
            case 6:                                 // Charizard  Mega X / Mega Y
            case 150:  return form == 1 || form == 2;   // Mewtwo    Mega X / Mega Y
            case 26:   return form == 2 || form == 3;   // Raichu -- Alolan (1) is permanent
            case 359:                               // Absol
            case 445:                               // Garchomp
            case 448:  return form == 1 || form == 2;   // Lucario -- Mega + Mega Z
            case 555:  return (form & 1) == 1;      // Darmanitan: Zen (1) and Galarian Zen (3)
            case 658:  return form >= 2;            // Greninja: Ash (2) + Mega (3); Battle Bond (1) is permanent
            case 670:  return form == 6;            // Floette: Mega only -- Eternal (5) is permanent
            case 678:  return form == 2 || form == 3;   // Meowstic: the two gendered megas
            case 718:  return form >= 4;            // Zygarde: Complete (4) + Mega (5)
            case 774:  return form < 7;             // Minior: the Meteor shells. Its stored shape is the Core (7-13).
            case 778:  return (form & 1) == 1;      // Mimikyu: Busted (1) and Totem Busted (3)
            case 800:  return form == 3;            // Necrozma: Ultra -- the Dusk Mane/Dawn Wings fusions persist
            case 801:  return form == 2 || form == 3;   // Magearna: Mega + Mega Original Color
            case 978:  return form >= 3;            // Tatsugiri: the three megas
            case 1017: return form >= 4;            // Ogerpon: the Terastallized Embody Aspect forms

            // Koraidon/Miraidon ride builds. PKHeX keeps their ride state in the form ARGUMENT
            // rather than the form index, so it does not treat these as battle forms -- but a
            // boxed one is always form 0.
            case 1007:
            case 1008: return form != 0;

            // --- Species where every non-zero form is temporary ------------------------------
            case 351:  // Castform (weather)
            case 382:  // Kyogre (Primal)
            case 383:  // Groudon (Primal)
            case 421:  // Cherrim (Sunshine)
            case 648:  // Meloetta (Pirouette)
            case 681:  // Aegislash (Blade)
            case 716:  // Xerneas (Active)
            case 746:  // Wishiwashi (School)
            case 845:  // Cramorant (Gulping/Gorging)
            case 875:  // Eiscue (Noice Face)
            case 877:  // Morpeko (Hangry)
            case 888:  // Zacian (Crowned Sword)
            case 889:  // Zamazenta (Crowned Shield)
            case 890:  // Eternatus (Eternamax)
            case 964:  // Palafin (Hero)
            case 1024: // Terapagos (Terastal/Stellar)
                return form != 0;

            default:
                break;
        }

        // Everything else: a Mega at form 1, or nothing temporary at all.
        return form == 1 && isSingleMegaSpecies(species);
    }

    // PKHeX FormInfo.HasMegaForm -- membership of its BattleMegas list. That list is exactly the
    // union of the two groups above (77 single-mega species + the 12 given their own case in
    // isBattleOnlyForm) MINUS Kyogre and Groudon: those two revert to PRIMAL forms, which PKHeX
    // tracks separately and Legends: Z-A's "seen mega" flag does not count. Verified by extracting
    // BattleMegas and diffing it against these two groups -- the only difference is that pair.
    bool hasMegaForm(uint16_t species) {
        if (species == 382 || species == 383) return false;   // Kyogre / Groudon are Primal
        if (isSingleMegaSpecies(species)) return true;
        switch (species) {
            case 6:    // Charizard   Mega X / Y
            case 26:   // Raichu      Mega X / Y (Z-A)
            case 150:  // Mewtwo      Mega X / Y
            case 359:  // Absol       Mega + Mega Z
            case 445:  // Garchomp    Mega + Mega Z
            case 448:  // Lucario     Mega + Mega Z
            case 658:  // Greninja
            case 670:  // Floette
            case 678:  // Meowstic    gendered
            case 718:  // Zygarde
            case 801:  // Magearna
            case 978:  // Tatsugiri   per-form
                return true;
            default:
                return false;
        }
    }

    // PKHeX FormInfo.IsMegaForm / IsBattleMegaForm. Distinct from isBattleOnlyForm: that also covers
    // Zen, Busted, School and friends, which are temporary but not Megas. Only the species whose Mega
    // does NOT sit at every non-zero index need a case -- Raichu 1 is Alolan, Greninja 2 is Ash,
    // Floette 5 is Eternal, Zygarde 4 is Complete, Slowbro 2 is Galarian.
    bool isMegaForm(uint16_t species, uint8_t form) {
        if (!hasMegaForm(species)) return false;
        switch (species) {
            case 26:   return form == 2 || form == 3;   // Raichu     Mega X / Y
            case 80:   return form == 1;                // Slowbro    (2 is Galarian)
            case 658:  return form == 3;                // Greninja   (2 is Ash)
            case 670:  return form == 6;                // Floette    (5 is Eternal)
            case 678:  return form == 2 || form == 3;   // Meowstic   gendered Megas
            case 718:  return form == 5;                // Zygarde    (4 is Complete)
            case 801:  return form == 2 || form == 3;   // Magearna   Mega + Original Color
            case 978:  return form >= 3 && form <= 5;   // Tatsugiri  three Megas
            default:   return form != 0;
        }
    }

    // The five Legends: Arceus nobles. Verbatim from PKHeX's FormInfo.IsLordForm.
    //
    // PKHeX gates this on the entity being Gen 8a; PKSE does not, because a form INDEX means the same
    // thing wherever it is read -- Arcanine form 2 is the Lord form full stop, and no other game
    // assigns that index to anything else. The per-game presence bit already removes these forms from
    // every game but Legends: Arceus, which is the only one whose table carries them.
    bool isLordForm(uint16_t species, uint8_t form) {
        switch (species) {
            case 59:   // Arcanine
            case 101:  // Electrode
            case 549:  // Lilligant
            case 713:  return form == 2;   // Avalugg
            case 900:  return form == 1;   // Kleavor
            default:   return false;
        }
    }

    // Verbatim from PKHeX's SpeciesCategory.IsFormGenderSpecific. The same four fall out of the
    // personal table on their own -- they are the only species where form 0 is male-only AND
    // form 1 is female-only -- which is what makes them different from Pikachu, Greninja and
    // Ursaluna, whose single-gender forms all hang off a dual-gender form 0.
    bool isFormGenderSpecific(uint16_t species) {
        switch (species) {
            case 678:   // Meowstic
            case 876:   // Indeedee
            case 902:   // Basculegion
            case 916:   // Oinkologne
                return true;
            default:
                return false;
        }
    }

    bool isFormChangeable(uint16_t species) {
        switch (species) {
            case 412:   // Burmy      -- cloak follows the last battle's terrain
            case 676:   // Furfrou    -- trims, from the barber
            case 741:   // Oricorio   -- nectar
            case 479:   // Rotom      -- appliances
            case 386:   // Deoxys     -- meteorites
            case 483:   // Dialga
            case 484:   // Palkia
            case 487:   // Giratina   -- Griseous Orb / Core
            case 492:   // Shaymin    -- Gracidea
            case 493:   // Arceus     -- plates
            case 641:   // Tornadus
            case 642:   // Thundurus
            case 645:   // Landorus   -- Reveal Glass
            case 646:   // Kyurem     -- fusion
            case 647:   // Keldeo     -- Secret Sword
            case 649:   // Genesect   -- drives
            case 720:   // Hoopa      -- Prison Bottle
            case 773:   // Silvally   -- memories
            case 800:   // Necrozma   -- fusion
            case 898:   // Calyrex    -- fusion
            case 905:   // Enamorus   -- Reveal Glass
            case 1017:  // Ogerpon    -- masks
            case 718:   // Zygarde    -- Reassembly Unit (Gen 8+; see the header note)
            case 585:   // Deerling   -- province/season, re-rolled on startup from Gen 8 on
            case 586:   // Sawsbuck
                return true;
            default:
                return false;
        }
    }

    uint8_t genderLinkedForm(uint16_t species, uint8_t currentForm, uint8_t gender) {
        if (!isFormGenderSpecific(species) || gender > 1) return currentForm;
        // Low bit only -- the rest of the index is whatever else the form encodes (Meowstic's
        // Z-A Mega lives at 2/3, so Mega male 2 flips to Mega female 3, not to plain female 1).
        return static_cast<uint8_t>((currentForm & ~1u) | gender);
    }

    uint32_t correctEncryptionConstantForForm(uint16_t species, uint8_t form, uint32_t ec) {
        bool needsZero;
        switch (species) {
            case 925:  needsZero = (form == 0); break;   // Maushold: Family of Three is the rare roll
            case 982:  needsZero = (form == 1); break;   // Dudunsparce: Three-Segment is the rare roll
            default:   return ec;                        // no EC-to-form correlation
        }

        const uint32_t mod = ec % 100u;
        if (needsZero == (mod == 0u)) return ec;         // already agrees -- leave the EC alone

        // Replace the low two digits, keep the rest (PKHeX: `rand - (rand % mod) + noise`). The
        // non-zero case derives its digits from the EC instead of rolling, so the result is stable
        // and flipping the form back and forth cannot walk the value.
        uint32_t base  = ec - mod;
        const uint32_t noise = needsZero ? 0u : (1u + (ec / 100u) % 99u);
        // `base` is a multiple of 100, so it can sit within `noise` of the u32 ceiling; step down a
        // whole hundred rather than wrapping, which would land on the wrong residue.
        if (base > 0xFFFFFFFFu - noise) base -= 100u;
        return base + noise;
    }

}
