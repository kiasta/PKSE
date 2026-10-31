/**
 * Runs before anything that needs a save, and it runs first on purpose: a hole in one of these
 * tables explains every downstream failure at once, and finding it at the TOP of the report saves
 * chasing the same bug five times under five generations.
 *
 * These are the tables CLAUDE.md calls out as the most common source of wrong behaviour in the
 * codebase, and the reason is always the same shape -- a lookup that is missing returns something
 * that reads as a definitive negative. An empty move pool is not "unknown moves", it is "every move
 * is illegal", and the transfer sanitizer then deletes all four. So the checks here are mostly
 * "this table has an entry AT ALL", which is exactly the failure that is otherwise invisible.
 */
#include <set>
#include <string>

#include "Test/TestSuite.h"
#include "Pokemon/FormInfo.h"
#include "Save/TitleFolder.h" // the one rule for what a title is called
#include "Trainer/Trainer.h"
#include "Trainer/Bank.h" // the entity factory, for the per-group capability sweep
#include "Names/FormNames.h"
#include "Names/ItemNames.h"
#include "Names/RibbonNames.h"
#include "Conversion/Convert.h" // affixedRibbonOffset -- which formats index ribbons for display
#include <cstring>
#include "Names/NameLanguage.h"
#include "Names/MoveNames.h"
#include "Names/TypeNames.h"
#include "Names/LocationNames.h" // egg-source locations
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/Experience.h"
#include "Pokemon/Evolution.h" // the trade-evolution offer + the action it gates
#include "Pokemon/Trade.h"     // applyTradeRoundTrip -- the handler block a trade leaves
#include "Trainer/TradeDonor.h" // gamesCanTrade -- which saves may stand in as a partner
#include "Pokemon/Gen1Tables.h"
#include "Legality/Legality.h"      // the encounter rules are asserted through the analyzer itself
#include "Pokemon/AbilityInfo.h"    // the Legends: Z-A birth ability
#include "Pokemon/EvolutionTable.h" // form-keeping evolution edges
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/PersonalInfo1RBY.h"
#include "Utils/StringHelpers.h"
#include "Pokemon/PokemonTypes.h"
#include "Pokemon/PersonalRecord.h"
#include "Names/MoveInfo.h"
#include "Names/MovePresence.h"
#include "Trainer/OriginStamp.h"

namespace Test
{

    namespace
    {
        /// The Gen 7 totem forms: NPC-only, so they are in the personal table and in no game.
        /// PKHeX FormInfo.IsTotemForm / HasTotemForm.
        bool isTotemForm(uint16_t speciesId, uint8_t formId)
        {
            switch (speciesId)
            {
            case 20:  // Raticate (Alolan totem)
            case 105: // Marowak (Alolan totem)
                return formId == 2;
            case 778: // Mimikyu (totem, disguised and busted)
                return formId == 2 || formId == 3;
            case 735: // Gumshoos
            case 738: // Vikavolt
            case 743: // Ribombee
            case 752: // Araquanid
            case 754: // Lurantis
            case 758: // Salazzle
            case 777: // Togedemaru
            case 784: // Kommo-o
                return formId == 1;
            default:
                return false;
            }
        }

        enum Chunk
        {
            SpeciesNamesChunk = 0,
            MoveNamesChunk,
            MiscNamesChunk,
            ExperienceChunk,
            PersonalChunkA,
            PersonalChunkB,
            FormNamesChunk,
            Gen1TablesChunk,
            EggSourceLocationsChunk,
            EntityCapabilitiesChunk,
            TitleRoutingChunk,
            NameLanguagesChunk,
            TeraTypesChunk,
            TradeEvolutionsChunk,
            HandlerTradeChunk,
            TradePartnerRulesChunk,
            EncounterRulesChunk,
            RibbonDefinitionsChunk,
            KnownValuesChunk,
            ChunkCount
        };

        constexpr uint16_t PERSONAL_CHUNK_SPLIT_SPECIES = 512; // species 1..511 in chunk A, the rest in B

        bool isNamed(const char *text)
        {
            return text != nullptr && text[0] != '\0';
        }

        /// PKHeX's gender byte is a small closed set: always-male (0), the four thresholds for
        /// 12.5 / 25 / 50 / 75 percent female (31 / 63 / 127 / 191), 87.5 percent female (225 --
        /// Litleo and Pyroar are the only species that use it), always-female (254) and genderless
        /// (255). Anything else means the byte being read is not the gender ratio at all, which is
        /// exactly the Gen 1 trap where personal_rb byte 0 looks like a dex number.
        bool isCanonicalGenderRatio(uint8_t genderRatio)
        {
            return genderRatio == 0 || genderRatio == 31 || genderRatio == 63 || genderRatio == 127 || genderRatio == 191 || genderRatio == 225 || genderRatio == 254 || genderRatio == 255;
        }

        std::string speciesLabel(uint16_t speciesId)
        {
            return "species=" + std::to_string(speciesId) + " (" + Trainer::getSpeciesName(speciesId) + ")";
        }

        /**
         * Every name table answers in every language, and the language actually switches.
         *
         * PKSE shipped English-only tables, so a French save read "Bulbasaur" where the game says
         * "Bulbizarre". The tables are now one array per language indexed by
         * Names::displayLanguageIndex(), and the two ways that goes wrong are both silent: a
         * language whose table is short reads off the end (caught by the generator, and again by
         * the static_assert it emits), and a getter that forgot to index by language keeps
         * returning English for everyone -- which looks perfectly fine to an English tester.
         *
         * THE COMPARISON IS TABLE-WIDE, NOT PER-NAME, and that is not laziness. Species names are
         * largely SHARED across English, Italian and Spanish -- only 21 of 1026 differ -- so a
         * sample-based test asking "is Bulbasaur translated?" fails on real, correct data. What is
         * true of every language is that its table differs from English SOMEWHERE, and that is
         * exactly the property a getter ignoring the language index would break.
         */
        void checkNameLanguages(Context &context)
        {
            const size_t originalLanguage = Names::displayLanguageIndex();

            // PER TABLE, not pooled. An earlier version compared one flat list of every name and
            // asked only whether SOMETHING differed from English -- which passes even when a whole
            // table is broken, because the other six still differ. Breaking getItemName on purpose
            // proved it: zero failures. Each table has to answer for itself.
            using NameTable = std::pair<const char *, std::vector<std::string>>;
            auto snapshot = [](std::vector<NameTable> &nameTables)
            {
                nameTables.clear();
                std::vector<std::string> rows;
                for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
                {
                    rows.push_back(Trainer::getSpeciesName(speciesId));
                }
                nameTables.push_back({"species", rows});
                rows.clear();
                for (uint16_t moveId = 1; moveId <= Pokemon::LEARN_MAX_MOVE_ID; ++moveId)
                {
                    rows.push_back(Names::getMoveName(moveId));
                }
                nameTables.push_back({"moves", rows});
                rows.clear();
                for (uint16_t itemId = 1; itemId < Names::getItemCount(); ++itemId)
                {
                    rows.push_back(Names::getItemName(itemId));
                }
                nameTables.push_back({"items", rows});
                rows.clear();
                for (uint16_t gen3ItemId = 1; gen3ItemId < Names::getItemCountG3(); ++gen3ItemId)
                {
                    rows.push_back(Names::getItemNameG3(gen3ItemId));
                }
                nameTables.push_back({"gen3 items", rows});
                rows.clear();
                for (uint16_t abilityId = 1; abilityId < 300; ++abilityId)
                {
                    rows.push_back(Trainer::getAbilityName(abilityId));
                }
                nameTables.push_back({"abilities", rows});
                rows.clear();
                for (uint8_t natureId = 0; natureId < 25; ++natureId)
                {
                    rows.push_back(Trainer::getNatureName(natureId));
                }
                nameTables.push_back({"natures", rows});
                rows.clear();
                for (uint8_t typeIndex = 0; typeIndex < Names::getTypeNameCount(); ++typeIndex)
                {
                    rows.push_back(Names::getTypeName(typeIndex));
                }
                nameTables.push_back({"types", rows});
                rows.clear();
                // Form names: English is the curated table and the other eight come from PKHeX,
                // so this checks the DISPATCHER is wired -- a getFormName that forgot to consult
                // the language would return the English name for everyone and fail the
                // differs-from-English check below.
                for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
                {
                    for (uint8_t formId = 0; formId < 4; ++formId)
                    {
                        rows.push_back(Names::getFormName(speciesId, formId));
                    }
                }
                nameTables.push_back({"forms", rows});
                rows.clear();
                // Locations go through the same dispatcher the details view uses.
                for (uint16_t locationId = 1; locationId < 200; ++locationId)
                {
                    rows.push_back(Names::getMetLocationName(static_cast<uint8_t>(Enums::GameVersion::SL), locationId));
                }
                nameTables.push_back({"locations", rows});
            };

            std::vector<NameTable> englishTables;
            Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
            snapshot(englishTables);

            for (size_t languageIndex = 0; languageIndex < Names::LANGUAGE_COUNT; ++languageIndex)
            {
                const std::string languageLabel = std::string("language=") + Names::nameLanguageSuffix(languageIndex);
                Names::setDisplayLanguage(languageIndex);
                context.check(Names::displayLanguageIndex() == languageIndex, "the display language did not take", languageLabel);

                std::vector<NameTable> tables;
                snapshot(tables);
                for (size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex)
                {
                    const std::string tableLabel = languageLabel + " table=" + tables[tableIndex].first;
                    const std::vector<std::string> &rows = tables[tableIndex].second;
                    const std::vector<std::string> &englishRows = englishTables[tableIndex].second;
                    if (!context.check(rows.size() == englishRows.size(), "a language returned a different number of names", tableLabel)) continue;
                    size_t differenceCount = 0;
                    size_t missingCount = 0;
                    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
                    {
                        // An empty string is legitimate: the location tables emit "" for a blank
                        // slot and for PKHeX's em-dash "no location" sentinel, so a run of ids is
                        // correctly nameless in EVERY language, English included. The invariant
                        // that holds is relative -- wherever English has a name, so must the rest.
                        if (englishRows[rowIndex].empty()) continue;
                        if (rows[rowIndex].empty())
                        {
                            ++missingCount;
                        }
                        else if (rows[rowIndex] != englishRows[rowIndex])
                        {
                            ++differenceCount;
                        }
                    }
                    context.check(missingCount == 0, "a name is missing from a language's table", tableLabel + " missing=" + std::to_string(missingCount));
                    if (languageIndex == Names::LANGUAGE_INDEX_ENGLISH) continue;
                    context.check(differenceCount > 0, "a table is identical to English in another language -- that getter is probably not indexing by language", tableLabel);
                }
            }
            // FORM NAMES NEED THEIR OWN CHECK, and the reason is worth keeping. The table-wide
            // "differs from English somewhere" test above does NOT bite for forms: a handful of
            // them (Arceus's plates, Silvally's memories) are named by calling getTypeName(), which
            // is itself language-indexed, so those eight names shift language even when the form
            // dispatcher is broken -- enough to satisfy "something differed" and hide the fault.
            // Breaking getFormName on purpose proved it: differences=8, zero failures.
            //
            // The contract to assert is the direct one: wherever the generated table HAS a name for
            // a (species, form) in this language, getFormName must return exactly that.
            for (size_t languageIndex = 0; languageIndex < Names::LANGUAGE_COUNT; ++languageIndex)
            {
                // English deliberately comes from the curated switch, not the table
                if (languageIndex == Names::LANGUAGE_INDEX_ENGLISH) continue;
                Names::setDisplayLanguage(languageIndex);
                const std::string languageLabel = std::string("language=") + Names::nameLanguageSuffix(languageIndex);
                size_t checkedFormCount = 0;
                size_t wrongFormCount = 0;
                for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
                {
                    for (uint8_t formId = 0; formId < 4; ++formId)
                    {
                        const char *localizedFormName = Names::getFormNameLocalized(speciesId, formId, languageIndex);
                        if (localizedFormName == nullptr || localizedFormName[0] == '\0') continue;
                        // Only where English names the form at all -- getFormName gates on that.
                        Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
                        const std::string englishFormName = Names::getFormName(speciesId, formId);
                        Names::setDisplayLanguage(languageIndex);
                        if (englishFormName.empty()) continue;
                        ++checkedFormCount;
                        if (std::string(Names::getFormName(speciesId, formId)) != localizedFormName)
                        {
                            ++wrongFormCount;
                        }
                    }
                }
                context.check(checkedFormCount > 0, "no localized form names were reachable at all", languageLabel);
                context.check(wrongFormCount == 0, "getFormName ignored the localized form table", languageLabel + " wrong=" + std::to_string(wrongFormCount) + " of " + std::to_string(checkedFormCount));
            }

            // getDisplayName's composition. English keeps the natural prefix ("Alolan Vulpix");
            // every other language gets a parenthetical, because the games compose no such name at
            // all and the attributive differs per language and per form family -- see the comment
            // on getDisplayName. A form English does not name must stay a bare species name in
            // EVERY language, which is the part that would regress silently.
            {
                const std::string speciesName = "Vulpix";
                for (size_t languageIndex = 0; languageIndex < Names::LANGUAGE_COUNT; ++languageIndex)
                {
                    Names::setDisplayLanguage(languageIndex);
                    const std::string languageLabel = std::string("language=") + Names::nameLanguageSuffix(languageIndex);
                    const std::string formName = Names::getFormName(37, 1);   // Alolan Vulpix
                    const std::string displayName = Names::getDisplayName(37, 1, speciesName);
                    if (languageIndex == Names::LANGUAGE_INDEX_ENGLISH)
                    {
                        context.check(displayName == formName + " " + speciesName, "English display name is not the natural prefix form", languageLabel + " got=" + displayName);
                    }
                    else
                    {
                        context.check(displayName == speciesName + " (" + formName + ")", "a non-English display name is not the parenthetical form", languageLabel + " got=" + displayName);
                    }
                    // Pikachu form 0 is unnamed, so it composes to nothing in any language.
                    context.check(Names::getDisplayName(25, 0, speciesName) == speciesName, "an unnamed form still altered the display name", languageLabel + " got=" + Names::getDisplayName(25, 0, speciesName));
                }
            }

            // RIBBONS, by the same direct contract as forms: every name a ribbon table can produce,
            // at every value it can take, in every language. Driven off the definitions rather than a
            // buffer of set bits, because a Gen 3 contest rank COMPOSES its name ("Cool Ribbon Hyper")
            // from its value, and an all-ones buffer reads as no rank at all.
            for (Enums::GameVersion group : ALL_SAVE_GROUPS)
            {
                for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(group))
                {
                    for (int ribbonValue = 1; ribbonValue <= ribbon.maxCount; ++ribbonValue)
                    {
                        Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
                        const std::string englishRibbonName = Names::getRibbonName(ribbon, static_cast<uint8_t>(ribbonValue));
                        for (size_t languageIndex = 0; languageIndex < Names::LANGUAGE_COUNT; ++languageIndex)
                        {
                            if (languageIndex == Names::LANGUAGE_INDEX_ENGLISH)
                            {
                                continue;
                            }
                            Names::setDisplayLanguage(languageIndex);
                            const std::string where = Enums::getGameVersionName(group) + " ribbon=\"" + englishRibbonName + "\" language=" + Names::nameLanguageSuffix(languageIndex);
                            const char *expectedRibbonName = Names::getRibbonNameLocalized(englishRibbonName.c_str(), languageIndex);
                            if (!context.check(expectedRibbonName != nullptr && expectedRibbonName[0] != '\0', "a ribbon PKSE can show has no localized row", where))
                            {
                                continue;
                            }
                            context.check(Names::getRibbonName(ribbon, static_cast<uint8_t>(ribbonValue)) == expectedRibbonName, "getRibbonName ignored the localized ribbon table", where);
                        }
                    }
                }
            }

