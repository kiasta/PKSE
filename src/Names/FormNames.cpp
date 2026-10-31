/**
 * Maps (species ID, form ID) pairs to form names for display purposes.
 *
 * Every form a save can hold a value for is named here, INCLUDING battle-only ones (Zen Mode,
 * Schooling, Aegislash's Blade, Minior's cores, Mega). The details view has to render what is
 * actually in the buffer, and a Zen-form Darmanitan showing as "Form 1" is exactly how a
 * mislabelled form goes unnoticed. Whether a form can be *selected* is a separate question,
 * answered by Pokemon::isBattleOnlyForm(), which gates the Form picker.
 *
 * Ordering follows PKHeX's FormConverter, NOT dex or appearance order, and it has real
 * exceptions: Slowbro's Mega comes before its Galarian form, and Darmanitan's Galarian form is
 * 2 because form 1 is Zen. Gigantamax is a flag in the save, not a form index, so it is absent.
 */

#include <cstdint>
#include <string>

#include "Names/FormNames.h"
#include "Names/NameLanguage.h"
#include "Names/TypeNames.h" // Arceus / Silvally name one form per type
#include "Utils/Logger.h"

namespace Names
{
    /// The curated ENGLISH table. See getFormName below for why this is not generated.
    static const char *getFormNameEnglish(uint16_t speciesId, uint8_t formId)
    {
        // Regional Variants - Form 1 = Regional Form
        switch (speciesId)
        {
        // Alolan Forms (Gen 7)
        case 19:  // Rattata
        case 27:  // Sandshrew
        case 28:  // Sandslash
        case 37:  // Vulpix
        case 38:  // Ninetales
        case 50:  // Diglett
        case 51:  // Dugtrio
        case 74:  // Geodude
        case 75:  // Graveler
        case 76:  // Golem
        case 88:  // Grimer
        case 89:  // Muk
        case 103: // Exeggutor
            if (formId == 1)
                return "Alolan";
            break;

        // Raticate and Marowak have an Alolan form AND a Gen 7 Totem form at 2, so they
        // can't ride the plain Alolan group.
        case 20:  // Raticate
        case 105: // Marowak
            if (formId == 1)
                return "Alolan";
            else if (formId == 2)
                return "Totem";
            break;

        // Meowth/Persian - Has both Alolan and Galarian
        // Meowth has both Alolan and Galarian; Persian only Alolan (Galarian Meowth
        // evolves into Perrserker, so there is no Galarian Persian).
        case 52: // Meowth
            if (formId == 1)
                return "Alolan";
            else if (formId == 2)
                return "Galarian";
            break;

        case 53: // Persian
            if (formId == 1)
                return "Alolan";
            break;

        // Galarian Forms (Gen 8)
        case 77:  // Ponyta
        case 78:  // Rapidash
        case 79:  // Slowpoke
        case 83:  // Farfetch'd
        case 110: // Weezing
        case 122: // Mr. Mime
        case 144: // Articuno
        case 145: // Zapdos
        case 146: // Moltres
        case 199: // Slowking
        case 222: // Corsola
        case 263: // Zigzagoon
        case 264: // Linoone
        case 554: // Darumaka
        case 562: // Yamask
        case 618: // Stunfisk
            if (formId == 1)
                return "Galarian";
            break;

        // Hisuian Forms (Gen 8 - Legends: Arceus)
        case 58:  // Growlithe
        case 100: // Voltorb
        case 157: // Typhlosion
        case 211: // Qwilfish
        case 215: // Sneasel
        case 503: // Samurott
        case 570: // Zorua
        case 571: // Zoroark
        case 628: // Braviary
        case 705: // Sliggoo
        case 706: // Goodra
        case 724: // Decidueye
            if (formId == 1)
                return "Hisuian";
            break;

        // Legends: Arceus noble/alpha variants sit at form 2, after the Hisuian form.
        case 59:  // Arcanine
        case 101: // Electrode
        case 713: // Avalugg
            if (formId == 1)
                return "Hisuian";
            else if (formId == 2)
                return "Lord";
            break;

        case 549: // Lilligant
            if (formId == 1)
                return "Hisuian";
            else if (formId == 2)
                return "Lady";
            break;

        // Wooper - Paldean form
        case 194: // Wooper
            if (formId == 1)
                return "Paldean";
            break;

        // Tauros - Paldean breeds
        case 128: // Tauros
            if (formId == 1)
                return "Combat Breed";
            else if (formId == 2)
                return "Blaze Breed";
            else if (formId == 3)
                return "Aqua Breed";
            break;

        // Pokemon with multiple permanent forms

        // Raichu / Slowbro / Meowstic sit apart from their regional + gender groups
        // because Legends: Z-A gives each of them Mega forms after those.
        case 26: // Raichu
            if (formId == 1)
                return "Alolan";
            else if (formId == 2)
                return "Mega X";
            else if (formId == 3)
                return "Mega Y";
            break;

        // Slowbro kept Mega at form 1 from Gen 6, so Galar was appended at 2 -- the one
        // species where a Mega precedes a regional form. (Ability check: form 1 is Shell
        // Armor = Mega, form 2 is Quick Draw = Galarian.)
        case 80: // Slowbro
            if (formId == 1)
                return "Mega";
            else if (formId == 2)
                return "Galarian";
            break;

        case 678: // Meowstic
            if (formId == 0)
                return "Male";
            else if (formId == 1)
                return "Female";
            else if (formId == 2)
                return "Male Mega";
            else if (formId == 3)
                return "Female Mega";
            break;

        // Gender difference
        case 876: // Indeedee
        case 902: // Basculegion
        case 916: // Oinkologne
            if (formId == 0)
                return "Male";
            else if (formId == 1)
                return "Female";
            break;

        // Deoxys - Different stats per form
        case 386: // Deoxys
            if (formId == 0)
                return "Normal";
            else if (formId == 1)
                return "Attack";
            else if (formId == 2)
                return "Defense";
            else if (formId == 3)
                return "Speed";
            break;

        // Burmy/Wormadam/Mothim - Cloak forms (Mothim's is inherited and cosmetic)
        case 412: // Burmy
        case 413: // Wormadam
        case 414: // Mothim
            if (formId == 0)
                return "Plant Cloak";
            else if (formId == 1)
                return "Sandy Cloak";
            else if (formId == 2)
                return "Trash Cloak";
            break;

        // Rotom - Appliance forms
        case 479: // Rotom
            if (formId == 1)
                return "Heat";
            else if (formId == 2)
                return "Wash";
            else if (formId == 3)
                return "Frost";
            else if (formId == 4)
                return "Fan";
            else if (formId == 5)
                return "Mow";
            break;

        // Giratina - Altered/Origin
        case 487: // Giratina
            if (formId == 0)
                return "Altered";
            else if (formId == 1)
                return "Origin";
            break;

        // Shaymin - Land/Sky
        case 492: // Shaymin
            if (formId == 0)
                return "Land";
            else if (formId == 1)
                return "Sky";
            break;

        case 550: // Basculin
            if (formId == 0)
                return "Red-Striped";
            else if (formId == 1)
                return "Blue-Striped";
            else if (formId == 2)
                return "White-Striped";
            break;

        // Tornadus/Thundurus/Landorus - Incarnate/Therian forms
        case 641: // Tornadus
        case 642: // Thundurus
        case 645: // Landorus
            if (formId == 0)
                return "Incarnate";
            else if (formId == 1)
                return "Therian";
            break;

        case 646: // Kyurem
            if (formId == 1)
                return "White";
            else if (formId == 2)
                return "Black";
            break;

        case 647: // Keldeo
            if (formId == 0)
                return "Ordinary";
            else if (formId == 1)
                return "Resolute";
            break;

        case 648: // Meloetta
            if (formId == 0)
                return "Aria";
            else if (formId == 1)
                return "Pirouette";
            break;

        // Pumpkaboo/Gourgeist - Size variants
        case 710: // Pumpkaboo
        case 711: // Gourgeist
            if (formId == 0)
                return "Average";
            else if (formId == 1)
                return "Small";
            else if (formId == 2)
                return "Large";
            else if (formId == 3)
                return "Super";
            break;

        case 718: // Zygarde
            if (formId == 0)
                return "50%";
            else if (formId == 1)
                return "10%";
            else if (formId == 2)
                return "10% Power Construct";
            else if (formId == 3)
                return "50% Power Construct";
            else if (formId == 4)
                return "Complete";
            else if (formId == 5)
                return "Mega";
            break;

        case 720: // Hoopa
            if (formId == 0)
                return "Confined";
            else if (formId == 1)
                return "Unbound";
            break;

        case 741: // Oricorio
            if (formId == 0)
                return "Baile";
            else if (formId == 1)
                return "Pom-Pom";
            else if (formId == 2)
                return "Pa'u";
            else if (formId == 3)
                return "Sensu";
            break;

        case 745: // Lycanroc
            if (formId == 0)
                return "Midday";
            else if (formId == 1)
                return "Midnight";
            else if (formId == 2)
                return "Dusk";
            break;

        case 800: // Necrozma
            if (formId == 1)
                return "Dusk Mane";
            else if (formId == 2)
                return "Dawn Wings";
            else if (formId == 3)
                return "Ultra";
            break;

        case 849: // Toxtricity
            if (formId == 0)
                return "Amped";
            else if (formId == 1)
                return "Low Key";
            break;

        // Zacian/Zamazenta - Crowned forms
        case 888: // Zacian
            if (formId == 0)
                return "Hero of Many Battles";
            else if (formId == 1)
                return "Crowned Sword";
            break;
        case 889: // Zamazenta
            if (formId == 0)
                return "Hero of Many Battles";
            else if (formId == 1)
                return "Crowned Shield";
            break;

        case 892: // Urshifu
            if (formId == 0)
                return "Single Strike";
            else if (formId == 1)
                return "Rapid Strike";
            break;

        case 898: // Calyrex
            if (formId == 1)
                return "Ice Rider";
            else if (formId == 2)
                return "Shadow Rider";
            break;

        case 901: // Ursaluna
            if (formId == 1)
                return "Bloodmoon";
            break;

        case 905: // Enamorus
            if (formId == 0)
                return "Incarnate";
            else if (formId == 1)
                return "Therian";
            break;

        // Maushold - Family size
        // This form variant is unique in that the rare variant is form 0, while the "base" form is form 1
        case 925: // Maushold
            if (formId == 0)
                return "Family of Three";
            else if (formId == 1)
                return "Family of Four";
            break;

        case 931: // Squawkabilly
            if (formId == 0)
                return "Green Plumage";
            else if (formId == 1)
                return "Blue Plumage";
            else if (formId == 2)
                return "Yellow Plumage";
            else if (formId == 3)
                return "White Plumage";
            break;

        case 978: // Tatsugiri
            if (formId == 0)
                return "Curly";
            else if (formId == 1)
                return "Droopy";
            else if (formId == 2)
                return "Stretchy";
            else if (formId == 3)
                return "Curly Mega";
            else if (formId == 4)
                return "Droopy Mega";
            else if (formId == 5)
                return "Stretchy Mega";
            break;

        // Dudunsparce - Segment count
        case 982: // Dudunsparce
            if (formId == 0)
                return "Two-Segment";
            else if (formId == 1)
                return "Three-Segment";
            break;

        case 999: // Gimmighoul
            if (formId == 0)
                return "Chest";
            else if (formId == 1)
                return "Roaming";
            break;

        // Poltchageist / Sinistcha - Forms (different names, so not a shared case)
        case 1012: // Poltchageist
            if (formId == 0)
                return "Counterfeit";
            else if (formId == 1)
                return "Artisan";
            break;

        case 1013: // Sinistcha
            if (formId == 0)
                return "Unremarkable";
            else if (formId == 1)
                return "Masterpiece";
            break;

        // Palafin - Zero/Hero
        case 964: // Palafin
            if (formId == 0)
                return "Zero";
            else if (formId == 1)
                return "Hero";
            break;

        // Ogerpon - Masks (permanent when holding a mask); 4-7 are the Terastallized
        // Embody Aspect forms, which only exist mid-battle.
        case 1017: // Ogerpon
            if (formId == 0)
                return "Teal Mask";
            else if (formId == 1)
                return "Wellspring Mask";
            else if (formId == 2)
                return "Hearthflame Mask";
            else if (formId == 3)
                return "Cornerstone Mask";
            else if (formId == 4)
                return "Teal Mask Terastal";
            else if (formId == 5)
                return "Wellspring Mask Terastal";
            else if (formId == 6)
                return "Hearthflame Mask Terastal";
            else if (formId == 7)
                return "Cornerstone Mask Terastal";
            break;

        case 1024: // Terapagos
            if (formId == 1)
                return "Terastal";
            else if (formId == 2)
                return "Stellar";
            break;

        // Pikachu - event caps. PKHeX's Gen 8+ list; "Starter" is the Let's Go partner,
        // which is a different form from "Partner Cap".
        case 25: // Pikachu
            if (formId == 1)
                return "Original Cap";
            else if (formId == 2)
                return "Hoenn Cap";
            else if (formId == 3)
                return "Sinnoh Cap";
            else if (formId == 4)
                return "Unova Cap";
            else if (formId == 5)
                return "Kalos Cap";
            else if (formId == 6)
                return "Alola Cap";
            else if (formId == 7)
                return "Partner Cap";
            else if (formId == 8)
                return "Starter";
            else if (formId == 9)
                return "World Cap";
            break;

        // Eevee - Let's Go partner
        case 133: // Eevee
            if (formId == 1)
                return "Starter";
            break;

        // Unown - one form per glyph
        case 201:
        { // Unown
            static const char *const LETTERS[28] = {
                "A",
                "B",
                "C",
                "D",
                "E",
                "F",
                "G",
                "H",
                "I",
                "J",
                "K",
                "L",
                "M",
                "N",
                "O",
                "P",
                "Q",
                "R",
                "S",
                "T",
                "U",
                "V",
                "W",
                "X",
                "Y",
                "Z",
                "!",
                "?",
            };
            if (formId < 28)
                return LETTERS[formId];
            break;
        }

        // Arceus / Silvally - one form per type, in the internal type order, so the shared
        // type table names them. Arceus form 18 is the Legends: Arceus Legend Plate.
        case 493: // Arceus
            if (formId == 18)
                return "Legend";
            if (formId < 18)
                return getTypeName(formId);
            break;

        case 773: // Silvally
            if (formId < 18)
                return getTypeName(formId);
            break;

        case 649: // Genesect
            if (formId == 1)
                return "Douse Drive";
            else if (formId == 2)
                return "Shock Drive";
            else if (formId == 3)
                return "Burn Drive";
            else if (formId == 4)
                return "Chill Drive";
            break;

        // Gen 7 Totem forms (Raticate, Marowak and Mimikyu handle theirs above, since
        // they have a regional or busted form in the way).
        case 735: // Gumshoos
        case 738: // Vikavolt
        case 743: // Ribombee
        case 752: // Araquanid
        case 754: // Lurantis
        case 758: // Salazzle
        case 777: // Togedemaru
        case 784: // Kommo-o
            if (formId == 1)
                return "Totem";
            break;

        // Rockruff - the Own Tempo (Dusk-evolving) variant
        case 744: // Rockruff
            if (formId == 1)
                return "Dusk";
            break;

        case 893: // Zarude
            if (formId == 1)
                return "Dada";
            break;

        case 900: // Kleavor
            if (formId == 1)
                return "Lord";
            break;

        // PKHeX names form 0 of each of these. Leaving form 0 as "Base" is the same class of
        // gap that let Maushold's swapped labels sit unnoticed: with no name to contradict,
        // a wrong form index looks exactly like a right one.

        // Darmanitan - Zen is form 1, so Galarian lands at 2. NOT the usual "form 1 =
        // regional" shape, which is why it sat in the Galarian group mislabelled.
        case 555: // Darmanitan
            if (formId == 0)
                return "Standard";
            else if (formId == 1)
                return "Zen";
            else if (formId == 2)
                return "Galarian";
            else if (formId == 3)
                return "Galarian Zen";
            break;

        case 351: // Castform
            if (formId == 1)
                return "Sunny";
            else if (formId == 2)
                return "Rainy";
            else if (formId == 3)
                return "Snowy";
            break;

        // Cramorant - Gulp Missile
        case 845: // Cramorant
            if (formId == 1)
                return "Gulping";
            else if (formId == 2)
                return "Gorging";
            break;

        case 890: // Eternatus
            if (formId == 1)
                return "Eternamax";
            break;

        case 421: // Cherrim
            if (formId == 0)
                return "Overcast";
            else if (formId == 1)
                return "Sunshine";
            break;

        // Shellos/Gastrodon - Sea
        case 422: // Shellos
        case 423: // Gastrodon
            if (formId == 0)
                return "West Sea";
            else if (formId == 1)
                return "East Sea";
            break;

        // Deerling/Sawsbuck - Seasons
        case 585: // Deerling
        case 586: // Sawsbuck
            if (formId == 0)
                return "Spring";
            else if (formId == 1)
                return "Summer";
            else if (formId == 2)
                return "Autumn";
            else if (formId == 3)
                return "Winter";
            break;

        // Scatterbug/Spewpa/Vivillon - Patterns (inherited, so all three carry the same 20)
        case 664: // Scatterbug
        case 665: // Spewpa
        case 666: // Vivillon
            if (formId == 0)
                return "Icy Snow";
            else if (formId == 1)
                return "Polar";
            else if (formId == 2)
                return "Tundra";
            else if (formId == 3)
                return "Continental";
            else if (formId == 4)
                return "Garden";
            else if (formId == 5)
                return "Elegant";
            else if (formId == 6)
                return "Meadow";
            else if (formId == 7)
                return "Modern";
            else if (formId == 8)
                return "Marine";
            else if (formId == 9)
                return "Archipelago";
            else if (formId == 10)
                return "High Plains";
            else if (formId == 11)
                return "Sandstorm";
            else if (formId == 12)
                return "River";
            else if (formId == 13)
                return "Monsoon";
            else if (formId == 14)
                return "Savanna";
            else if (formId == 15)
                return "Sun";
            else if (formId == 16)
                return "Ocean";
            else if (formId == 17)
                return "Jungle";
            else if (formId == 18)
                return "Fancy";
            else if (formId == 19)
                return "Poké Ball";
            break;

        // Flabebe/Florges - Flower colours (Floette shares these, plus Eternal + Mega)
        case 669: // Flabebe
        case 671: // Florges
            if (formId == 0)
                return "Red Flower";
            else if (formId == 1)
                return "Yellow Flower";
            else if (formId == 2)
                return "Orange Flower";
            else if (formId == 3)
                return "Blue Flower";
            else if (formId == 4)
                return "White Flower";
            break;

        case 676: // Furfrou
            if (formId == 0)
                return "Natural";
            else if (formId == 1)
                return "Heart Trim";
            else if (formId == 2)
                return "Star Trim";
            else if (formId == 3)
                return "Diamond Trim";
            else if (formId == 4)
                return "Debutante Trim";
            else if (formId == 5)
                return "Matron Trim";
            else if (formId == 6)
                return "Dandy Trim";
            else if (formId == 7)
                return "La Reine Trim";
            else if (formId == 8)
                return "Kabuki Trim";
            else if (formId == 9)
                return "Pharaoh Trim";
            break;

        case 681: // Aegislash
            if (formId == 0)
                return "Shield";
            else if (formId == 1)
                return "Blade";
            break;

        case 716: // Xerneas
            if (formId == 0)
                return "Neutral";
            else if (formId == 1)
                return "Active";
            break;

        case 746: // Wishiwashi
            if (formId == 0)
                return "Solo";
            else if (formId == 1)
                return "School";
            break;

        // Minior - Meteor (shields up) 0-6, then Core (shields down) 7-13
        case 774: // Minior
            if (formId == 0)
                return "Meteor Red";
            else if (formId == 1)
                return "Meteor Orange";
            else if (formId == 2)
                return "Meteor Yellow";
            else if (formId == 3)
                return "Meteor Green";
            else if (formId == 4)
                return "Meteor Blue";
            else if (formId == 5)
                return "Meteor Indigo";
            else if (formId == 6)
                return "Meteor Violet";
            else if (formId == 7)
                return "Core Red";
            else if (formId == 8)
                return "Core Orange";
            else if (formId == 9)
                return "Core Yellow";
            else if (formId == 10)
                return "Core Green";
            else if (formId == 11)
                return "Core Blue";
            else if (formId == 12)
                return "Core Indigo";
            else if (formId == 13)
                return "Core Violet";
            break;

        // Mimikyu - Disguise; 2/3 are the Gen 7 Totem entries
        case 778: // Mimikyu
            if (formId == 0)
                return "Disguised";
            else if (formId == 1)
                return "Busted";
            else if (formId == 2)
                return "Totem Disguised";
            else if (formId == 3)
                return "Totem Busted";
            break;

        // Sinistea/Polteageist - Authenticity
        case 854: // Sinistea
        case 855: // Polteageist
            if (formId == 0)
                return "Phony";
            else if (formId == 1)
                return "Antique";
            break;

        // Alcremie - Cream. The sweet is a separate form-argument byte, not a form index.
        case 869: // Alcremie
            if (formId == 0)
                return "Vanilla Cream";
            else if (formId == 1)
                return "Ruby Cream";
            else if (formId == 2)
                return "Matcha Cream";
            else if (formId == 3)
                return "Mint Cream";
            else if (formId == 4)
                return "Lemon Cream";
            else if (formId == 5)
                return "Salted Cream";
            else if (formId == 6)
                return "Ruby Swirl";
            else if (formId == 7)
                return "Caramel Swirl";
            else if (formId == 8)
                return "Rainbow Swirl";
            break;

        // Eiscue - Ice Face
        case 875: // Eiscue
            if (formId == 0)
                return "Ice Face";
            else if (formId == 1)
                return "Noice Face";
            break;

        // Morpeko - Hunger Switch
        case 877: // Morpeko
            if (formId == 0)
                return "Full Belly";
            else if (formId == 1)
                return "Hangry";
            break;

        // Koraidon/Miraidon - the ride builds/modes exist only while riding
        case 1007: // Koraidon
            if (formId == 0)
                return "Apex Build";
            else if (formId == 1)
                return "Limited Build";
            else if (formId == 2)
                return "Sprinting Build";
            else if (formId == 3)
                return "Swimming Build";
            else if (formId == 4)
                return "Gliding Build";
            break;

        case 1008: // Miraidon
            if (formId == 0)
                return "Ultimate Mode";
            else if (formId == 1)
                return "Low-Power Mode";
            else if (formId == 2)
                return "Drive Mode";
            else if (formId == 3)
                return "Aquatic Mode";
            else if (formId == 4)
                return "Glide Mode";
            break;

        // Mega Evolution -- storable again in Legends: Z-A. Single-mega species all
        // put Mega at form 1 (PKHeX FormConverter).
        case 3:
        case 9:
        case 15:
        case 18:
        case 36:
        case 65:
        case 71:
        case 94:
        case 115:
        case 121:
        case 127:
        case 130:
        case 142:
        case 149:
        case 154:
        case 160:
        case 181:
        case 208:
        case 212:
        case 214:
        case 227:
        case 229:
        case 248:
        case 254:
        case 257:
        case 260:
        case 282:
        case 302:
        case 303:
        case 306:
        case 308:
        case 310:
        case 319:
        case 323:
        case 334:
        case 354:
        case 358:
        case 362:
        case 373:
        case 376:
        case 380:
        case 381:
        case 384:
        case 398:
        case 428:
        case 460:
        case 475:
        case 478:
        case 485:
        case 491:
        case 500:
        case 530:
        case 531:
        case 545:
        case 560:
        case 604:
        case 609:
        case 623:
        case 652:
        case 655:
        case 668:
        case 687:
        case 689:
        case 691:
        case 701:
        case 719:
        case 740:
        case 768:
        case 780:
        case 807:
        case 870:
        case 952:
        case 970:
        case 998:
            if (formId == 1)
                return "Mega";
            break;
        case 6: // Charizard
            if (formId == 1)
                return "Mega X";
            else if (formId == 2)
                return "Mega Y";
            break;

        case 150: // Mewtwo
            if (formId == 1)
                return "Mega X";
            else if (formId == 2)
                return "Mega Y";
            break;

        case 359: // Absol
            if (formId == 1)
                return "Mega";
            else if (formId == 2)
                return "Mega Z";
            break;

        case 382: // Kyogre
            if (formId == 1)
                return "Primal";
            break;

        case 383: // Groudon
            if (formId == 1)
                return "Primal";
            break;

        case 445: // Garchomp
            if (formId == 1)
                return "Mega";
            else if (formId == 2)
                return "Mega Z";
            break;

        case 448: // Lucario
            if (formId == 1)
                return "Mega";
            else if (formId == 2)
                return "Mega Z";
            break;

        case 483: // Dialga
            if (formId == 1)
                return "Origin";
            break;

        case 484: // Palkia
            if (formId == 1)
                return "Origin";
            break;

        case 658: // Greninja
            if (formId == 1)
                return "Battle Bond";
            else if (formId == 2)
                return "Ash";
            else if (formId == 3)
                return "Mega";
            break;

        case 670: // Floette
            if (formId == 0)
                return "Red Flower";
            else if (formId == 1)
                return "Yellow Flower";
            else if (formId == 2)
                return "Orange Flower";
            else if (formId == 3)
                return "Blue Flower";
            else if (formId == 4)
                return "White Flower";
            else if (formId == 5)
                return "Eternal";
            else if (formId == 6)
                return "Mega";
            break;

        case 801: // Magearna
            if (formId == 1)
                return "Original Color";
            else if (formId == 2)
                return "Mega";
            else if (formId == 3)
                return "Mega Original Color";
            break;
        }

        // If no match found, return empty string
        return "";
    }

