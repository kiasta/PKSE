/**
 * formspritemap.cpp - (species, form) -> the PokeAPI id PKSE already maps for sprites.
 *
 * Built and run by tools/gen_formnames.py, which needs a way to ask PokeAPI about a specific form.
 * PKSE already carries a VERIFIED (species, form) -> PokeAPI id table in FormSpriteMapping (every
 * row was checked against the API's own `name` when the sprite was added), so the generator uses
 * that rather than guessing a slug from the species name -- a wrong-but-existing id fetches a
 * completely different Pokemon's form label with no error, the same trap the sprite table warns
 * about.
 *
 * PEER FAMILIES CARRY THEIR IDENTITY IN THE STEM, NOT THE ID. Flabebe, Burmy, Alcremie and Unown
 * are keyed by NAME upstream, so getFormSpriteId returns the bare species and only
 * getFormSpriteName distinguishes the form (`669-yellow`). Emitting both lets the generator ask
 * for `flabebe-yellow` rather than the default form, which is what it would otherwise get.
 *
 * Note the two id spaces are NOT the same: these are `/api/v2/pokemon/` ids, so the generator
 * resolves `/pokemon/<id>/` -> name -> `/pokemon-form/<name>/`. Asking `/pokemon-form/<id>/`
 * directly returns an unrelated form (10033 is venusaur-mega in one space and deoxys-speed in the
 * other).
 */
#include <cstdio>

#include "Names/FormNames.h"
#include "Pokemon/FormSpriteMapping.h"

int main()
{
    std::printf("[\n");
    bool first = true;
    for (uint16_t species = 1; species <= 1025; ++species)
    {
        for (uint8_t form = 0; form < 64; ++form)
        {
            const char *englishName = Names::getFormName(species, form);
            if (englishName == nullptr || englishName[0] == '\0')
                continue;
            if (!first)
                std::printf(",\n");
            first = false;
            const char *stem = Pokemon::getFormSpriteName(species, form);
            std::printf("  {\"species\":%u,\"form\":%u,\"spriteId\":%u,\"stem\":\"%s\",\"english\":\"%s\"}", species, form, Pokemon::getFormSpriteId(species, form), stem ? stem : "", englishName);
        }
    }
    std::printf("\n]\n");
    return 0;
}
