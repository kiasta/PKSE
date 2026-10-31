/**
 * FormNames.h - Pokemon Form Name Lookup
 *
 * Provides form name mapping for Pokemon with regional variants and other permanent forms.
 * Excludes battle-only forms (Mega Evolution, Gigantamax, Dynamax, etc.)
 */

#ifndef NAMES_FORM_NAMES_H
#define NAMES_FORM_NAMES_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"

namespace Names
{
    /**
     * Gets the form name for a given Pokemon species and form ID.
     *
     * @param speciesId The Pokemon's species ID (1-1025)
     * @param formId The form ID (0 = base form, 1+ = alternate forms)
     * @return Form name string, or empty string for base form (form 0)
     *
     * Examples:
     * - getFormName(26, 1) = "Alolan" (Alolan Raichu)
     * - getFormName(479, 1) = "Heat" (Heat Rotom)
     * - getFormName(25, 0) = "" (base Pikachu)
     */
    const char *getFormName(uint16_t speciesId, uint8_t formId);

    /**
     * The form name in one specific language, or nullptr when this table has none for it.
     *
     * Generated from PKHeX's own FormConverter (tools/gen_formnames.py). It carries the EIGHT
     * NON-ENGLISH languages only: PKSE's English names are hand-curated in FormNames.cpp and are
     * closer to the games than PKHeX's terse dropdown labels ("Alolan" rather than "Alola",
     * "Plant Cloak" rather than "Plant"), so English is never taken from here. getFormName()
     * applies this; call it directly only when you need a language other than the active one.
     */
    const char *getFormNameLocalized(uint16_t speciesId, uint8_t formId, size_t languageIndex);

    /**
     * Composes the user-facing display name: the variant label prefixed to the base species name,
     * e.g. "Alolan Raichu", "Combat Breed Tauros", "Hisuian Zoroark". Falls back to just `baseName`
     * when the form has no variant label (base form, or a form this table doesn't name). Use this at
     * every place the UI shows a species name so a variant reads as what it is instead of the plain
     * base species.
     *
     * @param speciesId species id (for the form lookup)
     * @param formId    the pokemon's form index (Pokemon::form())
     * @param baseName  the game-appropriate base species string (e.g. Pokemon::species())
     */
    std::string getDisplayName(uint16_t speciesId, uint8_t formId, const std::string &baseName);

    /**
     * getFormName as the save-format `group` numbers its forms. The plain overload is the Gen 7+
     * numbering, which two species do not share: ORAS's Pikachu forms 1-6 are the Cosplay outfits
     * (the caps there), and Gen 4's Arceus has the ???-type at form 9, moving Fire to Dark to
     * 10-17 with no Fairy. Every other (species, form) answers as the plain overload does.
     */
    const char *getFormName(uint16_t speciesId, uint8_t formId, Enums::GameVersion group);

    /// getDisplayName with the form named as `group` numbers it -- see the overload above.
    std::string getDisplayName(uint16_t speciesId, uint8_t formId, const std::string &baseName, Enums::GameVersion group);
}

#endif