    /**
     * The form name in the active display language.
     *
     * ENGLISH IS ASKED FIRST, and not only because it is the fallback. It decides WHETHER this
     * (species, form) is a named form at all: PKSE deliberately leaves form 0 unnamed for most
     * species, so a language table that named it would make the same Pokemon read "Normal Vulpix"
     * in French and "Vulpix" in English. Gating on the English answer keeps every language
     * agreeing about which forms have a label, with only the wording varying.
     *
     * The curated English table is also the BETTER English -- it says "Alolan", "Plant Cloak" and
     * "West Sea" where PKHeX's generated labels say "Alola", "Plant" and "West" -- so it is never
     * overridden. A language with no row falls back to it rather than showing a blank.
     */
    const char *getFormName(uint16_t speciesId, uint8_t formId)
    {
        const char *englishName = getFormNameEnglish(speciesId, formId);
        if (englishName == nullptr || englishName[0] == '\0')
            return englishName; // not a named form -- and so not a named form in any language
        const size_t languageIndex = displayLanguageIndex();
        if (languageIndex == LANGUAGE_INDEX_ENGLISH)
            return englishName;
        const char *localizedName = getFormNameLocalized(speciesId, formId, languageIndex);
        return (localizedName != nullptr && localizedName[0] != '\0') ? localizedName : englishName;
    }