            // Leave the global exactly as it was found: every later suite reads these tables, and a
            // run that ended in Korean would report every name check against the wrong language.
            Names::setDisplayLanguage(originalLanguage);
            context.check(Names::displayLanguageIndex() == originalLanguage, "the suite did not restore the display language", "");
        }

        void checkSpeciesNames(Context &context)
        {
            std::set<std::string> seenSpeciesNames;
            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                const char *speciesName = Trainer::getSpeciesName(speciesId);
                if (!context.check(isNamed(speciesName), "species has no name", "species=" + std::to_string(speciesId))) continue;
                // "Unknown" is the table's own out-of-range sentinel, and the legality checker tests
                // for it by value. A real species answering with it is a hole in the table.
                context.check(std::string(speciesName) != "Unknown", "species names as the unknown sentinel", "species=" + std::to_string(speciesId));
                // Two species sharing a name means one row was pasted over another; the UI then shows
                // the same name for two different Pokemon and nothing else notices.
                context.check(seenSpeciesNames.insert(speciesName).second, "two species share a name", speciesLabel(speciesId));
            }
        }

        void checkMoveNames(Context &context)
        {
            for (uint16_t moveId = 1; moveId <= Pokemon::LEARN_MAX_MOVE_ID; ++moveId)
            {
                // "-" is what getMoveName returns past the end of its table, so a table shorter than
                // LEARN_MAX_MOVE_ID fails here instead of reading as named.
                const char *moveName = Names::getMoveName(moveId);
                context.check(isNamed(moveName) && std::string(moveName) != "-", "move has no name", "move=" + std::to_string(moveId));
            }
        }

        void checkMiscNames(Context &context)
        {
            for (uint8_t typeIndex = 0; typeIndex < Names::getTypeNameCount(); ++typeIndex)
            {
                context.check(isNamed(Names::getTypeName(typeIndex)), "type has no name", "type=" + std::to_string(typeIndex));
            }

            for (uint8_t natureId = 0; natureId < 25; ++natureId)
            {
                const char *natureName = Trainer::getNatureName(natureId);
                context.check(isNamed(natureName) && std::string(natureName) != "Unknown", "nature has no name", "nature=" + std::to_string(natureId));
            }

            // Abilities are swept through the personal table rather than over the raw id space,
            // because the id space is not the interesting set: an ability that no species has is
            // unreachable, while an ability a species DOES have that names as "Unknown" is an
            // ability the details view cannot show.
            std::set<uint16_t> abilityIds;
            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(speciesId, 0);
                abilityIds.insert(personalInfo.ability1);
                abilityIds.insert(personalInfo.ability2);
                abilityIds.insert(personalInfo.abilityHidden);
            }
            abilityIds.erase(0);
            for (uint16_t abilityId : abilityIds)
            {
                const char *abilityName = Trainer::getAbilityName(abilityId);
                context.check(isNamed(abilityName) && std::string(abilityName) != "Unknown", "ability used by a species has no name", "ability=" + std::to_string(abilityId));
            }
            context.note("abilities referenced by the personal table", "count=" + std::to_string(abilityIds.size()));
        }

        void checkExperience(Context &context)
        {
            // Six growth rates. The table is shared all the way back to Gen 1 (the rates were
            // checked and are identical for all 151), so a break here is a break everywhere.
            for (uint8_t growthRate = 0; growthRate < 6; ++growthRate)
            {
                uint32_t previousExp = 0;
                for (uint8_t level = 1; level <= 100; ++level)
                {
                    const uint32_t levelExp = Pokemon::getExpForLevel(level, growthRate);
                    if (level > 1)
                    {
                        context.check(levelExp > previousExp, "EXP for a level is not above the previous level's", "rate=" + std::to_string(growthRate) + " level=" + std::to_string(level) + " exp=" + std::to_string(levelExp) + " previousExp=" + std::to_string(previousExp));
                    }
                    previousExp = levelExp;
                    // The round trip is what everything that DERIVES level from EXP depends on --
                    // PA8, PB7 and PK3 all do, and a level that is one off changes every stat.
                    context.check(Pokemon::getLevelFromExp(levelExp, growthRate) == level, "level derived from a level's own EXP is wrong", "rate=" + std::to_string(growthRate) + " level=" + std::to_string(level) + " exp=" + std::to_string(levelExp) + " got=" + std::to_string(Pokemon::getLevelFromExp(levelExp, growthRate)));
                }
            }
            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                context.check(Pokemon::getGrowthRate(speciesId) < 6, "species has an out-of-range growth rate", speciesLabel(speciesId) + " rate=" + std::to_string(Pokemon::getGrowthRate(speciesId)));
            }
        }

        void checkPersonal(Context &context, uint16_t firstSpeciesId, uint16_t lastSpeciesId)
        {
            for (uint16_t speciesId = firstSpeciesId; speciesId <= lastSpeciesId && speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                const Pokemon::PersonalInfo &basePersonalInfo = Pokemon::getPersonalInfo(speciesId, 0);

                if (!context.check(basePersonalInfo.formCount >= 1, "species has no forms at all", speciesLabel(speciesId))) continue;

                // formIndex is the redirection into the alternate-form half of the flat table.
                // formCount > 1 with formIndex == 0 means every alternate form silently resolves
                // back to form 0 -- same stats, same abilities, same types, no error anywhere.
                context.check((basePersonalInfo.formCount > 1) == (basePersonalInfo.formIndex != 0), "form count and form-table index disagree", speciesLabel(speciesId) + " formCount=" + std::to_string(basePersonalInfo.formCount) + " formIndex=" + std::to_string(basePersonalInfo.formIndex));

                context.check(isCanonicalGenderRatio(basePersonalInfo.genderRatio), "gender ratio is not a canonical value", speciesLabel(speciesId) + " ratio=" + std::to_string(basePersonalInfo.genderRatio));

                for (uint8_t formId = 0; formId < basePersonalInfo.formCount; ++formId)
                {
                    const Pokemon::PersonalInfo &formPersonalInfo = Pokemon::getPersonalInfo(speciesId, formId);
                    const std::string where = speciesLabel(speciesId) + " form=" + std::to_string(formId);

                    context.check(formPersonalInfo.type1 < Names::getTypeCount(), "form has an out-of-range primary type", where + " type1=" + std::to_string(formPersonalInfo.type1));
                    context.check(formPersonalInfo.type2 < Names::getTypeCount() || formPersonalInfo.type2 == Pokemon::TYPE_NONE, "form has an out-of-range secondary type", where + " type2=" + std::to_string(formPersonalInfo.type2));
                    context.check(formPersonalInfo.ability1 != 0, "form has no first ability", where);
                    // Battle-only forms (Mega, Ash-Greninja, Ultra Necrozma) and the Gen 7 totem forms
                    // exist only in battle or on NPCs, so no game can hold one -- every other form must
                    // be in at least one.
                    if (!Pokemon::isBattleOnlyForm(speciesId, formId) && !isTotemForm(speciesId, formId))
                    {
                        context.check(formPersonalInfo.presence != 0, "form is present in no game at all", where);
                    }
                }
            }
        }

        void checkFormNames(Context &context)
        {
            int unnamedFormCount = 0;
            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                const Pokemon::PersonalInfo &basePersonalInfo = Pokemon::getPersonalInfo(speciesId, 0);
                if (basePersonalInfo.formCount <= 1) continue;
                for (uint8_t formId = 1; formId < basePersonalInfo.formCount; ++formId)
                {
                    // An unnamed alternate form displays EXACTLY like form 0 -- getDisplayName falls
                    // through to the bare species name -- so a wrong form index looks like nothing
                    // at all. This is how Castform, Cramorant and Eternatus stayed unnamed.
                    // A failure: every alternate form in the table has a name today, so a new one
                    // without is the same bug again.
                    if (!context.check(isNamed(Names::getFormName(speciesId, formId)), "alternate form has no name", speciesLabel(speciesId) + " form=" + std::to_string(formId) + " formCount=" + std::to_string(basePersonalInfo.formCount)))
                    {
                        ++unnamedFormCount;
                    }
                }
            }
            context.note("alternate forms with no name", "count=" + std::to_string(unnamedFormCount));
        }

        void checkGen1Tables(Context &context)
        {
            // Gen 1's own tables, checked here rather than under the Gen 1 target: they are pure
            // data and need no save, so they get verified even on a console that has no .sav to
            // point at -- which is the common case.
            using namespace Pokemon;
            for (uint16_t speciesId = 1; speciesId <= MAX_SPECIES_GEN1; ++speciesId)
            {
                const uint8_t gen1SpeciesIndex = nationalToG1(speciesId);
                if (!context.check(gen1SpeciesIndex != 0, "Gen 1 species has no internal index", speciesLabel(speciesId))) continue;
                // The Gen 1 species index is arbitrary (Bulbasaur 0x99, Ivysaur 0x09), so a wrong
                // one is a plausible number that reads out as a different Pokemon.
                context.check(g1ToNational(gen1SpeciesIndex) == speciesId, "Gen 1 species index does not round trip", speciesLabel(speciesId) + " internal=" + std::to_string(gen1SpeciesIndex) + " back=" + std::to_string(g1ToNational(gen1SpeciesIndex)));

                const PersonalRecord &baseStats = getPersonalInfo1RBY(speciesId, 0);
                if (!context.check(baseStats.hp != 0, "Gen 1 species has no base-stat row", speciesLabel(speciesId))) continue;
                context.check(baseStats.hp > 0 && baseStats.atk > 0 && baseStats.spa > 0, "Gen 1 base stats are zero", speciesLabel(speciesId));
                // Gen 1's ROM type ids are its own (Bug 7, Ghost 8, specials at 20-26) and are
                // converted at generation time; a type that will not round trip means the two
                // numberings disagree, which draws the wrong type icon.
                context.check(baseStats.type1 != 255, "Gen 1 types are unmapped", speciesLabel(speciesId));
                context.check(g1TypeToPKSE(pkseTypeToG1(baseStats.type1)) == baseStats.type1, "Gen 1 primary type does not round trip", speciesLabel(speciesId));
                // A MONO-TYPED SPECIES REPORTS TYPE_NONE IN SLOT 2, not a repeat of slot 1 -- the
                // table's convention, and what stops the badge row drawing "Normal Normal". The ROM
                // stores the id twice and Pokemon1RBY::setSpecies re-expands the sentinel when it
                // writes a record, so there is nothing here to round trip.
                if (baseStats.type2 != Pokemon::PERSONAL_TYPE_NONE)
                {
                    context.check(g1TypeToPKSE(pkseTypeToG1(baseStats.type2)) == baseStats.type2, "Gen 1 secondary type does not round trip", speciesLabel(speciesId));
                }
                context.check(isCanonicalGenderRatio(baseStats.genderRatio), "Gen 1 substituted gender ratio is not canonical", speciesLabel(speciesId) + " ratio=" + std::to_string(baseStats.genderRatio));
                // Growth rates were checked to be identical to Gen 3, which is why Experience.cpp is
                // reused. If that ever stops being true this is where it shows up.
                context.check(baseStats.growthRate == getGrowthRate(speciesId), "Gen 1 growth rate differs from the shared table", speciesLabel(speciesId) + " gen1=" + std::to_string(baseStats.growthRate) + " shared=" + std::to_string(getGrowthRate(speciesId)));
            }

            context.check(g1TypeToPKSE(6) == 255, "Gen 1 ROM type 6 (the unused Bird type) maps to a real type");

            for (uint16_t moveId = 1; moveId <= MAX_MOVE_GEN1; ++moveId)
            {
                const uint8_t powerPoints = getMovePPGen1(moveId);
                context.check(powerPoints > 0 && powerPoints <= 40, "Gen 1 move PP out of range", "move=" + std::to_string(moveId) + " pp=" + std::to_string(powerPoints));
                // The record's PP field is six bits. Three PP Ups must still fit it.
                context.check(getMovePPGen1WithUps(moveId, 3) <= 63, "Gen 1 PP with three PP Ups overflows six bits", "move=" + std::to_string(moveId) + " pp=" + std::to_string(getMovePPGen1WithUps(moveId, 3)));
            }
        }
    }

    namespace
    {
        /// Every save-format group's ENTITY capabilities, checked against the buffer itself.
        ///
        /// The capability predicates (hasHandler, hasNature, hasBall, ...) exist because a getter
        /// cannot answer "does this format have this field?" -- 0 is a real nature, a real ball and
        /// a real ability, so a zero return means "unset" and "absent" at the same time. The UI
        /// asks the predicate and shows or hides the row on its answer, which makes a WRONG
        /// predicate silent in both directions: report false on a format that has the field and the
        /// row vanishes (a Sword Pokemon traded to Shield showed no handler at all); report true on
        /// one that does not and the row is invented, reading "(none)" forever for something the
        /// game never stored.
        ///
        /// Neither direction is visible in a screenshot, and neither is caught by anything that
        /// looks at one save. So this asks the buffer: write the field, read it back, and require
        /// the predicate to agree with what actually happened -- for all eighteen groups at once,
        /// including the ones no console here has a save for. That is the half of "which titles are
        /// affected" that a per-save suite structurally cannot answer.
        void checkEntityCapabilities(Context &context)
        {
            for (Enums::GameVersion group : Test::ALL_SAVE_GROUPS)
            {
                const std::string groupLabel = "group=" + Enums::getGameVersionName(group);
                const size_t recordSize = Trainer::Bank::recordSizeFor(group);
                std::vector<std::byte> record(recordSize, std::byte{0});
                auto entity = Trainer::Bank::makePokemon(group, record);
                if (!context.check(entity != nullptr, "no entity class for a supported save group", groupLabel)) continue;

                // Terastallization is Scarlet/Violet's alone: PK9 is the only record PKHeX gives an
                // ITeraType, and Legends: Z-A shares the entity layout without sharing the mechanic.
                context.check(entity->hasTeraType() == (group == Enums::GameVersion::SV), "hasTeraType() does not match the one format that has Terastallization", groupLabel + " hasTeraType=" + (entity->hasTeraType() ? "1" : "0"));

                // A FORMAT WITH NO VERSION BYTE MUST STILL NAME ITS OWN GENERATION. Gen 1 and Gen 2 both lack
                // one, so hardcoding "Gen 1" for the pair stamps every Gold, Silver and Crystal pokemon Gen 1 --
                // a wrong answer that reads exactly like a right one. With no save passed there is no title to
                // borrow, so the answer is the generation, and it has to be the ENTITY's.
                const std::string bankStamp = Trainer::originStampLabel(*entity, nullptr);
                context.check(!bankStamp.empty(), "a pokemon has no origin stamp at all", groupLabel);
                if (!entity->hasOriginGame())
                {
                    const char *expectedStamp = (group == Enums::GameVersion::GSC) ? "Gen 2" : "Gen 1";
                    context.check(bankStamp == expectedStamp, "a versionless format's origin stamp names the wrong generation", groupLabel + " stamp=\"" + bankStamp + "\" expected=\"" + expectedStamp + "\"");
                }

                // THE GAMES STAMP NOTHING BEFORE THE KALOS PENTAGON, so Gen 3, 4 and 5 must resolve
                // to None -- a mark there would be PKSE asserting something no game ever showed.
                // Gen 1/2 are the opposite: no version byte, so the CONSOLE is the only knowable
                // answer and it is what Bank, HOME and PKSM all display.
                if (group == Enums::GameVersion::RBY || group == Enums::GameVersion::GSC)
                {
                    context.check(Trainer::originMarkFor(*entity) == Trainer::OriginMark::GameBoy, "a Gen 1/2 pokemon is not marked with the Game Boy", groupLabel);
                }
                if (entity->hasOriginGame())
                {
                    // PKHeX OriginMarkUtil.GetOriginMark, checked against the version bytes PKSE's
                    // own getOriginGameName() names. The pairs that matter are the ones where a
                    // pokemon's FORMAT and its ORIGIN disagree: a Virtual Console, GO or Let's Go
                    // record lives in a Gen 7 format, so testing the generation first would stamp
                    // an Alola clover on every transferred Red/Blue pokemon.
                    struct MarkCase
                    {
                        uint8_t version;
                        Trainer::OriginMark mark;
                        const char *game;
                    };
                    static const MarkCase markCases[] = {
                        {2, Trainer::OriginMark::None, "Ruby"},
                        {4, Trainer::OriginMark::None, "FireRed"},
                        {10, Trainer::OriginMark::None, "Diamond"},
                        {21, Trainer::OriginMark::None, "Black"},
                        {24, Trainer::OriginMark::Gen6Pentagon, "X"},
                        {27, Trainer::OriginMark::Gen6Pentagon, "Omega Ruby"},
                        {30, Trainer::OriginMark::Gen7Clover, "Sun"},
                        {33, Trainer::OriginMark::Gen7Clover, "Ultra Moon"},
                        {34, Trainer::OriginMark::GO, "Pokemon GO"},
                        {35, Trainer::OriginMark::GameBoy, "Red (VC)"},
                        {41, Trainer::OriginMark::GameBoy, "Crystal (VC)"},
                        {42, Trainer::OriginMark::LetsGo, "Let's Go Pikachu"},
                        {44, Trainer::OriginMark::Gen8Galar, "Sword"},
                        {47, Trainer::OriginMark::Gen8Arc, "Legends Arceus"},
                        {48, Trainer::OriginMark::Gen8Trio, "Brilliant Diamond"},
                        {50, Trainer::OriginMark::Gen9Paldea, "Scarlet"},
                        {52, Trainer::OriginMark::Gen9ZA, "Legends Z-A"},
                        {56, Trainer::OriginMark::GameBoyAdvance, "FireRed (Switch)"},
                    };
                    const uint8_t originalOriginGame = entity->originGame();
                    for (const MarkCase &markCase : markCases)
                    {
                        entity->setOriginGame(markCase.version);
                        // this format cannot hold that version; not this test's business
                        if (entity->originGame() != markCase.version) continue;
                        context.check(Trainer::originMarkFor(*entity) == markCase.mark, "a pokemon's origin marking does not match the game it came from", groupLabel + " version=" + std::to_string(markCase.version) + " (" + markCase.game + ")");
                    }
                    entity->setOriginGame(originalOriginGame);
                }

                const std::u16string handlerProbe = u"HT";
                entity->setHTName(handlerProbe);
                const bool storesHandler = (entity->htName() == handlerProbe);
                context.check(storesHandler == entity->hasHandler(), "hasHandler() disagrees with whether the format can store one", groupLabel + " hasHandler=" + (entity->hasHandler() ? "1" : "0") + " stored=" + (storesHandler ? "1" : "0"));
                if (entity->hasHandler())
                {
                    // The flag says who is IN CHARGE, which is what decides whose friendship,
                    // affection and memories the game reads -- and what the details view labels
                    // "HT (active)". A no-op setter leaves every traded pokemon reading as OT-handled.
                    entity->setCurrentHandler(1);
                    context.check(entity->currentHandler() == 1, "the current-handler flag does not round trip", groupLabel);
                    entity->setCurrentHandler(0);
                    context.check(entity->currentHandler() == 0, "the current-handler flag could not be set back to the OT", groupLabel);
                }

                entity->setHeldItem(1);
                context.check((entity->heldItem() == 1) == entity->hasHeldItem(), "hasHeldItem() disagrees with whether the format can store one", groupLabel + " hasHeldItem=" + (entity->hasHeldItem() ? "1" : "0") + " read=" + std::to_string(entity->heldItem()));

                entity->setBall(4);
                context.check((entity->ball() == 4) == entity->hasBall(), "hasBall() disagrees with whether the format can store one", groupLabel + " hasBall=" + (entity->hasBall() ? "1" : "0") + " read=" + std::to_string(entity->ball()));

                entity->setOTFriendship(123);
                context.check((entity->otFriendship() == 123) == entity->hasFriendship(), "hasFriendship() disagrees with whether the format can store one", groupLabel + " hasFriendship=" + (entity->hasFriendship() ? "1" : "0") + " read=" + std::to_string(entity->otFriendship()));
            }
        }

        /// Egg SOURCES must be reachable, not merely nameable.
        ///
        /// "in BDSP, egg met location is 'Nursery Couple' and I cannot find that in the list" -- and
        /// the reporter was right. The name was in the table all along, so a hatched egg DISPLAYED
        /// correctly; what was missing was any way to select it, because the egg picker enumerated
        /// bank 0 (places) and egg sources live in bank 6 (people). This checks the property that
        /// was actually broken: every origin that names an egg source can also offer it.
        void checkEggSourceLocations(Context &context)
        {
            // The origins with a bank-6 table. Gen 3, Gen 4 (which banks its ids differently) and
            // Let's Go have none, and are expected to report an empty table rather than junk.
            static const uint8_t originsWithEggSources[] = {
                20, 21, 22, 23, 24, 25, 26, 27, 30, 31, 32, 33, 44, 45, 47, 48, 49, 50, 51, 52};
            for (uint8_t originVersion : originsWithEggSources)
            {
                const Names::LocationTable eggSources = Names::getEggSourceLocationTable(originVersion);
                if (!context.check(eggSources.count > 0, "an origin that should name egg sources has none", "origin=" + std::to_string(static_cast<int>(originVersion)))) continue;
                int namedSourceCount = 0;
                for (uint16_t sourceIndex = 0; sourceIndex < eggSources.count; ++sourceIndex)
                {
                    if (eggSources.names[sourceIndex] == nullptr || eggSources.names[sourceIndex][0] == '\0') continue;
                    ++namedSourceCount;
                    // The id the picker offers must resolve back to the same name the picker drew,
                    // or selecting a row writes a value that displays as something else.
                    const uint16_t locationId = static_cast<uint16_t>(Names::EGG_SOURCE_LOCATION_BASE + sourceIndex);
                    const char *resolvedName = Names::getMetLocationName(originVersion, locationId);
                    context.check(resolvedName != nullptr && std::string(resolvedName) == eggSources.names[sourceIndex], "an egg-source id does not resolve back to its own name", "origin=" + std::to_string(static_cast<int>(originVersion)) + " id=" + std::to_string(locationId) + " table=\"" + eggSources.names[sourceIndex] + "\"" + " resolved=\"" + (resolvedName ? resolvedName : "") + "\"");
                }
                context.check(namedSourceCount > 0, "an origin's egg-source table is entirely blank", "origin=" + std::to_string(static_cast<int>(originVersion)));
            }

            // The exact case that was reported, pinned so it cannot regress quietly.
            for (uint8_t brilliantDiamondOrShiningPearl : {48, 49})
            {
                const Names::LocationTable eggSources = Names::getEggSourceLocationTable(static_cast<uint8_t>(brilliantDiamondOrShiningPearl));
                bool offersNurseryCouple = false;
                for (uint16_t sourceIndex = 0; sourceIndex < eggSources.count; ++sourceIndex)
                {
                    if (eggSources.names[sourceIndex] && std::string(eggSources.names[sourceIndex]) == "Nursery Couple")
                    {
                        offersNurseryCouple = true;
                    }
                }
                context.check(offersNurseryCouple, "BDSP no longer offers \"Nursery Couple\" as an egg source", "origin=" + std::to_string(brilliantDiamondOrShiningPearl));
            }
        }
    }

    /**
     * NO TWO TITLE IDS MAY SHARE A BACKUP FOLDER.
     *
     * That is the whole invariant, and it broke as soon as Gen 3's per-language SKUs were listed:
     * six ids resolving to two GameVersions, a folder built from the GameVersion, and therefore one
     * directory for three different titles. Opening the French LeafGreen loaded whatever English
     * bytes were already in it, and saving with inject on would have written them into the French
     * title. Nothing failed, nothing logged, and the two saves are the same format so nothing looked
     * wrong.
     *
     * Asserted over PER_LANGUAGE_TITLES because that is the only place ids collapse onto one name --
     * every other Pokemon title is one game with one id and a name nobody else has. The labels are
     * checked too: identical tiles are how the user picks the wrong one in the first place.
     */
    void checkTitleFolders(Context &context)
    {
        for (const Enums::PerLanguageTitle &title : Enums::PER_LANGUAGE_TITLES)
        {
            const std::string where = Save::titleIdText(title.titleId);
            // The table carries the id -> version mapping AND the id -> language mapping so the two
            // cannot drift. Check the version reader actually answers from it.
            context.check(Enums::getGameVersion(title.titleId) == title.version, "a per-language title id does not resolve to its own row's game", where);
            context.check(Enums::getTitleLanguage(title.titleId) == title.language, "a per-language title id does not resolve to its own row's language", where);
            context.check(title.language != 0, "a per-language title has no language, so it cannot be told from its siblings", where);
        }

        for (const Enums::PerLanguageTitle &title : Enums::PER_LANGUAGE_TITLES)
        {
            for (const Enums::PerLanguageTitle &otherTitle : Enums::PER_LANGUAGE_TITLES)
            {
                if (otherTitle.titleId == title.titleId) continue;
                const std::string where = Save::titleIdText(title.titleId) + " vs " + Save::titleIdText(otherTitle.titleId);
                context.check(Save::titleFolderName(title.titleId, title.version) != Save::titleFolderName(otherTitle.titleId, otherTitle.version), "two title ids share one backup folder", where + " -> " + Save::titleFolderName(title.titleId, title.version));
                context.check(Save::titleDisplayLabel(title.titleId, title.version) != Save::titleDisplayLabel(otherTitle.titleId, otherTitle.version), "two titles are drawn with the same label, so the picker cannot tell them apart", where + " -> " + Save::titleDisplayLabel(title.titleId, title.version));
            }
        }

        // AN ID PKSE HAS NEVER SEEN still needs a folder of its own. It is identified by CONTENT,
        // which answers with a GROUP, so several unrecognised SKUs of one generation arrive with
        // the same name -- the id is what separates them. Two invented ids, because the real ones
        // for the Italian, German and Japanese SKUs are not in the table yet and this has to hold
        // for whatever turns up.
        {
            const uint64_t firstUnknownTitleId = 0x0100AAA0BBBB0000ull;
            const uint64_t secondUnknownTitleId = 0x0100CCC0DDDD0000ull;
            const Enums::GameVersion contentGroup = Enums::GameVersion::FRLG;
            context.check(Enums::getGameVersion(firstUnknownTitleId) == Enums::GameVersion::Invalid, "the unknown-title-id check is using an id PKSE actually knows", Save::titleIdText(firstUnknownTitleId));
            context.check(Save::titleFolderName(firstUnknownTitleId, contentGroup) != Save::titleFolderName(secondUnknownTitleId, contentGroup), "two unrecognised title ids share one backup folder", Save::titleFolderName(firstUnknownTitleId, contentGroup));
            context.check(Save::titleDisplayLabel(firstUnknownTitleId, contentGroup) != Save::titleDisplayLabel(secondUnknownTitleId, contentGroup), "two unrecognised titles are drawn with the same label", Save::titleDisplayLabel(firstUnknownTitleId, contentGroup));
            // And it must not collide with a title whose id IS known, either.
            context.check(Save::titleFolderName(firstUnknownTitleId, contentGroup) != Save::titleFolderName(Enums::PER_LANGUAGE_TITLES[0].titleId, Enums::PER_LANGUAGE_TITLES[0].version), "an unrecognised title id shares a folder with a known one", Save::titleFolderName(firstUnknownTitleId, contentGroup));
        }
    }

    /**
     * THE MIGRATION HAS TO MOVE SOMETHING, AND MUST NOT MOVE THE WRONG THING.
     *
     * Renaming backup folders to carry the title id orphans every backup already on the card, on
     * every user's console -- so PKSE moves them. It can only do that where the old name identifies
     * one title: `Pokemon Shield` was Shield's and nothing else's, while `Pokemon LeafGreen` on a
     * console with two LeafGreen SKUs could have been either, and the saves are the same format so
     * nothing in the bytes settles it. Those are refused rather than guessed.
     *
     * Both halves are checked, because each fails in a way the other hides: a migration that moves
     * nothing leaves every user's history invisible, and one that moves too much files somebody's
     * French backups under English.
     */
    void checkLegacyFolderMigration(Context &context)
    {
        // There is something to move at all: the old and new names must differ for every title, or
        // the migration is renaming folders onto themselves.
        for (const Enums::PerLanguageTitle &title : Enums::PER_LANGUAGE_TITLES)
        {
            context.check(Save::legacyTitleFolderName(title.version) != Save::titleFolderName(title.titleId, title.version), "a title's legacy folder name equals its new one, so nothing would migrate", Save::titleIdText(title.titleId) + " -> " + Save::legacyTitleFolderName(title.version));
        }

        // ONE SKU INSTALLED -> UNAMBIGUOUS. A user who owns only English LeafGreen has a clean
        // folder and must get it back; refusing here would punish the common case for a collision
        // that cannot have happened on their card.
        {
            const Save::InstalledTitle onlyEnglish{Enums::PER_LANGUAGE_TITLES[1].titleId,
                                                   Enums::PER_LANGUAGE_TITLES[1].version};
            const std::vector<Save::InstalledTitle> installedTitles{onlyEnglish};
            context.check(!Save::legacyFolderIsAmbiguous(onlyEnglish, installedTitles), "a legacy folder was refused although only one installed title could have made it", Save::legacyTitleFolderName(onlyEnglish.gameVersion));
        }

        // TWO SKUS OF ONE GAME INSTALLED -> AMBIGUOUS. This is the case that caused the bug, and the
        // one where a guess would file somebody's French backups under English.
        {
            const Save::InstalledTitle english{Enums::PER_LANGUAGE_TITLES[1].titleId,
                                               Enums::PER_LANGUAGE_TITLES[1].version};
            const Save::InstalledTitle french{Enums::PER_LANGUAGE_TITLES[2].titleId,
                                              Enums::PER_LANGUAGE_TITLES[2].version};
            context.check(Save::legacyTitleFolderName(english.gameVersion) == Save::legacyTitleFolderName(french.gameVersion), "two SKUs of one game no longer share a legacy folder name, so this check is moot", Save::legacyTitleFolderName(english.gameVersion));
            const std::vector<Save::InstalledTitle> installedTitles{english, french};
            context.check(Save::legacyFolderIsAmbiguous(english, installedTitles) && Save::legacyFolderIsAmbiguous(french, installedTitles), "a legacy folder shared by two installed SKUs was not refused as ambiguous", Save::legacyTitleFolderName(english.gameVersion));
        }

        // A DIFFERENT GAME DOES NOT MAKE IT AMBIGUOUS. FireRed and LeafGreen have different names,
        // so owning one of each leaves both unambiguous -- the refusal must key on the NAME, not on
        // "this console has more than one Gen 3 title".
        {
            const Save::InstalledTitle fireRed{Enums::PER_LANGUAGE_TITLES[1].titleId,
                                               Enums::PER_LANGUAGE_TITLES[1].version};
            const Save::InstalledTitle leafGreen{Enums::PER_LANGUAGE_TITLES[7].titleId,
                                                 Enums::PER_LANGUAGE_TITLES[7].version};
            context.check(fireRed.gameVersion != leafGreen.gameVersion, "the cross-game migration check is using two SKUs of the same game", Save::titleIdText(fireRed.titleId));
            const std::vector<Save::InstalledTitle> installedTitles{fireRed, leafGreen};
            context.check(!Save::legacyFolderIsAmbiguous(fireRed, installedTitles) && !Save::legacyFolderIsAmbiguous(leafGreen, installedTitles), "owning one SKU of each game wrongly made both legacy folders ambiguous", Save::legacyTitleFolderName(fireRed.gameVersion));
        }
    }

    /**
     * EVERY TESTED TITLE MUST RESOLVE TO A SAVE-FORMAT GROUP.
     *
     * getGameGroup() is a switch with a `default: return Invalid`, so a title nobody added a case
     * for does not fail — it quietly answers "no such group". That is the exact shape CLAUDE.md
     * warns about: a missing lookup read as a definitive negative.
     *
     * It cost Gen 1 its entire self-test coverage. The runner accepts a file only when
     * getGameGroup(folderTitle) matches the group the save parsed as, and Red, Green, Blue and
     * Yellow had no case at all — so every Gen 1 save sitting correctly in its own title folder
     * was REFUSED, and told the user "sav.dat is a Red/Blue/Yellow save, not Red": the group name,
     * reported as though it were some other game. Nothing else in the run would have said so,
     * because a refused save and an absent one both end as "this title has no save".
     *
     * The check is one line and belongs here rather than in the runner: it needs no save, no
     * console and no folder, so it answers on the host in microseconds and covers every title at
     * once — including whichever one is added next.
     */
    void checkTitleRouting(Context &context)
    {
        for (const Enums::GameVersion title : TESTED_TITLES)
        {
            const Enums::GameVersion group = Enums::getGameGroup(title);
            const std::string where = "title=" + Enums::getGameVersionName(title) + " (" + std::to_string(static_cast<int>(title)) + ")";
            if (!context.check(group != Enums::GameVersion::Invalid, "a tested title does not resolve to any save-format group", where)) continue;
            // A group must also NAME itself. The runner prints this string when it refuses a
            // misfiled save, so an unnamed group produces "is a Unknown save, not Red".
            context.check(!Enums::getGameVersionName(group).empty() && Enums::getGameVersionName(group) != "Unknown", "a tested title's group has no name", where);
            // And the title itself must name itself, because that is the folder the user is told
            // to create and the string the plan row prints.
            context.check(!Enums::getGameVersionName(title).empty() && Enums::getGameVersionName(title) != "Unknown", "a tested title has no name of its own", where);
            // A plain game name: renaming BU to PKHeX's "Blue (JP)" made the Blue row look for a
            // "Blue (JP)" folder, and the user's Blue save silently stopped being tested.
            const std::string titleName = Enums::getGameVersionName(title);
            context.check(titleName.find_first_of("/()") == std::string::npos, "a tested title's name is not a plain folder name", where);
        }
        context.check(Enums::getGameVersionName(Enums::GameVersion::BU) == "Blue", "the Blue title is not named \"Blue\", so its save folder moved", Enums::getGameVersionName(Enums::GameVersion::BU));
    }

    /**
     * THE TERA TYPE IS NOT A TYPE ID AND THIS IS WHERE THAT IS PINNED. Scarlet/Violet store Stellar
     * as 99 and "never overridden" as 19, so the obvious reading -- treat both bytes as type ids --
     * names a Pokemon's Tera type after whatever string sits at index 19, silently and plausibly.
     * The truth table below is PKHeX's TeraTypeUtil.GetTeraType; nothing else in PKSE re-derives it.
     */
    void checkTeraTypes(Context &context)
    {
        using namespace Pokemon;
        for (uint8_t typeId = 0; typeId <= TYPE_FAIRY; ++typeId)
        {
            const std::string where = "type=" + std::to_string(typeId);
            // Never overridden -> the original stands.
            context.check(resolveTeraType(typeId, TERA_TYPE_OVERRIDE_NONE) == typeId, "an un-overridden Tera type did not resolve to the original", where);
            // A Tera Shard override wins outright, whatever the original was.
            context.check(resolveTeraType(TYPE_NORMAL, typeId) == typeId, "a Tera type override did not win over the original", where);
            // A type id names itself in the string table.
            context.check(teraTypeNameIndex(typeId) == typeId, "a Tera type id did not map to its own name index", where);
        }
        // Stellar survives from either byte, and lands on the NAME index rather than its stored value.
        context.check(resolveTeraType(TERA_TYPE_STELLAR, TERA_TYPE_OVERRIDE_NONE) == TERA_TYPE_STELLAR, "a Stellar original did not resolve to Stellar", "original=99");
        context.check(resolveTeraType(TYPE_NORMAL, TERA_TYPE_STELLAR) == TERA_TYPE_STELLAR, "a Stellar override did not resolve to Stellar", "override=99");
        context.check(teraTypeNameIndex(TERA_TYPE_STELLAR) == TERA_TYPE_NAME_INDEX_STELLAR, "Stellar did not map to its name index", "stored=99 expected-index=18");
        context.check(TERA_TYPE_NAME_INDEX_STELLAR < Names::getTypeNameCount(), "the Stellar name index is outside the type name table", "index=18");
        context.check(std::string(Names::getTypeName(TERA_TYPE_NAME_INDEX_STELLAR)) != "???", "the type name table has no entry for Stellar", "index=18");
        // 18 is a NAME index, never a stored value: as an override it is out of range -> Normal.
        context.check(resolveTeraType(TYPE_NORMAL, TERA_TYPE_NAME_INDEX_STELLAR) == TYPE_NORMAL, "an out-of-range Tera override did not fall back to Normal", "override=18");
        // The two counts answer different questions and must not have converged.
        context.check(Names::getTypeCount() == 18 && Names::getTypeNameCount() == 19, "the species type count and the type NAME count are no longer distinct", "types=" + std::to_string(Names::getTypeCount()) + " names=" + std::to_string(Names::getTypeNameCount()));
    }

    /**
     * The per-game TRADE EVOLUTION table, and the action it gates.
     *
     * Two things are checked, because the table being well-formed says nothing about the action
     * doing the right thing with it.
     *
     * THE TABLE. Every row has to name a species+form that exists in that game, in that game's own
     * id spaces -- a held item id is the GAME'S (Metal Coat is 199 in Gen 3 and 233 from Gen 4 on),
     * so a row carrying the wrong generation's id still looks like a number and would silently gate
     * on an item the player can never hold. The lookup must also return rows for the key it was
     * asked about, which is what catches a binary search landing one row off.
     *
     * THE ACTION. A blank entity is evolved and the result inspected: the species becomes the
     * destination, the held item is CONSUMED for a held-item trade, and **total EXP does not
     * move** -- the one thing separating this from the species picker, which re-derives EXP from
     * the level because an arbitrary swap can cross growth rates. Every trade-evolution pair shares
     * a growth rate with its pre-evolution, so preserving EXP is both correct and checkable.
     */
    void checkTradeEvolutions(Context &context)
    {
        using namespace Pokemon;
        constexpr uint16_t kadabraSpeciesId = 64;
        constexpr uint16_t alakazamSpeciesId = 65;
        constexpr uint16_t scytherSpeciesId = 123;
        constexpr uint16_t scizorSpeciesId = 212;
        constexpr uint16_t clamperlSpeciesId = 366;

        for (Enums::GameVersion group : Test::ALL_SAVE_GROUPS)
        {
            const std::string groupLabel = "group=" + Enums::getGameVersionName(group);
            int groupRowCount = 0;

            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                for (uint8_t formId = 0; formId < 4; ++formId)
                {
                    TradeEvolution tradeRows[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
                    const int rowCount = getTradeEvolutions(group, speciesId, formId, tradeRows, TRADE_EVOLUTIONS_MAX_PER_SPECIES);
                    groupRowCount += rowCount;
                    for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
                    {
                        const TradeEvolution &row = tradeRows[rowIndex];
                        const std::string where = groupLabel + " species=" + std::to_string(speciesId) + " form=" + std::to_string(formId);
                        context.check(row.speciesId == speciesId && row.formId == formId, "a trade evolution lookup returned a row for a different species/form", where + " row=" + std::to_string(row.speciesId) + "/" + std::to_string(row.formId));
                        context.check(row.destinationSpeciesId != 0 && row.destinationSpeciesId <= Pokemon::PERSONAL_MAX_SPECIES, "a trade evolution names a destination species outside the dex", where + " destination=" + std::to_string(row.destinationSpeciesId));
                        context.check(row.destinationSpeciesId != speciesId, "a trade evolution evolves a species into itself", where);
                        // THE FORWARD AND REVERSE TABLES MUST AGREE ABOUT THE SAME EDGE. Both are generated from one
                        // binary, so a disagreement means one of them addressed it with the wrong personal table -- which
                        // reads as the Sun/Moon reverse lineage saying Alolan Golem evolved from Greninja while the trade
                        // row correctly says Alolan Graveler. Every alternate form in that game is wrong and nothing
                        // anywhere says so.
                        uint16_t reverseFromSpeciesId = 0;
                        uint8_t reverseFromFormId = 0;
                        const bool hasReverseEdge = getPreEvolution(group, row.destinationSpeciesId, row.destinationFormId, reverseFromSpeciesId, reverseFromFormId);
                        context.check(hasReverseEdge && reverseFromSpeciesId == speciesId && reverseFromFormId == formId, "the reverse lineage disagrees with a trade evolution about the same edge", where + " destination=" + std::to_string(row.destinationSpeciesId) + "/" + std::to_string(row.destinationFormId) + " reverse-says=" + std::to_string(reverseFromSpeciesId) + "/" + std::to_string(reverseFromFormId));
                        if (row.trigger == TradeEvolutionTrigger::HeldItem)
                        {
                            // The id has to name something in THIS game's item table. A Gen 3 id
                            // read in Gen 4's space is still a number, and still gates the button.
                            context.check(row.requiredItemId != 0, "a held-item trade evolution requires no item", where);
                            const char *itemName = Names::getItemNameFor(group, row.requiredItemId);
                            context.check(itemName != nullptr && itemName[0] != '\0', "a trade evolution's required item has no name in this game", where + " item=" + std::to_string(row.requiredItemId));
                        }
                        else
                        {
                            context.check(row.requiredItemId == 0, "a trade evolution that needs no item carries an item id", where);
                        }
                        if (row.trigger == TradeEvolutionTrigger::PartnerSpecies)
                        {
                            // Karrablast and Shelmet are traded FOR each other, so each one's
                            // partner row has to point back at it.
                            TradeEvolution partnerRows[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
                            const int partnerRowCount = getTradeEvolutions(group, row.partnerSpeciesId, 0, partnerRows, TRADE_EVOLUTIONS_MAX_PER_SPECIES);
                            context.check(partnerRowCount == 1 && partnerRows[0].partnerSpeciesId == speciesId, "a partner-species trade evolution is not reciprocated", where + " partner=" + std::to_string(row.partnerSpeciesId));
                        }
                        else
                        {
                            context.check(row.partnerSpeciesId == 0, "a trade evolution that needs no partner carries a partner species", where);
                        }
                    }
                }
            }
            // Every group PKSE supports has trade evolutions -- there is no generation without
            // them -- so an empty table means the group fell out of the generator's list.
            context.check(groupRowCount > 0, "a save group has no trade evolutions at all", groupLabel);

            const size_t recordSize = Trainer::Bank::recordSizeFor(group);
            std::vector<std::byte> record(recordSize, std::byte{0});
            auto entity = Trainer::Bank::makePokemon(group, record);
            if (!context.check(entity != nullptr, "no entity class for a supported save group", groupLabel)) continue;

            TradeEvolution kadabraRows[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
            if (getTradeEvolutions(group, kadabraSpeciesId, 0, kadabraRows, TRADE_EVOLUTIONS_MAX_PER_SPECIES) == 1)
            {
                entity->setSpecies(kadabraSpeciesId);
                entity->setForm(0);
                entity->setLevel(30);
                const uint32_t experienceBefore = entity->exp();
                context.check(!applyTradeEvolution(*entity, 1), "a trade evolution ran for a candidate the species does not have", groupLabel);
                context.check(entity->speciesID() == kadabraSpeciesId, "an out-of-range trade evolution changed the species anyway", groupLabel);
                context.check(applyTradeEvolution(*entity, 0), "an unconditional trade evolution was refused", groupLabel);
                context.check(entity->speciesID() == alakazamSpeciesId, "a trade evolution did not change the species", groupLabel + " species=" + std::to_string(entity->speciesID()));
                context.check(entity->exp() == experienceBefore, "a trade evolution moved the pokemon's EXP", groupLabel + " before=" + std::to_string(experienceBefore) + " after=" + std::to_string(entity->exp()));
            }
            else
            {
                context.skip("Kadabra has no single trade evolution in this game, so the no-item trade path was not checked", groupLabel);
            }

            TradeEvolution scytherRows[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
            if (getTradeEvolutions(group, scytherSpeciesId, 0, scytherRows, TRADE_EVOLUTIONS_MAX_PER_SPECIES) == 1 && scytherRows[0].trigger == TradeEvolutionTrigger::HeldItem && entity->hasHeldItem())
            {
                // THE ITEM IS NOT A GATE. Holding something else entirely still evolves, and the
                // something else survives -- consuming whatever happens to be in the field would
                // destroy an unrelated item every time.
                const uint16_t unrelatedItemId = static_cast<uint16_t>(scytherRows[0].requiredItemId + 1);
                entity->setSpecies(scytherSpeciesId);
                entity->setForm(0);
                entity->setHeldItem(unrelatedItemId);
                context.check(applyTradeEvolution(*entity, 0), "a held-item trade evolution was refused for want of the item", groupLabel);
                context.check(entity->speciesID() == scizorSpeciesId, "a trade evolution without the item did not change the species", groupLabel + " species=" + std::to_string(entity->speciesID()));
                context.check(entity->heldItem() == unrelatedItemId, "a trade evolution consumed an item its row does not ask for", groupLabel + " held=" + std::to_string(entity->heldItem()));

                entity->setSpecies(scytherSpeciesId);
                entity->setForm(0);
                entity->setHeldItem(scytherRows[0].requiredItemId);
                context.check(getTradeEvolutionOffer(*entity).heldItemIndex == 0, "a held item did not name the row that asks for it", groupLabel + " item=" + std::to_string(scytherRows[0].requiredItemId));
                context.check(applyTradeEvolution(*entity, 0), "a held-item trade evolution was refused while holding the item", groupLabel + " item=" + std::to_string(scytherRows[0].requiredItemId));
                context.check(entity->speciesID() == scizorSpeciesId, "a held-item trade evolution did not change the species", groupLabel + " species=" + std::to_string(entity->speciesID()));
                context.check(entity->heldItem() == 0, "a held-item trade evolution did not consume the item", groupLabel + " held=" + std::to_string(entity->heldItem()));
            }
            else
            {
                context.skip("Scyther has no held-item trade evolution in this game, so the held-item trade path was not checked", groupLabel);
            }

            // THE ONE SPECIES WHOSE ITEM CHOOSES RATHER THAN PERMITS. Clamperl's two rows lead to
            // different Pokemon, so with neither item held the offer must REFUSE to answer -- that
            // -1 is what sends the UI to ask instead of silently picking Huntail.
            TradeEvolution clamperlRows[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
            if (getTradeEvolutions(group, clamperlSpeciesId, 0, clamperlRows, TRADE_EVOLUTIONS_MAX_PER_SPECIES) == 2 && entity->hasHeldItem())
            {
                context.check(clamperlRows[0].destinationSpeciesId != clamperlRows[1].destinationSpeciesId, "two trade evolutions of one species lead to the same place", groupLabel);
                entity->setSpecies(clamperlSpeciesId);
                entity->setForm(0);
                entity->setHeldItem(0);
                context.check(getTradeEvolutionOffer(*entity).resolvedIndex() < 0, "an ambiguous trade evolution resolved itself instead of asking", groupLabel);

                for (int candidateIndex = 0; candidateIndex < 2; ++candidateIndex)
                {
                    entity->setSpecies(clamperlSpeciesId);
                    entity->setForm(0);
                    entity->setHeldItem(clamperlRows[candidateIndex].requiredItemId);
                    const TradeEvolutionOffer heldOffer = getTradeEvolutionOffer(*entity);
                    context.check(heldOffer.resolvedIndex() == candidateIndex, "a held item chose the wrong one of two trade evolutions", groupLabel + " item=" + std::to_string(clamperlRows[candidateIndex].requiredItemId) + " chose=" + std::to_string(heldOffer.resolvedIndex()));
                    context.check(applyTradeEvolution(*entity, candidateIndex) && entity->speciesID() == clamperlRows[candidateIndex].destinationSpeciesId, "a chosen trade evolution landed on the other destination", groupLabel + " species=" + std::to_string(entity->speciesID()));
                }
            }
            else
            {
                context.skip("Clamperl has no pair of trade evolutions in this game, so the item-chooses-the-result path was not checked", groupLabel);
            }
        }
    }

    /**
     * What a TRADE leaves in the handler block, per format.
     *
     * Checked as BEHAVIOUR rather than against a list of game groups, because the three trade
     * behaviours are chosen by asking the entity what it can store -- so testing them against the
     * same capability predicates would prove only that the code agrees with itself. Every
     * assertion below is a statement about what the games do.
     */
    void checkHandlerTrade(Context &context)
    {
        using namespace Pokemon;
        constexpr uint16_t pikachuSpeciesId = 25;
        constexpr uint8_t probeFriendship = 123; // distinctive: no species has this as its base
        constexpr uint16_t returnMoveId = 216;
        const std::u16string partnerName = u"Donor";

        for (Enums::GameVersion group : Test::ALL_SAVE_GROUPS)
        {
            const std::string groupLabel = "group=" + Enums::getGameVersionName(group);
            const size_t recordSize = Trainer::Bank::recordSizeFor(group);
            std::vector<std::byte> record(recordSize, std::byte{0});
            auto entity = Trainer::Bank::makePokemon(group, record);
            if (!context.check(entity != nullptr, "no entity class for a supported save group", groupLabel)) continue;
            entity->setSpecies(pikachuSpeciesId);
            entity->setForm(0);
            entity->setLevel(20);
            entity->setFriendship(probeFriendship);

            // A "BLANK" ENTITY IS NOT ZEROS. Bank::makePokemon hands the constructor an all-zero
            // buffer and every constructor DECRYPTS what it is handed, so the record arrives full of
            // pseudo-random plaintext -- a Gen 6 one reads back a handler memory of 175. That is
            // realistic rather than a nuisance (a real record always holds something), but it means
            // the memory has to be cleared before testing the write, because the writer deliberately
            // refuses to overwrite one that already exists. Both branches are tested below.
            if (entity->hasHandlerMemories())
            {
                entity->setHTMemory(0);
            }

            TradePartner partner;
            partner.trainerName = partnerName;
            partner.gender = 1;
            partner.language = static_cast<uint8_t>(Enums::LanguageID::French);
            partner.consoleCountry = 0x31;
            partner.consoleRegion = 0x07;

            // Only Gen 6 writes a trade memory and geolocation (PKHeX PK6.TradeHT). Gen 7 stores both
            // fields and writes neither (PK7.TradeHT), so there they must come through untouched.
            const bool writesTradeHistory = entity->hasGeolocation() && !entity->hasHyperTraining();
            const uint8_t memoryBeforeTrade = entity->hasHandlerMemories() ? entity->htMemory() : 0;
            const uint8_t countryBeforeTrade = entity->hasGeolocation() ? entity->geolocationCountry(0) : 0;
            const bool tradeApplied = applyTradeRoundTrip(*entity, partner);

            // A FORMAT WITH NO HANDLER MUST REFUSE. Before Gen 6 a trade writes nothing at all, so
            // silently "succeeding" would claim an edit that did not happen.
            context.check(tradeApplied == entity->hasHandler(), "applyTradeRoundTrip disagrees with whether the format has a handler", groupLabel + " applied=" + (tradeApplied ? "1" : "0") + " hasHandler=" + (entity->hasHandler() ? "1" : "0"));
            if (!entity->hasHandler()) continue;

            context.check(entity->htName() == partnerName, "the trade did not store the partner's name", groupLabel);
            context.check(entity->htGender() == 1, "the trade did not store the partner's gender", groupLabel + " gender=" + std::to_string(entity->htGender()));

            // THE ROUND TRIP ENDS WITH THE POKEMON BACK WITH ITS ORIGINAL TRAINER -- and the handler
            // block left populated. Both halves matter: handler 1 would mean it is still sitting in
            // the partner's box, and a cleared name would erase the only evidence the trade happened,
            // which is the thing a trade evolution is checked against.
            context.check(entity->currentHandler() == 0, "the trade back did not return the Pokemon to its OT", groupLabel + " handler=" + std::to_string(entity->currentHandler()));
            context.check(!entity->htName().empty(), "the trade back cleared the handler block", groupLabel);

            if (entity->hasHandlerLanguage())
            {
                const uint8_t expectedLanguage = Enums::safeLanguageForGroup(group, static_cast<uint8_t>(Enums::LanguageID::French));
                context.check(entity->htLanguage() == expectedLanguage, "the trade did not store the partner's language", groupLabel + " read=" + std::to_string(entity->htLanguage()) + " expected=" + std::to_string(expectedLanguage));
            }

            // THE NEW HANDLER STARTS AT THE SPECIES' BASE FRIENDSHIP, except where a visible stat is
            // computed from friendship and a reset would move it.
            const uint8_t baseFriendship = getPersonalRecord(group, pikachuSpeciesId, 0).baseFriendship;
            const bool copiesFriendship = !entity->hasHandlerLanguage() && !entity->hasGeolocation();
            if (copiesFriendship)
            {
                context.check(entity->htFriendship() == probeFriendship, "a format that must copy friendship on trade reset it to base instead", groupLabel + " read=" + std::to_string(entity->htFriendship()));
            }
            else
            {
                context.check(entity->htFriendship() == baseFriendship, "the trade did not reset the handler friendship to the species base", groupLabel + " read=" + std::to_string(entity->htFriendship()) + " base=" + std::to_string(baseFriendship));
            }

            if (entity->hasHandlerMemories())
            {
                if (entity->hasGeolocation() && !writesTradeHistory)
                {
                    context.check(entity->htMemory() == memoryBeforeTrade, "a Gen 7 trade wrote a handler memory", groupLabel + " memory=" + std::to_string(entity->htMemory()) + " before=" + std::to_string(memoryBeforeTrade));
                }
                else if (entity->hasGeolocation())
                {
                    context.check(entity->htMemory() == 4 && entity->htMemoryVariable() == 9 && entity->htMemoryIntensity() == 1, "the trade did not write a link-trade memory", groupLabel + " memory=" + std::to_string(entity->htMemory()) + " variable=" + std::to_string(entity->htMemoryVariable()) + " intensity=" + std::to_string(entity->htMemoryIntensity()));
                }
                else
                {
                    context.check(entity->htMemory() == 0, "a format that defers memories to the game had one written anyway", groupLabel + " memory=" + std::to_string(entity->htMemory()));
                }
            }

            if (entity->hasGeolocation() && !writesTradeHistory)
            {
                context.check(entity->geolocationCountry(0) == countryBeforeTrade, "a Gen 7 trade pushed the partner's console region", groupLabel + " country=" + std::to_string(entity->geolocationCountry(0)) + " before=" + std::to_string(countryBeforeTrade));
            }
            if (writesTradeHistory)
            {
                context.check(entity->geolocationCountry(0) == 0x31 && entity->geolocationRegion(0) == 0x07, "the trade did not record the partner's console region", groupLabel + " country=" + std::to_string(entity->geolocationCountry(0)) + " region=" + std::to_string(entity->geolocationRegion(0)));

                // A SECOND TRADE PUSHES THE FIRST DOWN. Five slots exist because the games keep the
                // chain; a writer that only ever set slot 0 would pass every single-trade test.
                TradePartner secondPartner = partner;
                secondPartner.trainerName = u"Second";
                secondPartner.consoleCountry = 0x42;
                secondPartner.consoleRegion = 0x09;
                applyTradeRoundTrip(*entity, secondPartner);
                context.check(entity->geolocationCountry(0) == 0x42 && entity->geolocationCountry(1) == 0x31 && entity->geolocationRegion(1) == 0x07, "a second trade did not push the previous handler's region down a slot", groupLabel + " slot0=" + std::to_string(entity->geolocationCountry(0)) + " slot1=" + std::to_string(entity->geolocationCountry(1)));

                // A PARTNER WITH NO CONSOLE REGION MUST NOT DISTURB THE HISTORY. The slots are read
                // in order and a non-empty one after an empty one is invalid, so pushing a blank
                // entry would shift real entries down behind a gap and turn a legal record illegal.
                {
                    std::vector<std::byte> geolocationRecord(recordSize, std::byte{0});
                    auto geolocationHolder = Trainer::Bank::makePokemon(group, geolocationRecord);
                    if (geolocationHolder != nullptr)
                    {
                        geolocationHolder->setSpecies(pikachuSpeciesId);
                        geolocationHolder->setGeolocationCountry(0, 0x31);
                        geolocationHolder->setGeolocationRegion(0, 0x07);
                        for (int slotIndex = 1; slotIndex < Pokemon::Pokemon::GEOLOCATION_SLOT_COUNT; ++slotIndex)
                        {
                            geolocationHolder->setGeolocationCountry(slotIndex, 0);
                            geolocationHolder->setGeolocationRegion(slotIndex, 0);
                        }
                        TradePartner regionlessPartner;
                        regionlessPartner.trainerName = u"NoRegion";
                        regionlessPartner.gender = 0;
                        applyTradeRoundTrip(*geolocationHolder, regionlessPartner);
                        context.check(geolocationHolder->geolocationCountry(0) == 0x31 && geolocationHolder->geolocationRegion(0) == 0x07 && geolocationHolder->geolocationCountry(1) == 0, "a partner with no console region disturbed the geolocation history", groupLabel + " slot0=" + std::to_string(geolocationHolder->geolocationCountry(0)) + " slot1=" + std::to_string(geolocationHolder->geolocationCountry(1)));
                    }
                }

                // RETURN AND FRUSTRATION read the ACTIVE handler's friendship, so Gen 6/7 carries the
                // value across the switch rather than letting the move change power on a trade.
                std::vector<std::byte> returnRecord(recordSize, std::byte{0});
                auto returnHolder = Trainer::Bank::makePokemon(group, returnRecord);
                if (returnHolder != nullptr)
                {
                    returnHolder->setSpecies(pikachuSpeciesId);
                    returnHolder->setForm(0);
                    returnHolder->setLevel(20);
                    returnHolder->setCurrentHandler(0); // the OT holds it: friendship() is the current handler's in Gen 6/7
                    returnHolder->setFriendship(probeFriendship);
                    returnHolder->setMove(0, returnMoveId);
                    applyTradeRoundTrip(*returnHolder, partner);
                    context.check(returnHolder->htFriendship() == probeFriendship, "a Return holder's friendship was not carried across the handler switch", groupLabel + " read=" + std::to_string(returnHolder->htFriendship()) + " expected=" + std::to_string(probeFriendship));
                }

                // EVERY FEELING A LINK-TRADE MEMORY CAN CARRY MUST BE ONE THE GAMES ALLOW. The feeling
                // is picked from the record's own encryption constant, so sweeping it sweeps the
                // choice -- a plain modulo 20 would land on an illegal value about a third of the time.
                static const uint8_t permittedFeelings[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 15, 18};
                for (uint32_t encryptionProbe = 0; encryptionProbe < 64; ++encryptionProbe)
                {
                    std::vector<std::byte> feelingRecord(recordSize, std::byte{0});
                    auto feelingHolder = Trainer::Bank::makePokemon(group, feelingRecord);
                    if (feelingHolder == nullptr) break;
                    feelingHolder->setSpecies(pikachuSpeciesId);
                    feelingHolder->setEncryptionConstant(encryptionProbe);
                    feelingHolder->setHTMemory(0);
                    applyTradeRoundTrip(*feelingHolder, partner);
                    const uint8_t memoryFeeling = feelingHolder->htMemoryFeeling();
                    bool feelingPermitted = false;
                    for (uint8_t permittedFeeling : permittedFeelings)
                    {
                        if (permittedFeeling == memoryFeeling)
                        {
                            feelingPermitted = true;
                        }
                    }
                    context.check(feelingPermitted, "a link-trade memory carries a feeling the games do not allow", groupLabel + " encryptionConstant=" + std::to_string(encryptionProbe) + " feeling=" + std::to_string(memoryFeeling));
                }

                // AN EXISTING MEMORY IS NOT OVERWRITTEN. A Pokemon that has been traded before
                // carries a memory its previous handler made, and the games keep it -- so the writer
                // fills the field only when it is empty. Without this the skip branch is never
                // exercised on purpose, only by accident.
                std::vector<std::byte> keepRecord(recordSize, std::byte{0});
                auto keepHolder = Trainer::Bank::makePokemon(group, keepRecord);
                if (keepHolder != nullptr)
                {
                    constexpr uint8_t existingMemory = 42;
                    keepHolder->setSpecies(pikachuSpeciesId);
                    keepHolder->setHTMemory(existingMemory);
                    applyTradeRoundTrip(*keepHolder, partner);
                    context.check(keepHolder->htMemory() == existingMemory, "a trade overwrote a handler memory that was already there", groupLabel + " read=" + std::to_string(keepHolder->htMemory()));
                }
            }
        }
    }

    /**
     * Which games may stand in as the other side of a trade, over every pair of groups.
     *
     * Swept exhaustively rather than spot-checked, because the interesting answers are the FALSE
     * ones and there is no way to know a rule is too permissive by testing the cases it allows.
     */
    void checkTradePartnerRules(Context &context)
    {
        using Enums::GameVersion;
        for (GameVersion donorGroup : Test::ALL_SAVE_GROUPS)
        {
            for (GameVersion recordGroup : Test::ALL_SAVE_GROUPS)
            {
                const std::string where = "donor=" + Enums::getGameVersionName(donorGroup) + " record=" + Enums::getGameVersionName(recordGroup);
                const bool canTrade = Trainer::gamesCanTrade(donorGroup, recordGroup);

                // TRADING IS SYMMETRIC. A rule that is not would let a Pokemon be traded one way
                // and not back, which no link cable has ever done.
                context.check(canTrade == Trainer::gamesCanTrade(recordGroup, donorGroup), "gamesCanTrade is not symmetric", where);

                const bool involvesArceus = donorGroup == GameVersion::PLA || recordGroup == GameVersion::PLA;
                const bool bothGen6 = (donorGroup == GameVersion::XY || donorGroup == GameVersion::ORAS) && (recordGroup == GameVersion::XY || recordGroup == GameVersion::ORAS);
                const bool bothGen7 = (donorGroup == GameVersion::SM || donorGroup == GameVersion::USUM) && (recordGroup == GameVersion::SM || recordGroup == GameVersion::USUM);
                const bool expectedCanTrade = !involvesArceus && (donorGroup == recordGroup || bothGen6 || bothGen7);
                context.check(canTrade == expectedCanTrade, "gamesCanTrade disagrees with the link rule", where + " canTrade=" + (canTrade ? "1" : "0"));
            }
        }
        // THE TITLE FILTER THE DONOR PICKER USES. A concrete title resolves to its group, and a
        // format with no handler must offer NO titles at all -- a picker listing saves that cannot
        // contribute anything is worse than no picker.
        for (Enums::GameVersion recordGroup : Test::ALL_SAVE_GROUPS)
        {
            const std::string groupLabel = "record=" + Enums::getGameVersionName(recordGroup);
            const size_t recordSize = Trainer::Bank::recordSizeFor(recordGroup);
            std::vector<std::byte> record(recordSize, std::byte{0});
            auto entity = Trainer::Bank::makePokemon(recordGroup, record);
            if (entity == nullptr) continue;
            int offeredCount = 0;
            for (Enums::GameVersion titleVersion : Test::TESTED_TITLES)
            {
                if (Trainer::titleCanTradeWith(titleVersion, *entity))
                {
                    ++offeredCount;
                    context.check(entity->hasHandler(), "a title was offered as a donor for a format with no handler", groupLabel + " title=" + Enums::getGameVersionName(titleVersion));
                    context.check(Trainer::gamesCanTrade(Enums::getGameGroup(titleVersion), recordGroup), "a title was offered that cannot trade with this Pokemon's game", groupLabel + " title=" + Enums::getGameVersionName(titleVersion));
                }
            }
            if (!entity->hasHandler())
            {
                context.check(offeredCount == 0, "a handler-less format was offered donor titles", groupLabel + " offered=" + std::to_string(offeredCount));
            }
        }

        // LEGENDS: ARCEUS TRADES WITH NOTHING, INCLUDING ITSELF -- it has no trading feature, which
        // is why it needs no donor: its four trade evolutions run off the Linking Cord instead.
        context.check(!Trainer::gamesCanTrade(GameVersion::PLA, GameVersion::PLA), "Legends: Arceus was allowed to trade with itself", "group=PLA");
    }

    /**
     * Encounter rules that no save in a typical collection exercises, built rather than found.
     *
     * Every case here was a finding the console reported on a Pokemon PKHeX calls legal, and every
     * one is PKHeX's rule, not PKSE's guess: Crystal's in-game trades are met at LinkTrade2NPC at met
     * level 0; its roamers are met on whichever Johto route they were cornered; a Crystal egg hatches
     * anywhere up to location 95; a Gen 2 record with no caught data may be an egg from Gold or
     * Silver, or a Pokemon that crossed the Time Capsule from Gen 1 -- and a Gen 1 record may have
     * crossed the other way. The negatives matter as much: a Celebi with no caught data is NOT the
     * Virtual Console GS Ball one, and a Togepi below the level an egg hatches at did not hatch.
     *
     * Two more rules ride along because they share the question "is this legal Pokemon read as
     * legal": a cosmetic form keeps its colour down its evolution chain (a Blue Flower Florges
     * descends from a blue Flabébé), and Legends: Z-A keeps the ability a Pokemon was ENCOUNTERED
     * with through evolution.
     */
    void checkEncounterRules(Context &context)
    {
        constexpr uint16_t butterfreeSpeciesId = 12;
        constexpr uint16_t raticateSpeciesId = 20;
        constexpr uint16_t route16Location = 116; // FireRed: Rattata 18-22 and Raticate 23-25
        constexpr uint16_t articunoSpeciesId = 144;
        constexpr uint16_t cleffaSpeciesId = 173;
        constexpr uint16_t togepiSpeciesId = 175;
        constexpr uint16_t steelixSpeciesId = 208;
        constexpr uint16_t raikouSpeciesId = 243;
        constexpr uint16_t celebiSpeciesId = 251;
        constexpr uint16_t flabebeSpeciesId = 669;
        constexpr uint16_t floetteSpeciesId = 670;
        constexpr uint16_t florgesSpeciesId = 671;
        constexpr uint16_t fletchinderSpeciesId = 662;
        constexpr uint8_t blueFlowerFormId = 3;
        constexpr uint16_t bigPecksAbilityId = 145;
        constexpr uint16_t stenchAbilityId = 1;
        constexpr uint16_t kyleTrainerId = 48926; // the Violet City trade's Onix

        auto problemsIn = [](const Legality::Report &report)
        {
            std::string problems;
            for (const auto &issue : report.issues)
            {
                if (issue.severity != Legality::Severity::Info)
                {
                    problems += (problems.empty() ? "" : "; ") + issue.text;
                }
            }
            return problems;
        };
        // A Crystal-format record. Met location 0 and met level 0 leave its caught data EMPTY, which
        // is what a Gold or Silver Pokemon carries.
        auto gen2Pokemon = [](uint16_t speciesId, uint8_t level, uint16_t metLocation, uint8_t metLevel)
        {
            auto pokemon = std::make_unique<Pokemon::Pokemon2GSC>(false);
            pokemon->setSpecies(speciesId);
            pokemon->setLevel(level);
            pokemon->setTID16(12345);
            pokemon->setOTName(Utils::utf8ToUtf16("PKSE"));
            pokemon->setMetLocation(metLocation);
            pokemon->setMetLevel(metLevel);
            pokemon->recalculateStats();
            return pokemon;
        };
        auto expectLegal = [&](const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const char *what, const std::string &detail)
        {
            const Legality::Report report = Legality::analyze(pokemon, group);
            context.check(report.ok(), what, detail + " -- " + problemsIn(report));
        };
        auto expectFlagged = [&](const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const char *what, const std::string &detail)
        {
            context.check(!Legality::analyze(pokemon, group).ok(), what, detail);
        };

        {
            auto steelix = gen2Pokemon(steelixSpeciesId, 6, 126, 0);
            steelix->setTID16(kyleTrainerId);
            // The trade's own fixed DVs (Attack 9, then 6/6/6), which is also what makes it male --
            // a Gen 2 gender is read off the Attack DV, and the template fixes this one.
            steelix->setDV16(0x9666);
            steelix->recalculateStats();
            expectLegal(*steelix, Enums::GameVersion::GSC, "a Crystal in-game trade is not recognised", "Steelix traded as Kyle's Onix, met at LinkTrade2NPC (126) at met level 0");
        }
        {
            auto raikou = gen2Pokemon(raikouSpeciesId, 40, 34, 40);
            expectLegal(*raikou, Enums::GameVersion::GSC, "a Crystal roamer met on a Johto route is not recognised", "Raikou met at location 34 at level 40");
        }
        {
            auto cleffa = gen2Pokemon(cleffaSpeciesId, 5, 19, 1);
            expectLegal(*cleffa, Enums::GameVersion::GSC, "a Crystal egg hatched away from location 16 is flagged", "Cleffa met at location 19 at met level 1");
        }
        {
            auto togepi = gen2Pokemon(togepiSpeciesId, 5, 0, 0);
            expectLegal(*togepi, Enums::GameVersion::GSC, "a Gold/Silver egg with no caught data is flagged", "Togepi, level 5, no caught data");
            togepi->setLevel(3);
            expectFlagged(*togepi, Enums::GameVersion::GSC, "a Pokemon below the level an egg hatches at was explained as an egg", "Togepi, level 3, no caught data");
        }
        {
            auto articuno = gen2Pokemon(articunoSpeciesId, 65, 0, 0);
            expectLegal(*articuno, Enums::GameVersion::GSC, "a Gen 1 Pokemon in a Gen 2 save is not traced back through the Time Capsule", "Articuno, level 65, no caught data");
        }
        {
            auto celebi = gen2Pokemon(celebiSpeciesId, 30, 0, 0);
            expectFlagged(*celebi, Enums::GameVersion::GSC, "a Celebi with no caught data matched Crystal's GS Ball Celebi", "Celebi, level 30, no caught data");
            celebi->setMetLocation(14);
            celebi->setMetLevel(30);
            expectLegal(*celebi, Enums::GameVersion::GSC, "Crystal's GS Ball Celebi is not recognised", "Celebi met at Ilex Forest (14) at level 30");
        }
        {
            Pokemon::Pokemon1RBY butterfree(false);
            butterfree.setSpecies(butterfreeSpeciesId);
            butterfree.setLevel(9);
            butterfree.setTID16(12345);
            butterfree.setOTName(Utils::utf8ToUtf16("PKSE"));
            butterfree.recalculateStats();
            expectLegal(butterfree, Enums::GameVersion::RBY, "a Gen 1 Pokemon catchable in Gen 2 is not traced forward through the Time Capsule", "Butterfree, level 9 -- Gold and Crystal have a level 7 one");
        }

        // THE EVOLUTION FLOOR OUTRANKS THE LEVEL BAND. On FireRed's Route 16 a Raticate met at 18 fits
        // the Rattata row and misses its own 23-25 row; now level 19, what it cannot have done is
        // evolve. Ranked the other way, the report named a level band instead.
        {
            std::vector<std::byte> record(Trainer::Bank::recordSizeFor(Enums::GameVersion::FRLG), std::byte{0});
            auto raticate = Trainer::Bank::makePokemon(Enums::GameVersion::FRLG, record);
            if (context.check(raticate != nullptr, "no entity class for a supported save group", "FireRed/LeafGreen"))
            {
                raticate->setSpecies(raticateSpeciesId);
                raticate->setForm(0);
                raticate->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::FR));
                raticate->setMetLocation(route16Location);
                raticate->setMetLevel(18);
                raticate->setLevel(19);
                raticate->recalculateStats();
                const Legality::Report report = Legality::analyze(*raticate, Enums::GameVersion::FRLG);
                bool explainedAsEvolution = false;
                for (const auto &issue : report.issues)
                {
                    explainedAsEvolution = explainedAsEvolution || issue.text.find("would have to evolve from") != std::string::npos;
                }
                context.check(explainedAsEvolution, "an unpaid evolution level was reported as a level band instead", "Raticate met at Route 16 at 18, now level " + std::to_string(raticate->level()) + " -- " + problemsIn(report));
            }
        }

        // A COSMETIC FORM KEEPS ITS COLOUR DOWN THE CHAIN. X/Y's Flabébé flowers share one stats entry,
        // so a table that only counts forms WITH their own entry dropped every edge between them.
        {
            uint16_t ancestorSpeciesId = 0;
            uint8_t ancestorFormId = 0;
            const bool floetteFound = Pokemon::getPreEvolution(Enums::GameVersion::XY, florgesSpeciesId, blueFlowerFormId, ancestorSpeciesId, ancestorFormId);
            context.check(floetteFound && ancestorSpeciesId == floetteSpeciesId && ancestorFormId == blueFlowerFormId, "a Blue Flower Florges does not descend from a Blue Flower Floette", "found=" + std::to_string(floetteFound) + " species=" + std::to_string(ancestorSpeciesId) + " form=" + std::to_string(ancestorFormId));
            const bool flabebeFound = Pokemon::getPreEvolution(Enums::GameVersion::XY, floetteSpeciesId, blueFlowerFormId, ancestorSpeciesId, ancestorFormId);
            context.check(flabebeFound && ancestorSpeciesId == flabebeSpeciesId && ancestorFormId == blueFlowerFormId, "a Blue Flower Floette does not descend from a Blue Flower Flabébé", "found=" + std::to_string(flabebeFound) + " species=" + std::to_string(ancestorSpeciesId) + " form=" + std::to_string(ancestorFormId));
            const bool florgesHasLearnset = Pokemon::getLearnableBits(florgesSpeciesId, blueFlowerFormId, Enums::GameVersion::XY) != nullptr;
            context.check(florgesHasLearnset, "a cosmetic form has no learnset, so its moves are never checked", "Blue Flower Florges in X/Y");
        }

        // LEGENDS: Z-A KEEPS THE ABILITY A POKEMON WAS ENCOUNTERED WITH. Scarlet/Violet re-reads the
        // slot on evolution, so the same record there is the illegal one.
        for (const Enums::GameVersion group : {Enums::GameVersion::ZA, Enums::GameVersion::SV})
        {
            std::vector<std::byte> record(Trainer::Bank::recordSizeFor(group), std::byte{0});
            auto fletchinder = Trainer::Bank::makePokemon(group, record);
            if (!context.check(fletchinder != nullptr, "no entity class for a supported save group", Enums::getGameVersionName(group))) continue;
            fletchinder->setSpecies(fletchinderSpeciesId);
            fletchinder->setForm(0); // a zeroed record decrypts to garbage in every field, form included
            fletchinder->setAbility(bigPecksAbilityId);
            fletchinder->setAbilityNumber(2);
            const bool keepsAbility = group == Enums::GameVersion::ZA;
            context.check(Pokemon::isAbilityLegal(*fletchinder) == keepsAbility, keepsAbility ? "a Z-A Fletchinder keeping its Fletchling ability is called illegal" : "a Fletchinder keeping its Fletchling ability is called legal outside Z-A", Enums::getGameVersionName(group) + " ability=Big Pecks slot=2");
            fletchinder->setAbility(stenchAbilityId);
            context.check(!Pokemon::isAbilityLegal(*fletchinder), "an ability no species in the chain has is called legal", Enums::getGameVersionName(group) + " ability=Stench");
        }
    }

    /// The legality rules the 1.2.2 audit found answering wrongly on legitimate Pokemon, each posed
    /// on a synthetic record and asserted through the analyzer by the finding's own wording, so an
    /// unrelated finding about the synthetic record cannot mask or fake a result.
    void checkLegalityRuleFixes(Context &context)
    {
        auto makeRecord = [&](Enums::GameVersion group) -> std::unique_ptr<Pokemon::Pokemon>
        {
            // A zeroed DECRYPTED record, so every field starts blank; zero stored bytes decrypt to noise.
            const std::vector<std::byte> decryptedRecord(Trainer::Bank::recordSizeFor(group), std::byte{0});
            std::byte *encryptedRecord = Trainer::Bank::encryptRecord(group, decryptedRecord, 0);
            if (!context.check(encryptedRecord != nullptr, "no entity class for a supported save group", Enums::getGameVersionName(group))) return nullptr;
            auto pokemon = Trainer::Bank::makePokemon(group, std::span<const std::byte>(encryptedRecord, decryptedRecord.size()));
            delete[] encryptedRecord;
            if (!context.check(pokemon != nullptr, "no entity class for a supported save group", Enums::getGameVersionName(group))) return nullptr;
            return pokemon;
        };
        auto pose = [](Pokemon::Pokemon &pokemon, uint16_t speciesId, uint8_t formId, uint8_t originVersion, uint8_t level)
        {
            pokemon.setSpecies(speciesId);
            pokemon.setForm(formId);
            pokemon.setOriginGame(originVersion);
            pokemon.setLevel(level);
            pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
            pokemon.setLanguage(2);
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                pokemon.setMove(moveIndex, 0);
            }
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                pokemon.setEV(statIndex, 0);
            }
            pokemon.recalculateStats();
        };
        auto findingsAbout = [](const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const std::string &needle)
        {
            std::string matching;
            for (const auto &issue : Legality::analyze(pokemon, group).issues)
            {
                if (issue.text.find(needle) != std::string::npos)
                {
                    matching += (matching.empty() ? "" : "; ") + issue.text;
                }
            }
            return matching;
        };
        auto expectSilent = [&](const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const std::string &needle, const char *what, const std::string &detail)
        {
            const std::string found = findingsAbout(pokemon, group, needle);
            context.check(found.empty(), what, detail + " -- " + found);
        };
        auto expectFinding = [&](const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const std::string &needle, const char *what, const std::string &detail)
        {
            context.check(!findingsAbout(pokemon, group, needle).empty(), what, detail);
        };

        // Latin American Spanish is a Legends: Z-A language and no earlier game's.
        constexpr uint8_t spanishLatinAmericaLanguage = 11;
        if (auto pokemon = makeRecord(Enums::GameVersion::ZA))
        {
            pose(*pokemon, 25, 0, static_cast<uint8_t>(Enums::GameVersion::ZA), 10);
            pokemon->setLanguage(spanishLatinAmericaLanguage);
            expectSilent(*pokemon, Enums::GameVersion::ZA, "Invalid language", "a Z-A Pokemon in Latin American Spanish is called an invalid language", "language=11");
        }
        if (auto pokemon = makeRecord(Enums::GameVersion::SV))
        {
            pose(*pokemon, 25, 0, static_cast<uint8_t>(Enums::GameVersion::SL), 10);
            pokemon->setLanguage(spanishLatinAmericaLanguage);
            expectFinding(*pokemon, Enums::GameVersion::SV, "Invalid language", "a Scarlet Pokemon in Latin American Spanish is not flagged", "language=11");
        }

        // 255 EVs in one stat is ordinary before Gen 6; the 252 cap arrived with X/Y.
        for (const Enums::GameVersion group : {Enums::GameVersion::RSE, Enums::GameVersion::B2W2, Enums::GameVersion::XY})
        {
            auto pokemon = makeRecord(group);
            if (!pokemon) continue;
            pose(*pokemon, 25, 0, Enums::getGroupRepVersion(group), 50);
            pokemon->setEV(0, 255);
            const bool capsAt252 = group == Enums::GameVersion::XY;
            const std::string found = findingsAbout(*pokemon, group, "EV over 252");
            context.check(found.empty() != capsAt252, capsAt252 ? "a Gen 6 EV of 255 is not flagged" : "a pre-Gen 6 EV of 255 is called over the 252 cap", Enums::getGameVersionName(group) + " HP EV 255");
        }

        // A move learned in an earlier generation stays legal after the transfer. X/Y's Chespin learns
        // Tackle; Sun/Moon's does not.
        constexpr uint16_t chespinSpeciesId = 650;
        constexpr uint16_t tackleMoveId = 33;
        context.check(!Pokemon::isLearnable(chespinSpeciesId, 0, Enums::GameVersion::SM, tackleMoveId) && Pokemon::isLearnable(chespinSpeciesId, 0, Enums::GameVersion::XY, tackleMoveId), "the transferred-move case no longer poses the question it was written for", "Chespin Tackle: SM must lack it, XY must have it");
        if (auto pokemon = makeRecord(Enums::GameVersion::SM))
        {
            pose(*pokemon, chespinSpeciesId, 0, static_cast<uint8_t>(Enums::GameVersion::X), 10);
            pokemon->setMove(0, tackleMoveId);
            expectSilent(*pokemon, Enums::GameVersion::SM, "learnable: Tackle", "a move learned in X/Y is called unlearnable after the move to Sun/Moon", "Chespin from X with Tackle in Sun");
            pokemon->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::SN));
            expectFinding(*pokemon, Enums::GameVersion::SM, "learnable: Tackle", "a Sun-native Chespin with Tackle is not flagged", "Chespin from Sun with Tackle in Sun");
        }

        // Smeargle can know whatever Sketch copies, and nothing Sketch cannot.
        constexpr uint16_t smeargleSpeciesId = 235;
        context.check(Pokemon::isLearnable(smeargleSpeciesId, 0, Enums::GameVersion::XY, 147), "Smeargle's sketched Spore is unlearnable", "XY");
        context.check(!Pokemon::isLearnable(smeargleSpeciesId, 0, Enums::GameVersion::XY, 165), "Smeargle can learn Struggle", "XY");
        context.check(!Pokemon::isLearnable(smeargleSpeciesId, 0, Enums::GameVersion::SV, 464), "Smeargle can sketch Dark Void in Scarlet/Violet", "SV");

        // Gen 7's egg moves are per FORM, and Necrozma's fusion moves come from its fused forms.
        context.check(Pokemon::isLearnable(37, 1, Enums::GameVersion::SM, 573) && !Pokemon::isLearnable(37, 0, Enums::GameVersion::SM, 573), "Gen 7 egg moves are read by species rather than form", "Freeze-Dry: Alolan Vulpix yes, Kantonian no");
        context.check(Pokemon::isLearnable(800, 2, Enums::GameVersion::USUM, 714) && !Pokemon::isLearnable(800, 0, Enums::GameVersion::USUM, 714), "Dawn Wings Necrozma's Moongeist Beam is missing", "USUM");

        // A form past the species' count reads the base row, not the next species' rows.
        context.check(Pokemon::getLearnableBits(1, 5, Enums::GameVersion::SV) == Pokemon::getLearnableBits(1, 0, Enums::GameVersion::SV), "an out-of-range form reads another species' learnset", "Bulbasaur form 5");

        // ORAS's Cosplay Pikachu are female; the caps sharing their form numbers in Gen 7+ are male.
        context.check(Pokemon::getGenderRatio(25, 1, Enums::GameVersion::ORAS) == 254 && Pokemon::getGenderRatio(25, 1, Enums::GameVersion::USUM) == 0, "the gender ratio ignores what a form means in its game", "Pikachu form 1");
        if (auto pokemon = makeRecord(Enums::GameVersion::ORAS))
        {
            pose(*pokemon, 25, 1, static_cast<uint8_t>(Enums::GameVersion::OR), 20);
            pokemon->setGender(1);
            expectSilent(*pokemon, Enums::GameVersion::ORAS, "Male-only", "a female Cosplay Pikachu is called a male-only species", "ORAS Pikachu form 1");
        }
        context.check(std::string(Names::getFormName(25, 1, Enums::GameVersion::ORAS)) == "Rock Star" && std::string(Names::getFormName(493, 9, Enums::GameVersion::DP)) == "???" && std::string(Names::getFormName(493, 10, Enums::GameVersion::DP)) == Names::getTypeName(9), "a form is named by the Gen 7+ numbering in a game that numbers it differently", "ORAS Pikachu 1, Gen 4 Arceus 9 and 10");

        // ORAS: the Flutes and DexNav move a wild slot's level.
        if (auto pokemon = makeRecord(Enums::GameVersion::ORAS))
        {
            constexpr uint16_t slakothSpeciesId = 287;
            constexpr uint16_t petalburgWoodsLocation = 282;
            pose(*pokemon, slakothSpeciesId, 0, static_cast<uint8_t>(Enums::GameVersion::OR), 20);
            pokemon->setMetLocation(petalburgWoodsLocation);
            pokemon->setMetLevel(15);
            expectSilent(*pokemon, Enums::GameVersion::ORAS, "No encounter for", "a DexNav-boosted ORAS met level is reported", "Slakoth met at Petalburg Woods at 15");
            pokemon->setLevel(80);
            pokemon->setMetLevel(70);
            expectFinding(*pokemon, Enums::GameVersion::ORAS, "No encounter for", "an ORAS met level past even a DexNav chain is accepted", "Slakoth met at Petalburg Woods at 70");
        }

        // Bred-egg locations: BD/SP's traded egg is 30001; Platinum and HGSS also accept Faraway4.
        if (auto pokemon = makeRecord(Enums::GameVersion::BDSP))
        {
            pose(*pokemon, 387, 0, static_cast<uint8_t>(Enums::GameVersion::BD), 5);
            pokemon->setEggLocation(30001);
            pokemon->setMetLevel(1);
            expectSilent(*pokemon, Enums::GameVersion::BDSP, "Egg location", "a BD/SP link-traded egg (30001) is flagged", "Turtwig hatched from a traded egg");
        }
        for (const Enums::GameVersion version : {Enums::GameVersion::Pt, Enums::GameVersion::D})
        {
            auto pokemon = makeRecord(Enums::GameVersion::PT);
            if (!pokemon) continue;
            pose(*pokemon, 387, 0, static_cast<uint8_t>(version), 5);
            pokemon->setEggLocation(3002);
            const bool faraway = version == Enums::GameVersion::Pt;
            const std::string found = findingsAbout(*pokemon, Enums::GameVersion::PT, "Egg location");
            context.check(found.empty() == faraway, faraway ? "a Platinum egg from a faraway place (3002) is flagged" : "a Diamond egg from a faraway place (3002) is accepted", Enums::getGameVersionName(version) + " " + found);
        }

        // Scarlet/Violet writes an egg's version when it hatches, so an unhatched one has none.
        if (auto pokemon = makeRecord(Enums::GameVersion::SV))
        {
            pose(*pokemon, 443, 0, 0, 1);
            pokemon->setEgg(true);
            pokemon->setEggLocation(30023);
            expectSilent(*pokemon, Enums::GameVersion::SV, "Origin game ID 0", "an unhatched Scarlet/Violet egg reads as an unknown origin", "Gible egg, version 0");
        }

        // Gen 5's Blue-Striped Basculin may carry Reckless.
        if (auto pokemon = makeRecord(Enums::GameVersion::BW))
        {
            pose(*pokemon, 550, 1, static_cast<uint8_t>(Enums::GameVersion::W), 20);
            pokemon->setAbility(120);
            pokemon->setAbilityNumber(1);
            expectSilent(*pokemon, Enums::GameVersion::BW, "Ability not legal", "Gen 5 Blue-Striped Basculin with Reckless is called illegal", "White");
        }

        // HOME's met-location sentinels end the encounter check -- including the Switch FR/LG ones.
        if (auto pokemon = makeRecord(Enums::GameVersion::SWSH))
        {
            pose(*pokemon, 25, 0, static_cast<uint8_t>(Enums::GameVersion::SH), 20);
            pokemon->setMetLocation(59994);
            pokemon->setMetLevel(20);
            expectFinding(*pokemon, Enums::GameVersion::SWSH, "HOME replaced the met location", "the Switch FireRed/LeafGreen HOME sentinel is not recognised", "met 59994");
            expectSilent(*pokemon, Enums::GameVersion::SWSH, "is not a place", "the encounter sweep still runs after a HOME sentinel", "met 59994");
        }

        // The origin-version metadata for the ids PKHeX added (FRX/LGX, Champions, Winds, Waves).
        context.check(Enums::getVersionGeneration(56) == 3 && Enums::getVersionGeneration(57) == 3 && Enums::getVersionGeneration(53) == 9 && Enums::getVersionGeneration(54) == 0, "a new origin version has the wrong generation", "56/57/53/54");
        context.check(Enums::getOriginGameName(56) != "Unknown" && Enums::getOriginGameName(55) != "Unknown", "a new origin version has no name", "55/56");
        context.check(Enums::usesSixDigitTrainerID(34) && !Enums::usesSixDigitTrainerID(56) && !Enums::usesSixDigitTrainerID(53) && Enums::usesSixDigitTrainerID(52) && !Enums::usesSixDigitTrainerID(41), "the trainer-id format disagrees with PKHeX's generation rule", "34/56/53/52/41");
    }

    namespace
    {
        /// KNOWN VALUES. Every other check here asks whether a table is well formed -- named, unique,
        /// in range -- and a table shifted by one row passes all of them. These are values from the
        /// games (PKHeX's strings and personal tables), so a wrong row, a wrong generation's table or
        /// a wrong curve fails here.
        void checkKnownValues(Context &context)
        {
            // Names, in English: the display language is pinned for the check and put back after.
            const size_t originalLanguage = Names::displayLanguageIndex();
            Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
            auto expectName = [&](const char *tableKind, int entryId, const char *actualName, const char *expectedName)
            {
                context.check(actualName && std::string(actualName) == expectedName, "a table names a known id wrongly", std::string(tableKind) + "=" + std::to_string(entryId) + " got=\"" + (actualName ? actualName : "") + "\" want=\"" + expectedName + "\"");
            };
            expectName("species", 1, Trainer::getSpeciesName(1), "Bulbasaur");
            expectName("species", 25, Trainer::getSpeciesName(25), "Pikachu");
            expectName("species", 150, Trainer::getSpeciesName(150), "Mewtwo");
            expectName("species", 493, Trainer::getSpeciesName(493), "Arceus");
            expectName("species", 1025, Trainer::getSpeciesName(1025), "Pecharunt");
            expectName("move", 1, Names::getMoveName(1), "Pound");
            expectName("move", 33, Names::getMoveName(33), "Tackle");
            expectName("move", 85, Names::getMoveName(85), "Thunderbolt");
            expectName("ability", 1, Trainer::getAbilityName(1), "Stench");
            expectName("ability", 65, Trainer::getAbilityName(65), "Overgrow");
            expectName("nature", 0, Trainer::getNatureName(0), "Hardy");
            expectName("nature", 24, Trainer::getNatureName(24), "Quirky");
            expectName("item", 1, Names::getItemNameFor(Enums::GameVersion::SWSH, 1), "Master Ball");
            expectName("item", 17, Names::getItemNameFor(Enums::GameVersion::SWSH, 17), "Potion");
            Names::setDisplayLanguage(originalLanguage);

            // The six growth curves at level 100, in the games' own curve order.
            const uint32_t level100Exp[6] = {1000000, 600000, 1640000, 1059860, 800000, 1250000};
            for (uint8_t growthRate = 0; growthRate < 6; ++growthRate)
            {
                context.check(Pokemon::getExpForLevel(100, growthRate) == level100Exp[growthRate], "a growth curve has the wrong EXP total at level 100", "rate=" + std::to_string(growthRate) + " got=" + std::to_string(Pokemon::getExpForLevel(100, growthRate)) + " want=" + std::to_string(level100Exp[growthRate]));
            }
            context.check(Pokemon::getGrowthRate(1) == 3, "Bulbasaur is not on the Medium Slow curve", "rate=" + std::to_string(Pokemon::getGrowthRate(1)));
            context.check(Pokemon::getGrowthRate(25) == 0, "Pikachu is not on the Medium Fast curve", "rate=" + std::to_string(Pokemon::getGrowthRate(25)));

            // Base stats in EVERY per-group personal table. Bulbasaur never changed; Pikachu's Defense
            // and Sp. Def went up in Gen 6, so a table built from the wrong generation fails here.
            // Gen 1's single Special is checked as Sp. Atk only.
            for (Enums::GameVersion group : ALL_SAVE_GROUPS)
            {
                using Enums::GameVersion;
                const std::string where = "group=" + Enums::getGameVersionName(group);
                const bool isGen1 = group == GameVersion::RBY;
                const bool beforeGen6 = isGen1 || group == GameVersion::GSC || Enums::isGen3Group(group) || group == GameVersion::DP || group == GameVersion::PT || group == GameVersion::HGSS || group == GameVersion::BW || group == GameVersion::B2W2;
                struct ExpectedBaseStats
                {
                    uint16_t speciesId;
                    uint8_t hp;
                    uint8_t atk;
                    uint8_t def;
                    uint8_t spe;
                    uint8_t spa;
                    uint8_t spd;
                };
                const ExpectedBaseStats expectedRows[2] = {
                    {1, 45, 49, 49, 45, 65, 65},
                    {25, 35, 55, (uint8_t)(beforeGen6 ? 30 : 40), 90, 50, (uint8_t)(beforeGen6 ? 40 : 50)},
                };
                for (const ExpectedBaseStats &row : expectedRows)
                {
                    const Pokemon::PersonalRecord &personalRecord = Pokemon::getPersonalRecord(group, row.speciesId, 0);
                    if (personalRecord.hp == 0)
                    {
                        context.skip("a species is not in this game's personal table, so its base stats were not checked", where + " species=" + std::to_string(row.speciesId));
                        continue;
                    }
                    const bool baseStatsMatch = personalRecord.hp == row.hp && personalRecord.atk == row.atk && personalRecord.def == row.def && personalRecord.spe == row.spe && personalRecord.spa == row.spa && (isGen1 || personalRecord.spd == row.spd);
                    context.check(baseStatsMatch, "a personal table has the wrong base stats for a known species", where + " species=" + std::to_string(row.speciesId) + " got=" + std::to_string(personalRecord.hp) + "/" + std::to_string(personalRecord.atk) + "/" + std::to_string(personalRecord.def) + "/" + std::to_string(personalRecord.spe) + "/" + std::to_string(personalRecord.spa) + "/" + std::to_string(personalRecord.spd));
                }

                // Base PP. The Legends games run their own battle rules, so they are left out.
                if (group == GameVersion::PLA || group == GameVersion::ZA) continue;
                const struct
                {
                    uint16_t moveId;
                    uint8_t basePP;
                } ppRows[3] = {{1, 35}, {33, 35}, {85, 15}};
                for (const auto &ppRow : ppRows)
                {
                    if (!Names::isMovePresent(ppRow.moveId, group)) continue;
                    const uint8_t actualBasePP = Names::getMoveBasePP(ppRow.moveId, group);
                    context.check(actualBasePP == ppRow.basePP, "a move has the wrong base PP", where + " move=" + std::to_string(ppRow.moveId) + " got=" + std::to_string(actualBasePP) + " want=" + std::to_string(ppRow.basePP));
                }
            }
        }
        /// The ribbon tables, as the editor writes through them. Each row must own its bits alone --
        /// a write that spills into a neighbour hands out or takes away a ribbon nobody touched --
        /// and the HOME block's rows must sit in RibbonIndex order, because AffixedRibbon stores that
        /// index and the editor derives it from the row.
        void checkRibbonDefinitions(Context &context)
        {
            // Covers the largest ribbon block, PK4/PK5's 0x24..0x63.
            constexpr size_t entityBufferSize = 0x100;
            for (Enums::GameVersion group : ALL_SAVE_GROUPS)
            {
                const std::string groupLabel = Enums::getGameVersionName(group);
                const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(group);
                // Gen 3 introduced ribbons; every format from it on stores some.
                const bool storesRibbons = Enums::getVersionGeneration(Enums::getGroupRepVersion(group)) >= 3;
                if (!context.check(ribbons.empty() != storesRibbons, "a format's ribbon table is empty where the format stores ribbons, or not where it stores none", groupLabel + " ribbons=" + std::to_string(ribbons.size())))
                {
                    continue;
                }

                const bool homeBlock = Conversion::affixedRibbonOffset(group) != 0;
                int expectedAffixIndex = 0;
                for (size_t ribbonIndex = 0; ribbonIndex < ribbons.size(); ++ribbonIndex)
                {
                    const Names::RibbonDefinition &ribbon = ribbons[ribbonIndex];
                    const std::string where = groupLabel + " ribbon=\"" + ribbon.englishName + "\"";
                    context.check(ribbon.maxCount >= 1 && (ribbon.kind != Names::RibbonKind::Flag || ribbon.maxCount == 1), "a ribbon's maximum does not fit its kind", where + " max=" + std::to_string(ribbon.maxCount));

                    // Set to its top on an empty record: it reads back, and no other ribbon moved.
                    uint8_t entityBytes[entityBufferSize] = {};
                    Names::setRibbonValue(entityBytes, ribbon, ribbon.maxCount);
                    context.check(Names::getRibbonValue(entityBytes, ribbon) == ribbon.maxCount, "a ribbon set to its maximum does not read back", where);
                    int movedCount = 0;
                    for (size_t otherIndex = 0; otherIndex < ribbons.size(); ++otherIndex)
                    {
                        if (otherIndex != ribbonIndex && Names::getRibbonValue(entityBytes, ribbons[otherIndex]) != 0)
                        {
                            ++movedCount;
                        }
                    }
                    context.check(movedCount == 0, "setting a ribbon gave the Pokemon another ribbon too", where + " others=" + std::to_string(movedCount));
                    Names::setRibbonValue(entityBytes, ribbon, 0);
                    bool clearedToZero = true;
                    for (uint8_t entityByte : entityBytes)
                    {
                        clearedToZero = clearedToZero && entityByte == 0;
                    }
                    context.check(clearedToZero, "clearing a ribbon left bytes behind on an empty record", where);

                    // Cleared on a full record: only its own value falls, and every other byte holds.
                    uint8_t fullBytes[entityBufferSize];
                    std::memset(fullBytes, 0xFF, sizeof(fullBytes));
                    uint8_t expectedBytes[entityBufferSize];
                    std::memcpy(expectedBytes, fullBytes, sizeof(fullBytes));
                    Names::setRibbonValue(fullBytes, ribbon, 0);
                    context.check(Names::getRibbonValue(fullBytes, ribbon) == 0, "a cleared ribbon still reads as held", where);
                    int strayByteCount = 0;
                    for (size_t byteOffset = 0; byteOffset < entityBufferSize; ++byteOffset)
                    {
                        const bool ownByte = byteOffset == ribbon.byteOffset || (ribbon.kind == Names::RibbonKind::MemoryCount && byteOffset == ribbon.countByteOffset) || (ribbon.kind == Names::RibbonKind::ContestRank && byteOffset >= ribbon.byteOffset && byteOffset < ribbon.byteOffset + 4u);
                        if (!ownByte && fullBytes[byteOffset] != expectedBytes[byteOffset])
                        {
                            ++strayByteCount;
                        }
                    }
                    context.check(strayByteCount == 0, "clearing a ribbon changed a byte that is not its own", where + " bytes=" + std::to_string(strayByteCount));
                    // A PK3's bit 31 is FatefulEncounter, in the same word as the contest ranks.
                    if (Enums::isGen3Group(group))
                    {
                        context.check((fullBytes[0x4F] & 0x80) != 0, "clearing a Gen 3 ribbon cleared the fateful-encounter bit", where);
                    }

                    // A memory count carries its has-flag with it, both ways.
                    if (ribbon.kind == Names::RibbonKind::MemoryCount)
                    {
                        uint8_t countBytes[entityBufferSize] = {};
                        Names::setRibbonValue(countBytes, ribbon, 1);
                        context.check(((countBytes[ribbon.byteOffset] >> ribbon.bit) & 1) == 1, "a memory count was set without its has-flag", where);
                        Names::setRibbonValue(countBytes, ribbon, static_cast<uint8_t>(ribbon.maxCount + 5));
                        context.check(Names::getRibbonValue(countBytes, ribbon) == ribbon.maxCount, "a memory count was stored past its maximum", where + " got=" + std::to_string(Names::getRibbonValue(countBytes, ribbon)));
                        Names::setRibbonValue(countBytes, ribbon, 0);
                        context.check(((countBytes[ribbon.byteOffset] >> ribbon.bit) & 1) == 0, "a memory count cleared to zero kept its has-flag", where);
                    }

                    // Only the HOME block feeds AffixedRibbon, and its rows count up from 0 in order.
                    const int affixIndex = Names::getRibbonAffixIndex(ribbon);
                    if (homeBlock)
                    {
                        context.check(affixIndex == expectedAffixIndex, "a HOME ribbon row is out of RibbonIndex order", where + " affix=" + std::to_string(affixIndex) + " want=" + std::to_string(expectedAffixIndex));
                        ++expectedAffixIndex;
                    }
                    else
                    {
                        context.check(affixIndex == -1, "a ribbon outside the HOME block claims an AffixedRibbon index", where + " affix=" + std::to_string(affixIndex));
                    }

                    for (int ribbonValue = 0; ribbonValue <= ribbon.maxCount; ++ribbonValue)
                    {
                        context.check(!Names::getRibbonSpriteStem(ribbon, static_cast<uint8_t>(ribbonValue), group).empty(), "a ribbon value has no sprite stem", where + " value=" + std::to_string(ribbonValue));
                    }
                }

                // PKHeX's RibbonIndex anchors: the first row, the contest memory, the first row after
                // the count bytes and the last, so a row inserted or dropped anywhere shifts one of them.
                if (homeBlock)
                {
                    const auto affixIndexOf = [&](const char *englishName)
                    {
                        for (const Names::RibbonDefinition &ribbon : ribbons)
                        {
                            if (std::strcmp(ribbon.englishName, englishName) == 0) return Names::getRibbonAffixIndex(ribbon);
                        }
                        return -2;
                    };
                    context.check(affixIndexOf("Kalos Champion") == 0, "Kalos Champion is not RibbonIndex 0", groupLabel);
                    context.check(affixIndexOf("Contest Memory") == 37, "Contest Memory is not RibbonIndex 37", groupLabel);
                    context.check(affixIndexOf("Misty Mark") == 64, "Misty Mark is not RibbonIndex 64", groupLabel);
                    context.check(affixIndexOf("Partner") == 110, "Partner is not RibbonIndex 110", groupLabel);
                }
            }

            // The Gen 3 contest ranks are named and drawn per rank, not per flag.
            const std::span<const Names::RibbonDefinition> gen3Ribbons = Names::getRibbonDefinitions(Enums::GameVersion::RSE);
            if (context.check(!gen3Ribbons.empty() && gen3Ribbons[0].kind == Names::RibbonKind::ContestRank, "the Gen 3 table does not open with the Cool contest rank", ""))
            {
                const size_t originalLanguage = Names::displayLanguageIndex();
                Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
                context.check(Names::getRibbonName(gen3Ribbons[0], 3) == "Cool Ribbon Hyper", "a Gen 3 contest rank is misnamed", "got=\"" + Names::getRibbonName(gen3Ribbons[0], 3) + "\"");
                Names::setDisplayLanguage(originalLanguage);
                context.check(Names::getRibbonSpriteStem(gen3Ribbons[0], 4, Enums::GameVersion::RSE) == "ribbong3coolmaster", "a Gen 3 contest rank draws the wrong art", "got=" + Names::getRibbonSpriteStem(gen3Ribbons[0], 4, Enums::GameVersion::RSE));
            }
        }
    }


    int tablesPlan(Context &) { return ChunkCount; }

    void tablesRun(Context &context, int chunk)
    {
        switch (chunk)
        {
        case SpeciesNamesChunk:
            checkSpeciesNames(context);
            break;
        case KnownValuesChunk:
            checkKnownValues(context);
            break;
        case MoveNamesChunk:
            checkMoveNames(context);
            break;
        case MiscNamesChunk:
            checkMiscNames(context);
            break;
        case ExperienceChunk:
            checkExperience(context);
            break;
        case PersonalChunkA:
            checkPersonal(context, 1, PERSONAL_CHUNK_SPLIT_SPECIES - 1);
            break;
        case PersonalChunkB:
            checkPersonal(context, PERSONAL_CHUNK_SPLIT_SPECIES, Pokemon::PERSONAL_MAX_SPECIES);
            break;
        case FormNamesChunk:
            checkFormNames(context);
            break;
        case EggSourceLocationsChunk:
            checkEggSourceLocations(context);
            break;
        case EntityCapabilitiesChunk:
            checkEntityCapabilities(context);
            break;
        case Gen1TablesChunk:
            checkGen1Tables(context);
            break;
        case NameLanguagesChunk:
            checkNameLanguages(context);
            break;
        case TitleRoutingChunk:
            checkTitleRouting(context);
            checkTitleFolders(context);
            checkLegacyFolderMigration(context);
            break;
        case TeraTypesChunk:
            checkTeraTypes(context);
            break;
        case TradeEvolutionsChunk:
            checkTradeEvolutions(context);
            break;
        case HandlerTradeChunk:
            checkHandlerTrade(context);
            break;
        case TradePartnerRulesChunk:
            checkTradePartnerRules(context);
            break;
        case EncounterRulesChunk:
            checkEncounterRules(context);
            checkLegalityRuleFixes(context);
            break;
        case RibbonDefinitionsChunk:
            checkRibbonDefinitions(context);
            break;
        default:
            break;
        }
    }
}
