/**
 * This is the suite the host harnesses cannot do at all. Every other kind of byte work in PKSE
 * compiles with an ordinary g++ and can be checked on a PC; whether the .nro that was just built
 * CONTAINS the 3260 sprite files, three fonts, nineteen type icons and the ribbon art it will ask for is a question
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
#include <set>
#include <string>
#include <sys/stat.h>

#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/FormSpriteMapping.h"
#include "Names/TypeNames.h"
#include "Names/RibbonNames.h"
#include "Trainer/OriginStamp.h"

namespace Test
{

    namespace
    {
        constexpr int SPECIES_PER_CHUNK = 128;
        // Chunk 0 is fonts + type icons; the species sweep follows.
        constexpr int FIXED_CHUNK_COUNT = 1;

        bool romfsHas(const std::string &relativePath)
        {
            struct stat entryStatus{};
            return stat(("romfs:/" + relativePath).c_str(), &entryStatus) == 0 && S_ISREG(entryStatus.st_mode);
        }

        /// A sprite stem is present if EITHER resolution is on the card: SpriteManager prefers the
        /// 256px HD render and falls back to the bundled 96px sprite, so having only one is fine.
        bool spriteStemExists(const std::string &stem)
        {
            return romfsHas("sprites/pokemon_hd/" + stem + ".png") || romfsHas("sprites/pokemon/" + stem + ".png");
        }

        std::string speciesLabel(uint16_t speciesId)
        {
            return "species=" + std::to_string(speciesId) + " (" + Trainer::getSpeciesName(speciesId) + ")";
        }

        void checkFixedAssets(Context &context)
        {
            // The UI font and both symbol fallbacks. Without Nunito nothing draws at all; without
            // the symbol fonts the gender signs, the shiny star and the Let's Go heart come out as
            // tofu -- and the name tables deliberately keep PKHeX's punctuation on the strength of
            // those fallbacks being present.
            context.check(romfsHas("fonts/Nunito.ttf"), "UI font missing from ROMFS", "path=\"romfs:/fonts/Nunito.ttf\" fix=\"python tools/gen_fonts.py\"");
            context.check(romfsHas("fonts/NotoSansSymbols.ttf"), "symbol fallback font missing from ROMFS", "path=\"romfs:/fonts/NotoSansSymbols.ttf\" fix=\"python tools/gen_fonts.py\"");
            context.check(romfsHas("fonts/NotoSansSymbols2.ttf"), "second symbol fallback font missing from ROMFS", "path=\"romfs:/fonts/NotoSansSymbols2.ttf\" fix=\"python tools/gen_fonts.py\"");

            // NAME count, not species-type count: Stellar (18) is drawn wherever a Tera type is.
            for (uint8_t typeIndex = 0; typeIndex < Names::getTypeNameCount(); ++typeIndex)
            {
                context.check(romfsHas("sprites/types/" + std::to_string(typeIndex) + ".png"), "type icon missing from ROMFS", "type=" + std::to_string(typeIndex) + " (" + Names::getTypeName(typeIndex) + ") fix=\"python tools/gen_typeicons.py\"");
            }

            // Origin markings. Driven off the enum rather than a second list of filenames, so a
            // mark added to OriginStamp.h without a matching download fails here by construction.
            for (uint8_t markIndex = 1; markIndex < static_cast<uint8_t>(Trainer::OriginMark::Count); ++markIndex)
            {
                const char *stem = Trainer::originMarkFileStem(static_cast<Trainer::OriginMark>(markIndex));
                if (stem == nullptr)
                {
                    continue;
                }
                context.check(romfsHas(std::string("sprites/marks/") + stem + ".png"), "origin marking missing from ROMFS", std::string("mark=") + stem + " fix=\"python tools/gen_marks.py\"");
            }

            // Ribbon art, every image the ribbon editor can ask for: each row of each format's table
            // at each value it takes, since a rank or a full memory count draws its own image.
            std::set<std::string> ribbonSpriteStems;
            for (Enums::GameVersion group : ALL_SAVE_GROUPS)
            {
                for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(group))
                {
                    for (int ribbonValue = 0; ribbonValue <= ribbon.maxCount; ++ribbonValue)
                    {
                        ribbonSpriteStems.insert(Names::getRibbonSpriteStem(ribbon, static_cast<uint8_t>(ribbonValue), group));
                    }
                }
            }
            for (const std::string &ribbonSpriteStem : ribbonSpriteStems)
            {
                context.check(romfsHas("sprites/ribbons/" + ribbonSpriteStem + ".png"), "ribbon image missing from ROMFS", "ribbon=" + ribbonSpriteStem + " fix=\"python tools/gen_ribbons.py\"");
            }
        }

        void checkSpeciesSprites(Context &context, uint16_t firstSpeciesId, uint16_t lastSpeciesId)
        {
            int unmappedFormCount = 0;
            for (uint16_t speciesId = firstSpeciesId; speciesId <= lastSpeciesId && speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                const std::string baseStem = std::to_string(speciesId);
                context.check(spriteStemExists(baseStem), "species has no base sprite", speciesLabel(speciesId) + " fix=\"python tools/gen_hdsprites.py\"");
                // A missing shiny is not always wrong -- the cap Pikachu correctly have none, since
                // they cannot be shiny -- but for a base species it is nearly always a fetch that
                // did not finish, so it is worth seeing without being called a failure.
                context.warn(spriteStemExists(baseStem + "s"), "species has no shiny sprite", speciesLabel(speciesId));

                const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(speciesId, 0);
                for (uint8_t formId = 0; formId < personalInfo.formCount; ++formId)
                {
                    const char *namedStem = Pokemon::getFormSpriteName(speciesId, formId);
                    if (namedStem && namedStem[0] != '\0')
                    {
                        context.check(spriteStemExists(namedStem), "named form sprite stem has no file", speciesLabel(speciesId) + " form=" + std::to_string(formId) + " stem=\"" + namedStem + "\"");
                        continue;
                    }
                    const uint32_t spriteId = Pokemon::getFormSpriteId(speciesId, formId);
                    if (spriteId == speciesId)
                    {
                        // No mapping: the sprite falls back to the base species, by design.
                        if (formId > 0)
                        {
                            ++unmappedFormCount;
                        }
                        continue;
                    }
                    context.check(spriteStemExists(std::to_string(spriteId)), "mapped form sprite id has no file", speciesLabel(speciesId) + " form=" + std::to_string(formId) + " spriteId=" + std::to_string(spriteId));
                }
            }
            if (unmappedFormCount > 0)
            {
                context.note("forms with no sprite mapping (drawn as the base species)", "count=" + std::to_string(unmappedFormCount) + " range=" + std::to_string(firstSpeciesId) + ".." + std::to_string(lastSpeciesId));
            }
        }
    }

    int assetsPlan(Context &)
    {
        const int speciesChunkCount = (Pokemon::PERSONAL_MAX_SPECIES + SPECIES_PER_CHUNK - 1) / SPECIES_PER_CHUNK;
        return FIXED_CHUNK_COUNT + speciesChunkCount;
    }

    void assetsRun(Context &context, int chunk)
    {
        if (chunk == 0)
        {
            checkFixedAssets(context);
            return;
        }
        const uint16_t firstSpeciesId = (uint16_t)((chunk - FIXED_CHUNK_COUNT) * SPECIES_PER_CHUNK + 1);
        const uint16_t lastSpeciesId = (uint16_t)(firstSpeciesId + SPECIES_PER_CHUNK - 1);
        checkSpeciesSprites(context, firstSpeciesId, lastSpeciesId);
    }
}