    /**
     * The variant label joined to the species name, e.g. "Alolan Raichu" / "Goupix (Forme d'Alola)".
     *
     * THE GAMES DO NOT COMPOSE THIS AT ALL. They show the form as a separate label -- the Pokedex
     * titles it "Alola Form" -- and their running text often drops the regional adjective outright.
     * "Alolan Vulpix" is a wiki and community convention, not a string any game displays. So this
     * is PKSE's own affordance: an editor shows hundreds of Pokemon at once and a form has to be
     * legible at a glance, which the games never need to solve.
     *
     * ENGLISH KEEPS THE NATURAL PREFIX because that convention exists and reads well. No other
     * language can borrow it, and not merely because of word order:
     *
     *   fr  Goupix d'Alola      it  Vulpix di Alola     de  Alola-Vulpix    ja  アローラのロコン
     *
     * French, Italian and Spanish put the label AFTER; German prefixes with a hyphen; Japanese uses
     * a particle. Worse, every one of them uses a SHORTER attributive than the standalone label --
     * "d'Alola", not "Forme d'Alola" -- and that shortening is per language and per form family.
     * Deriving it was tried and measured against PokeAPI's composed names: 73% agreement in French,
     * 36% in German, and Italian and Spanish carry no composed names to check against at all. The
     * German ones that did exist turned out to be PokeAPI's own parenthetical inventions.
     *
     * So every other language gets a PARENTHETICAL. It consumes the sourced label verbatim, invents
     * no grammar it cannot verify, stays correct for every form family rather than just regional
     * ones, and is how the French and Italian Pokedex sites present a form themselves.
     */
    namespace
    {
        std::string composeDisplayName(const char *formName, const std::string &baseName)
        {
            if (formName == nullptr || formName[0] == '\0')
                return baseName;
            if (displayLanguageIndex() == LANGUAGE_INDEX_ENGLISH)
                return std::string(formName) + " " + baseName;
            return baseName + " (" + formName + ")";
        }
    }

