/**
 * This is the suite the host harnesses cannot do at all. Every other kind of byte work in PKSE
 * compiles with an ordinary g++ and can be checked on a PC; whether the .nro that was just built
 * CONTAINS the 3260 sprite files, three fonts and nineteen type icons it will ask for is a question
 * only the .nro can answer, and it is a question with real history: romfs/ is gitignored, so a fresh
 * checkout has none of it until every asset fetcher under tools/ has been run, and a build that
 * skipped one of those is a build where every Pokemon silently draws as its base form.
 *
 * The rules being checked come from CLAUDE.md's sprite section, and each one has a specific failure
 * behind it:
 *
 *   - A NAME-keyed stem (getFormSpriteName -> "666-meadow") that names a missing file is a FAILURE.
 *     Those tables are generated and validated against romfs, so a miss means the validation did not
 *     happen for this build -- and the fallback hides it by drawing the base species.
 *   - A NUMERIC form id that differs from the species id is a mapping row someone added by hand.
 *     If its file is missing, the row is wrong; the sprite silently falls back to the base species,
 *     which is exactly what a wrong-but-existing id does too, so nothing on screen says so.
 *   - A form with NO mapping is not a finding. Art genuinely does not exist for Mothim's cloaks,
 *     the Let's Go partners, the PLA bosses and others, and falling back is the documented
 *     behaviour -- adding a mapping for them would be the bug.
 *
 * Files are probed with stat() rather than loaded: SpriteManager decodes a 256px PNG to 256 KB, and
 * sweeping fifteen hundred of them through the cache would evict everything and take minutes to
 * answer a question stat() answers immediately.
 */
#include <string>
#include <sys/stat.h>

#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/FormSpriteMapping.h"
#include "Names/TypeNames.h"
#include "Trainer/OriginStamp.h"

namespace Test
{

    namespace
    {
        constexpr int SPECIES_PER_CHUNK = 128;
        // Chunk 0 is fonts + type icons; the species sweep follows.
        constexpr int FixedChunkCount = 1;

        bool romfsHas(const std::string &rel)
        {
            struct stat entryStatus{};
            return stat(("romfs:/" + rel).c_str(), &entryStatus) == 0 && S_ISREG(entryStatus.st_mode);
        }

        /// A sprite stem is present if EITHER resolution is on the card: SpriteManager prefers the
        /// 256px HD render and falls back to the bundled 96px sprite, so having only one is fine.
        bool spriteStemExists(const std::string &stem)
        {
            return romfsHas("sprites/pokemon_hd/" + stem + ".png") || romfsHas("sprites/pokemon/" + stem + ".png");
        }

        std::string sp(uint16_t species)
        {
            return "species=" + std::to_string(species) + " (" + Trainer::getSpeciesName(species) + ")";
        }

        void checkFixedAssets(Context &context)
        {
            // The UI font and both symbol fallbacks. Without Nunito nothing draws at all; without
            // the symbol fonts the gender signs, the shiny star and the Let's Go heart come out as
            // tofu -- and the name tables deliberately keep PKHeX's punctuation on the strength of
            // those fallbacks being present.
            context.check(romfsHas("fonts/Nunito.ttf"), "UI font missing from ROMFS",
                      "path=\"romfs:/fonts/Nunito.ttf\" fix=\"python tools/gen_fonts.py\"");
            context.check(romfsHas("fonts/NotoSansSymbols.ttf"), "symbol fallback font missing from ROMFS",
                      "path=\"romfs:/fonts/NotoSansSymbols.ttf\" fix=\"python tools/gen_fonts.py\"");
            context.check(romfsHas("fonts/NotoSansSymbols2.ttf"), "second symbol fallback font missing from ROMFS",
                      "path=\"romfs:/fonts/NotoSansSymbols2.ttf\" fix=\"python tools/gen_fonts.py\"");

            // NAME count, not species-type count: Stellar (18) is drawn wherever a Tera type is.
            for (uint8_t typeIndex = 0; typeIndex < Names::getTypeNameCount(); ++typeIndex)
            {
                context.check(romfsHas("sprites/types/" + std::to_string(typeIndex) + ".png"),
                              "type icon missing from ROMFS",
                              "type=" + std::to_string(typeIndex) + " (" + Names::getTypeName(typeIndex) +
                                  ") fix=\"python tools/gen_typeicons.py\"");
            }

            // Origin markings. Driven off the enum rather than a second list of filenames, so a
            // mark added to OriginStamp.h without a matching download fails here by construction.
            for (uint8_t markIndex = 1; markIndex < static_cast<uint8_t>(Trainer::OriginMark::Count); ++markIndex)
            {
                const char *stem = Trainer::originMarkFileStem(static_cast<Trainer::OriginMark>(markIndex));
                if (stem == nullptr)
                    continue;
                context.check(romfsHas(std::string("sprites/marks/") + stem + ".png"),
                          "origin marking missing from ROMFS",
                          std::string("mark=") + stem + " fix=\"python tools/gen_marks.py\"");
            }
        }

        void checkSpeciesSprites(Context &context, uint16_t first, uint16_t last)
        {
            int fallbacks = 0;
            for (uint16_t sIndex = first; sIndex <= last && sIndex <= Pokemon::PERSONAL_MAX_SPECIES; ++sIndex)
            {
                const std::string base = std::to_string(sIndex);
                context.check(spriteStemExists(base), "species has no base sprite",
                          sp(sIndex) + " fix=\"python tools/gen_hdsprites.py\"");
                // A missing shiny is not always wrong -- the cap Pikachu correctly have none, since
                // they cannot be shiny -- but for a base species it is nearly always a fetch that
                // did not finish, so it is worth seeing without being called a failure.
                context.warn(spriteStemExists(base + "s"), "species has no shiny sprite", sp(sIndex));

                const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(sIndex, 0);
                for (uint8_t formIndex = 0; formIndex < pi.formCount; ++formIndex)
                {
                    const char *named = Pokemon::getFormSpriteName(sIndex, formIndex);
                    if (named && named[0] != '\0')
                    {
                        context.check(spriteStemExists(named), "named form sprite stem has no file",
                                  sp(sIndex) + " form=" + std::to_string(formIndex) + " stem=\"" + named + "\"");
                        continue;
                    }
                    const uint32_t itemId = Pokemon::getFormSpriteId(sIndex, formIndex);
                    if (itemId == sIndex)
                    {
                        if (formIndex > 0)
                            ++fallbacks;
                        continue;
                    } // no mapping: falls back, by design
                    context.check(spriteStemExists(std::to_string(itemId)), "mapped form sprite id has no file",
                                  sp(sIndex) + " form=" + std::to_string(formIndex) +
                                      " spriteId=" + std::to_string(itemId));
                }
            }
            if (fallbacks > 0)
                context.note("forms with no sprite mapping (drawn as the base species)",
                         "count=" + std::to_string(fallbacks) +
                             " range=" + std::to_string(first) + ".." + std::to_string(last));
        }
    }

    int assetsPlan(Context &)
    {
        const int speciesChunks = (Pokemon::PERSONAL_MAX_SPECIES + SPECIES_PER_CHUNK - 1) / SPECIES_PER_CHUNK;
        return FixedChunkCount + speciesChunks;
    }

    void assetsRun(Context &context, int chunk)
    {
        if (chunk == 0)
        {
            checkFixedAssets(context);
            return;
        }
        const uint16_t first = (uint16_t)((chunk - FixedChunkCount) * SPECIES_PER_CHUNK + 1);
        const uint16_t last = (uint16_t)(first + SPECIES_PER_CHUNK - 1);
        checkSpeciesSprites(context, first, last);
    }
}