    std::string getDisplayName(uint16_t speciesId, uint8_t formId, const std::string &baseName)
    {
        return composeDisplayName(getFormName(speciesId, formId), baseName);
    }

    const char *getFormName(uint16_t speciesId, uint8_t formId, Enums::GameVersion group)
    {
        constexpr uint16_t pikachuSpecies = 25;
        constexpr uint16_t arceusSpecies = 493;
        const int generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
        // PKHeX FormConverter.GetFormsPikachu, Gen 6: ORAS's Cosplay outfits, not the Gen 7+ caps.
        // English only -- the localized table is built from the Gen 7+ list, so another language
        // shows these English labels rather than a cap's name.
        if (speciesId == pikachuSpecies && generation == 6)
        {
            static const char *const cosplayNames[] = {"", "Rock Star", "Belle", "Pop Star", "Ph.D.", "Libre", "Cosplay"};
            return formId < sizeof(cosplayNames) / sizeof(cosplayNames[0]) ? cosplayNames[formId] : "";
        }
        // PKHeX FormConverter.GetFormsArceus, Gen 4: the ???-type plate sits at 9, so Fire through
        // Dark are 10-17, and there is no Fairy.
        if (speciesId == arceusSpecies && generation == 4)
        {
            constexpr uint8_t questionTypeForm = 9;
            constexpr uint8_t lastGenerationFourForm = 17;
            if (formId == questionTypeForm)
                return "???";
            if (formId > lastGenerationFourForm)
                return "";
            return getTypeName(formId < questionTypeForm ? formId : static_cast<uint8_t>(formId - 1));
        }
        return getFormName(speciesId, formId);
    }

    std::string getDisplayName(uint16_t speciesId, uint8_t formId, const std::string &baseName, Enums::GameVersion group)
    {
        return composeDisplayName(getFormName(speciesId, formId, group), baseName);
    }
}
