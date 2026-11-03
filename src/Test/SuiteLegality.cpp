/**
 * The checker is informational and deliberately conservative, so it has no "correct" output to
 * compare against. What it does have is a property worth testing: run against Pokemon that were
 * caught on real hardware, in the game they came from, it should find nothing Invalid. Every
 * Invalid on a legitimately-played save is either a real illegality the user already knew about, or
 * a bug in the checker -- and the second kind is exactly the kind that is impossible to notice by
 * hand, because a checker that flags too much looks like it is working hard.
 *
 * The Gen 1 case is the worked example: the analyzer read Stat Experience through the 8-bit EV
 * accessors, which SATURATE at 255, and reported seven Invalid findings on a legitimately trained
 * Pokemon. Nothing crashed, nothing looked broken, and the only way to see it was to look at a real
 * pokemon and disagree with the verdict. This suite is that look, done for every Pokemon in the save.
 *
 * Findings here are WARNINGS, not failures: PKSE can edit saves, so a user's own box may genuinely
 * contain something illegal, and calling that a test failure would be wrong. The value is in the
 * list and the rate -- one Invalid in a box of thirty is a pokemon; thirty Invalids in a box of thirty
 * is a bug.
 *
 * A checker that never fires would pass all of that, so the suite also PLANTS defects: a blank
 * Pokemon with one deliberate illegality must be flagged for exactly that, and the same Pokemon
 * without it must not be. Those are failures, because there the right answer is known.
 */
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <string_view>

#include "Enums/LanguageID.h"
#include "Test/TestSuite.h"
#include "Trainer/Bank.h"
#include "Trainer/PokemonFile.h"
#include "Trainer/Trainer.h"
#include "Legality/AnalysisContext.h"
#include "Legality/Encounters/EggHatchLocation.h"
#include "Legality/Encounters/EncounterEgg2.h"
#include "Legality/Encounters/EncounterEgg3.h"
#include "Legality/Encounters/EncounterEgg4.h"
#include "Legality/Encounters/EncounterEgg5.h"
#include "Legality/Encounters/EncounterEgg6.h"
#include "Legality/Encounters/EncounterEgg7.h"
#include "Legality/Encounters/EncounterEgg8.h"
#include "Legality/Encounters/EncounterEgg8b.h"
#include "Legality/Encounters/EncounterEgg9.h"
#include "Legality/Encounters/EncounterInvalid.h"
#include "Legality/Encounters/EncounterStatic5.h"
#include "Legality/Encounters/EncounterSlot3.h"
#include "Legality/Encounters/EncounterSlot7.h"
#include "Legality/Encounters/EncounterArea3.h"
#include "Legality/Encounters/EncounterArea5.h"
#include "Legality/Encounters/EncounterArea7.h"
#include "Legality/Encounters/EncounterArea4.h"
#include "Legality/Encounters/EncounterData1.h"
#include "Legality/Encounters/EncounterData2.h"
#include "Legality/Encounters/EncounterData4.h"
#include "Legality/Encounters/EncounterData5.h"
#include "Legality/Encounters/EncounterData6.h"
#include "Legality/Encounters/EncounterData7.h"
#include "Legality/Encounters/EncounterSlot7b.h"
#include "Legality/Encounters/EncounterGenerator6.h"
#include "Legality/Encounters/EncounterGenerator7.h"
#include "Legality/Encounters/EncounterSlot6AO.h"
#include "Legality/Encounters/EncounterSlot6XY.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/EncounterTransfer7.h"
#include "Legality/Encounters/EncounterData8.h"
#include "Legality/Encounters/EncounterData8a.h"
#include "Legality/Encounters/EncounterData8b.h"
#include "Legality/Encounters/EncounterEnumerator8a.h"
#include "Legality/Encounters/EncounterEnumerator8b.h"
#include "Legality/Encounters/EncounterGenerator8b.h"
#include "Legality/Encounters/EncounterRules8a.h"
#include "Legality/Encounters/EncounterRules8b.h"
#include "Legality/Encounters/EncounterSlot8a.h"
#include "Legality/Encounters/EncounterSlot8b.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterFinder.h"
#include "Legality/Encounters/EncounterGenerator3.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/EncounterSlot5.h"
#include "Legality/Encounters/EncounterGenerator8.h"
#include "Legality/Encounters/EncounterData9.h"
#include "Legality/Encounters/EncounterData9a.h"
#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/EncounterSlot9a.h"
#include "Legality/RNG/LumioseSolver.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Legality/Encounters/EncounterEnumerator9.h"
#include "Legality/Encounters/EncounterGenerator9.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/EncounterDataGO.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/EncounterStatic9.h"
#include "Legality/Encounters/EncounterStatic9a.h"
#include "Legality/Encounters/EncounterTrade4RanchGift.h"
#include "Legality/Encounters/EncounterTrade7.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/MysteryGiftWC9.h"
#include "Legality/Encounters/TemplateRibbons.h"
#include "Legality/Encounters/PogoSlot.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Legality/Encounters/EncounterData3.h"
#include "Legality/Encounters/EncounterData3GC.h"
#include "Legality/RNG/ClassicEraRNG.h"
#include "Legality/RNG/CommonEvent3.h"
#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/EncounterShadow3Colo.h"
#include "Legality/Encounters/EncounterShadow3XD.h"
#include "Legality/Encounters/EncounterSlot1.h"
#include "Legality/Encounters/EncounterSlot2.h"
#include "Legality/Encounters/GroundTileAllowed.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Legality.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Legality/LearnSource/ChainBreed.h"
#include "Legality/LearnSource/LearnEncounter.h"
#include "Legality/LearnSource/LearnEntity.h"
#include "Legality/LearnSource/LearnVerifier.h"
#include "Legality/LearnSource/MoveBreed.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Pokemon/Pokemon8LA.h"
#include "Legality/Verifiers/MiscVerifier.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Legality/Verifiers/WordFilter.h"
#include "Names/RibbonNames.h"
#include "Conversion/Convert.h"
#include "Utils/Gen2KoreanText.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalRecord.h"

namespace Test
{

    namespace
    {
        constexpr int PARTY_CHUNK = 0;

        /// Analyze one Pokemon and report anything Invalid. `checkDeterminism` additionally re-runs the analyzer
        /// to prove it is deterministic -- an analyzer whose verdict depends on how many times it
        /// has run has state it should not have, and would give the user a different answer each
        /// time they opened the same pokemon.
        void analyzeOne(Context &context, const Pokemon::Pokemon &pokemon, const std::string &slotLabel, bool checkDeterminism)
        {
            const std::string findingLabel = slotLabel + "  " + pokemonLabel(pokemon);

            Legality::Report legalityReport = Legality::analyze(pokemon, context.group);

            int invalidCount = 0;
            int warningCount = 0;
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == Legality::Severity::Invalid)
                {
                    ++invalidCount;
                    context.warn(false, "legality analyzer reports INVALID on a stored Pokemon", findingLabel + " issue=\"" + issue.text + "\"");
                }
                else if (issue.severity == Legality::Severity::Warning)
                {
                    ++warningCount;
                }
            }

            // problemCount() must agree with what the issue list actually contains; the UI gates its
            // badge on that number rather than on the list.
            context.check(legalityReport.problemCount() == invalidCount + warningCount, "legality report's problem count disagrees with its own issue list", findingLabel + " problemCount=" + std::to_string(legalityReport.problemCount()) + " invalid=" + std::to_string(invalidCount) + " warnings=" + std::to_string(warningCount));
            context.check(legalityReport.ok() == (legalityReport.problemCount() == 0), "legality report's ok() disagrees with its count", findingLabel);

            if (checkDeterminism)
            {
                // The whole verdict, not just its length: two runs that found the same NUMBER of
                // different problems disagree just as much.
                Legality::Report secondReport = Legality::analyze(pokemon, context.group);
                bool verdictsMatch = secondReport.issues.size() == legalityReport.issues.size();
                for (size_t issueIndex = 0; verdictsMatch && issueIndex < secondReport.issues.size(); ++issueIndex)
                {
                    verdictsMatch = secondReport.issues[issueIndex].text == legalityReport.issues[issueIndex].text && secondReport.issues[issueIndex].severity == legalityReport.issues[issueIndex].severity;
                }
                context.check(verdictsMatch, "legality analyzer is not deterministic: two runs disagree", findingLabel + " first=" + std::to_string(legalityReport.issues.size()) + " second=" + std::to_string(secondReport.issues.size()));
            }
        }
    }

    namespace
    {
        bool reportsCode(const Legality::Report &legalityReport, Legality::Severity severity, Legality::CheckIdentifier identifier, Legality::ResultCode code)
        {
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == severity && issue.identifier == identifier && issue.code == code)
                {
                    return true;
                }
            }
            return false;
        }

        bool reportsInvalid(const Legality::Report &legalityReport, const std::string &textPrefix)
        {
            for (const auto &issue : legalityReport.issues)
            {
                if (issue.severity == Legality::Severity::Invalid && issue.text.rfind(textPrefix, 0) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// One planted defect: `control` is a Pokemon without it, `defective` the same Pokemon with
        /// it. The checker must flag the second for exactly this and must not flag the first.
        void expectFlagged(Context &context, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const std::string &textPrefix)
        {
            const std::string defectLabel = "planted defect \"" + textPrefix + "\" on a blank " + Enums::getGameVersionName(context.group) + " entity";
            context.check(!reportsInvalid(Legality::analyze(control, context.group), textPrefix), "legality analyzer flags a planted defect on the control Pokemon that does not have it", defectLabel);
            context.check(reportsInvalid(Legality::analyze(defective, context.group), textPrefix), "legality analyzer did not flag a planted defect", defectLabel);
        }

        void plantedDefects(Context &context, Trainer::Trainer &trainer)
        {
            auto control = trainer.createBlankPokemon();
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing")) return;
            control->setSpecies(25);  // Pikachu, in every game's dex
            control->setMove(0, 33);  // Tackle
            control->setMove(1, 85);  // Thunderbolt -- both exist from Gen 1 on
            control->refreshChecksum();

            // Every format holds four moves, and the same move twice is never legal.
            auto duplicate = control->clone();
            if (!context.check(duplicate != nullptr, "clone() returned nothing for this format")) return;
            duplicate->setMove(1, 33);
            duplicate->refreshChecksum();
            // PKHeX LearnVerifier.Finalize flags the earlier of the two slots.
            expectFlagged(context, *control, *duplicate, "Invalid Move 1: Duplicate Move.");

            // The 510 EV cap is Gen 3 onward. Gens 1 and 2 keep uncapped Stat Experience, and Let's Go
            // stores no EVs at all, so there is nothing to plant there.
            const Enums::GameVersion group = context.group;
            if (group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC && group != Enums::GameVersion::GG)
            {
                auto overTrained = control->clone();
                if (!overTrained) return;
                for (int statIndex = 0; statIndex < 3; ++statIndex)
                {
                    overTrained->setEVWide(statIndex, 252);
                }
                overTrained->refreshChecksum();
                const Legality::Report controlReport = Legality::analyze(*control, group);
                const Legality::Report overTrainedReport = Legality::analyze(*overTrained, group);
                const std::string defectLabel = "planted defect \"EV total over 510\" on a blank " + Enums::getGameVersionName(group) + " entity";
                context.check(!reportsCode(controlReport, Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortAbove510), "legality analyzer flags a planted defect on the control Pokemon that does not have it", defectLabel);
                context.check(reportsCode(overTrainedReport, Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortAbove510), "legality analyzer did not flag a planted defect", defectLabel);
            }
        }
    }

    // ---- Planted defects for the PID, gender, IV, EV, awakening value and Hyper Training verifiers.
    // Each runs one verifier directly with a hand-built encounter, because most of their branches
    // depend on what the encounter search concluded, which a blank Pokemon cannot reach; neither the
    // real saves nor PKHeX's test files exercise these branches.
    namespace
    {
        using VerifierFunction = void (*)(const Legality::AnalysisContext &);

        int getGroupGeneration(Enums::GameVersion group)
        {
            return Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
        }

        /// Runs one verifier on a hand-built context; `encounterMatch`, when given, is planted as the
        /// engine's match, as the encounter search would.
        Legality::Report runVerifier(VerifierFunction verifier, const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const Legality::EncounterInfo &encounter, const Legality::Encounters::EncounterReference &encounterMatch = {})
        {
            Legality::Report legalityReport;
            const Legality::AnalysisOptions analysisOptions;
            Legality::Encounters::LegalityInfo legalityInfo(pokemon, analysisOptions);
            Legality::AnalysisContext analysisContext{pokemon, group, getGroupGeneration(group), analysisOptions, legalityReport, encounter};
            if (encounterMatch)
            {
                legalityInfo.setEncounterMatch(encounterMatch);
                analysisContext.legalityInfo = &legalityInfo;
            }
            verifier(analysisContext);
            return legalityReport;
        }

        struct VerifierExpectation
        {
            Legality::Severity severity;
            Legality::CheckIdentifier identifier;
            Legality::ResultCode code;
        };

        /// The verifier must report `expectation` for `defective` and must not for `control`.
        void expectVerifierFlags(Context &context, VerifierFunction verifier, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const Legality::EncounterInfo &encounter, const Legality::Encounters::EncounterReference &encounterMatch, const VerifierExpectation &expectation)
        {
            const std::string matchLabel = encounterMatch ? " (" + encounterMatch->longName() + ")" : std::string();
            const std::string defectLabel = std::string("planted defect PKHeX ") + Legality::getCheckIdentifierSourceName(expectation.identifier) + "/" + Legality::getResultCodeSourceName(expectation.code) + (expectation.severity == Legality::Severity::Invalid ? " (Invalid)" : " (Fishy)") + matchLabel + " on a blank " + Enums::getGameVersionName(context.group) + " entity";
            context.check(!reportsCode(runVerifier(verifier, control, context.group, encounter, encounterMatch), expectation.severity, expectation.identifier, expectation.code), "legality verifier flags a planted defect on the control Pokemon that does not have it", defectLabel);
            context.check(reportsCode(runVerifier(verifier, defective, context.group, encounter, encounterMatch), expectation.severity, expectation.identifier, expectation.code), "legality verifier did not flag a planted defect", defectLabel);
        }

        void expectVerifierFlags(Context &context, VerifierFunction verifier, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const Legality::EncounterInfo &encounter, const VerifierExpectation &expectation)
        {
            expectVerifierFlags(context, verifier, control, defective, encounter, {}, expectation);
        }

        /// The first template in a generated table that `predicate` accepts; null when none does.
        template <typename TemplateRange, typename Predicate>
        Legality::Encounters::EncounterReference findTableTemplate(const TemplateRange &templates, Predicate predicate)
        {
            for (const auto &encounterTemplate : templates)
            {
                if (predicate(encounterTemplate))
                {
                    return Legality::Encounters::borrowEncounter(encounterTemplate);
                }
            }
            return nullptr;
        }

        /// The first wild slot in a generated area table that `predicate` accepts; null when none does.
        template <typename AreaRange, typename Predicate>
        Legality::Encounters::EncounterReference findAreaSlot(const AreaRange &areas, Predicate predicate)
        {
            for (const auto &area : areas)
            {
                for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); ++slotIndex)
                {
                    if (predicate(area.slotAt(slotIndex)))
                    {
                        return Legality::Encounters::borrowEncounter(area.slotAt(slotIndex));
                    }
                }
            }
            return nullptr;
        }

        /// The first wild slot of `speciesId` in a generated area table.
        template <typename AreaRange>
        Legality::Encounters::EncounterReference findAreaSlotOfSpecies(const AreaRange &areas, uint16_t speciesId)
        {
            return findAreaSlot(areas, [speciesId](const auto &slot) { return slot.speciesId() == speciesId; });
        }

        std::unique_ptr<Pokemon::Pokemon> createPokemon(const Trainer::Trainer &trainer, uint16_t speciesId, uint8_t level)
        {
            auto pokemon = trainer.createBlankPokemon();
            if (!pokemon)
            {
                return nullptr;
            }
            pokemon->setSpecies(speciesId);
            pokemon->setExp(Pokemon::getExpForLevel(level, Pokemon::getGrowthRate(speciesId)));
            pokemon->refreshChecksum();
            return pokemon;
        }

        void setPersonality(Pokemon::Pokemon &pokemon, uint32_t PID, uint32_t encryptionConstant)
        {
            pokemon.setPID(PID);
            pokemon.setEncryptionConstant(encryptionConstant);
            pokemon.refreshChecksum();
        }

        void plantedEffortValueDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            constexpr uint16_t pikachuSpecies = 25;
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            if (formatGeneration < 3)
            {
                // Stat Experience gained in battle comes from every base stat at once.
                auto control = base->clone();
                auto defective = base->clone();
                for (int statIndex = 0; statIndex < 5; ++statIndex)
                {
                    control->setEVWide(statIndex, 2560);
                    defective->setEVWide(statIndex, statIndex == 0 ? 100 : 0);
                }
                expectVerifierFlags(context, Legality::verifyEffortValues, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortShouldBeZero});
                return;
            }
            if (group == Enums::GameVersion::GG)
            {
                return;
            }
            {
                auto control = base->clone();
                control->setEgg(true);
                auto defective = control->clone();
                defective->setEVWide(0, 4);
                expectVerifierFlags(context, Legality::verifyEffortValues, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortEgg});
            }
            if (formatGeneration >= 6)
            {
                auto defective = base->clone();
                defective->setEVWide(0, 253);
                expectVerifierFlags(context, Legality::verifyEffortValues, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortAbove252});
            }
            if (formatGeneration == 3 || formatGeneration == 4)
            {
                // Effort values beyond vitamins (10 at a time, up to 100) need the EXP of the battles behind them.
                // EncounterInvalid stands in for a template: its lowest level is the met level.
                const Legality::EncounterInfo staticEncounter{Legality::EncounterType::Static, group};
                const uint8_t ownVersion = Enums::getGroupRepVersion(group);
                auto levelHundred = createPokemon(trainer, pikachuSpecies, 100);
                levelHundred->setOriginGame(ownVersion);
                levelHundred->setMetLevel(100);
                const auto levelHundredTemplate = Legality::Encounters::makeEncounter<Legality::Encounters::EncounterInvalid>(*levelHundred);
                auto vitaminOnly = levelHundred->clone();
                vitaminOnly->setEVWide(0, 100);
                auto trained = levelHundred->clone();
                trained->setEVWide(0, 101);
                expectVerifierFlags(context, Legality::verifyEffortValues, *vitaminOnly, *trained, staticEncounter, levelHundredTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortCap100});

                auto untrained = createPokemon(trainer, pikachuSpecies, 5);
                untrained->setOriginGame(ownVersion);
                untrained->setMetLevel(5);
                const auto levelFiveTemplate = Legality::Encounters::makeEncounter<Legality::Encounters::EncounterInvalid>(*untrained);
                auto untrainedVitamins = untrained->clone();
                untrainedVitamins->setEVWide(0, 100);
                auto untrainedOver = untrained->clone();
                untrainedOver->setEVWide(0, 101);
                expectVerifierFlags(context, Legality::verifyEffortValues, *untrainedVitamins, *untrainedOver, staticEncounter, levelFiveTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortUntrainedCap});

                auto barelyTrained = untrained->clone();
                barelyTrained->setExp(Pokemon::getExpForLevel(5, Pokemon::getGrowthRate(pikachuSpecies)) + 1);
                auto barelyTrainedVitamins = barelyTrained->clone();
                barelyTrainedVitamins->setEVWide(0, 100);
                auto barelyTrainedOver = barelyTrained->clone();
                barelyTrainedOver->setEVWide(0, 200);
                expectVerifierFlags(context, Legality::verifyEffortValues, *barelyTrainedVitamins, *barelyTrainedOver, staticEncounter, levelFiveTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::EffortValues, Legality::ResultCode::EffortUntrainedMoreEXP});
            }
        }

        void plantedPIDDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            const uint8_t ownVersion = Enums::getGroupRepVersion(group);
            if (formatGeneration >= 6)
            {
                constexpr uint16_t pikachuSpecies = 25;
                auto base = createPokemon(trainer, pikachuSpecies, 30);
                if (!base)
                {
                    return;
                }
                base->setOriginGame(ownVersion);
                base->setId32(0);
                auto control = base->clone();
                setPersonality(*control, 0x12345678, 0x87654321);
                auto equalDefective = base->clone();
                setPersonality(*equalDefective, 0x12345678, 0x12345678);
                expectVerifierFlags(context, Legality::verifyPID, *control, *equalDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::PIDEqualsEncryptionConstant});

                // A Gen 3-5 PID flipped as Poke Transfer would have, on a Pokemon that never transferred.
                auto xorDefective = base->clone();
                setPersonality(*xorDefective, 0x00080000, 0x80080000);
                expectVerifierFlags(context, Legality::verifyPID, *control, *xorDefective, uncheckedEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::TransferEncryptGen6Xor});

                constexpr uint8_t whiteVersion = 21;
                auto transferControl = base->clone();
                transferControl->setOriginGame(whiteVersion);
                setPersonality(*transferControl, 0x12345678, 0x12345678);
                auto transferDefective = transferControl->clone();
                setPersonality(*transferDefective, 0x12345678, 0x11111111);
                expectVerifierFlags(context, Legality::verifyPID, *transferControl, *transferDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::TransferEncryptGen6Equals});

                if (formatGeneration >= 8 && group != Enums::GameVersion::SWSH)
                {
                    // HOME flips bit 28 of a Switch FireRed/LeafGreen PID that the wider shiny odds would make shiny.
                    constexpr uint8_t fireRedVirtualConsoleVersion = 56;
                    auto homeControl = base->clone();
                    homeControl->setOriginGame(fireRedVirtualConsoleVersion);
                    setPersonality(*homeControl, 0x10080000, 0x00080000);
                    auto homeDefective = homeControl->clone();
                    setPersonality(*homeDefective, 0x00080000, 0x00080000);
                    expectVerifierFlags(context, Legality::verifyPID, *homeControl, *homeDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::TransferEncryptGen6BitFlip});
                }
            }

            // Wurmple's evolution is fixed by its encryption constant.
            {
                constexpr uint16_t wurmpleSpecies = 265;
                constexpr uint16_t beautiflySpecies = 267;
                auto beautifly = createPokemon(trainer, beautiflySpecies, 30);
                if (beautifly)
                {
                    const Legality::EncounterInfo wurmpleEncounter{Legality::EncounterType::Wild, group};
                    const Legality::Encounters::EncounterReference wurmpleSlot = findAreaSlotOfSpecies(Legality::Encounters::getAreas3R(), wurmpleSpecies);
                    auto control = beautifly->clone();
                    setPersonality(*control, 0x00010000, 0x00010000);
                    auto defective = beautifly->clone();
                    setPersonality(*defective, 0x00050000, 0x00050000);
                    expectVerifierFlags(context, Legality::verifyPID, *control, *defective, wurmpleEncounter, wurmpleSlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::PIDEncryptWurmple});
                }
            }

            if (formatGeneration == 4 || formatGeneration == 5)
            {
                // Day-care eggs: Gen 4 never stores PID 0, Gen 3 never a zero lower half, and Nidoran's
                // and Volbeat/Illumise's gender is bit 15.
                constexpr uint16_t nidoranFemaleSpecies = 29;
                auto nidoran = createPokemon(trainer, nidoranFemaleSpecies, 1);
                if (nidoran)
                {
                    nidoran->setGender(1);
                    const Legality::EncounterInfo eggFromDiamondPearl{Legality::EncounterType::Egg, Enums::GameVersion::DP};
                    const auto diamondEgg = Legality::Encounters::makeEncounter<Legality::Encounters::EncounterEgg4>(nidoranFemaleSpecies, Enums::GameVersion::D);
                    auto control = nidoran->clone();
                    setPersonality(*control, 0x00000001, 0x00000001);
                    auto zeroDefective = nidoran->clone();
                    setPersonality(*zeroDefective, 0, 0);
                    expectVerifierFlags(context, Legality::verifyPID, *control, *zeroDefective, eggFromDiamondPearl, diamondEgg, {Legality::Severity::Warning, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::PIDEncryptZero});
                    auto genderDefective = nidoran->clone();
                    setPersonality(*genderDefective, 0x00008001, 0x00008001);
                    expectVerifierFlags(context, Legality::verifyPID, *control, *genderDefective, eggFromDiamondPearl, diamondEgg, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::PIDGenderMismatch});
                }
                // Geodude, which no Ruby/Sapphire/Emerald gift or event hands out as an egg.
                constexpr uint16_t geodudeSpecies = 74;
                constexpr uint8_t rubyVersion = 2;
                auto rubyEgg = createPokemon(trainer, geodudeSpecies, 5);
                if (rubyEgg)
                {
                    rubyEgg->setOriginGame(rubyVersion);
                    const Legality::EncounterInfo eggFromRuby{Legality::EncounterType::Egg, Enums::GameVersion::RSE};
                    const auto rubyEggTemplate = Legality::Encounters::makeEncounter<Legality::Encounters::EncounterEgg3>(geodudeSpecies, Enums::GameVersion::RU);
                    auto control = rubyEgg->clone();
                    setPersonality(*control, 0x12340001, 0x12340001);
                    auto defective = rubyEgg->clone();
                    setPersonality(*defective, 0x12340000, 0x12340000);
                    expectVerifierFlags(context, Legality::verifyPID, *control, *defective, eggFromRuby, rubyEggTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::EncryptionConstant, Legality::ResultCode::PIDEncryptZero});
                }
            }

            if (formatGeneration >= 5)
            {
                // Black/White force a static's fixed gender into a random PID and tie a wild PID to the trainer ids.
                constexpr uint16_t pikachuSpecies = 25;
                auto base = createPokemon(trainer, pikachuSpecies, 30);
                if (base)
                {
                    base->setId32(0);
                    // A Black/White static that fixes a female gender, and a wild slot outside a Hidden Grotto.
                    const Legality::EncounterInfo staticEncounter{Legality::EncounterType::Static, Enums::GameVersion::BW};
                    const Legality::Encounters::EncounterReference femaleStatic = findTableTemplate(Legality::Encounters::getStatic5BW(), [](const Legality::Encounters::EncounterStatic5 &encounter) { return encounter.fixedGender() == 1; });
                    auto control = base->clone();
                    setPersonality(*control, 0x00000101, 0x00000101);
                    auto defective = base->clone();
                    setPersonality(*defective, 0x00000100, 0x00000100);
                    expectVerifierFlags(context, Legality::verifyPID, *control, *defective, staticEncounter, femaleStatic, {Legality::Severity::Invalid, Legality::CheckIdentifier::PID, Legality::ResultCode::EncounterConditionBadRNGFrame});

                    const Legality::EncounterInfo wildEncounter{Legality::EncounterType::Wild, Enums::GameVersion::BW};
                    const Legality::Encounters::EncounterReference wildSlot = findAreaSlot(Legality::Encounters::getAreas5B(), [](const Legality::Encounters::EncounterSlot5 &slot) { return !slot.isHiddenGrotto(); });
                    auto correlated = base->clone();
                    setPersonality(*correlated, 0x00000100, 0x00000100);
                    auto uncorrelated = base->clone();
                    setPersonality(*uncorrelated, 0x00000101, 0x00000101);
                    expectVerifierFlags(context, Legality::verifyPID, *correlated, *uncorrelated, wildEncounter, wildSlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::PID, Legality::ResultCode::PIDTypeMismatch});
                }
            }
        }

        void plantedGenderDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            if (formatGeneration < 6)
            {
                return;
            }
            const Legality::EncounterInfo uncheckedEncounter;
            constexpr uint16_t taurosSpecies = 128;
            auto tauros = createPokemon(trainer, taurosSpecies, 30);
            if (tauros)
            {
                tauros->setOriginGame(Enums::getGroupRepVersion(context.group));
                tauros->setGender(0);
                auto defective = tauros->clone();
                defective->setGender(1);
                expectVerifierFlags(context, Legality::verifyGender, *tauros, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Gender, Legality::ResultCode::GenderInvalidNone});
            }
            if (context.group == Enums::GameVersion::GG)
            {
                return;
            }
            // A Diamond Azurill that evolved keeps its PID gender under Azurill's ratio, which reads as a mismatch under Marill's.
            constexpr uint8_t diamondVersion = 10;
            constexpr uint16_t azumarillSpecies = 184;
            auto azumarill = createPokemon(trainer, azumarillSpecies, 30);
            if (azumarill)
            {
                azumarill->setOriginGame(diamondVersion);
                azumarill->setGender(1);
                auto control = azumarill->clone();
                setPersonality(*control, 0x00000096, 0x00000096);
                auto defective = azumarill->clone();
                setPersonality(*defective, 0x000000C8, 0x000000C8);
                expectVerifierFlags(context, Legality::verifyGender, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Gender, Legality::ResultCode::PIDGenderMismatch});
            }
            // Gallade's gender was decided by the Ralts it was met as.
            constexpr uint16_t raltsSpecies = 280;
            constexpr uint16_t galladeSpecies = 475;
            auto gallade = createPokemon(trainer, galladeSpecies, 30);
            if (gallade)
            {
                gallade->setOriginGame(diamondVersion);
                gallade->setGender(0);
                const Legality::EncounterInfo raltsEncounter{Legality::EncounterType::Wild, Enums::GameVersion::DP};
                const Legality::Encounters::EncounterReference raltsSlot = findAreaSlotOfSpecies(Legality::Encounters::getAreas4D(), raltsSpecies);
                auto control = gallade->clone();
                setPersonality(*control, 0x000000C8, 0x000000C8);
                auto defective = gallade->clone();
                setPersonality(*defective, 0x0000000A, 0x0000000A);
                expectVerifierFlags(context, Legality::verifyGender, *control, *defective, raltsEncounter, raltsSlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::Gender, Legality::ResultCode::PIDGenderMismatch});
            }
        }

        void plantedIndividualValueDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            if (formatGeneration < 7 || context.group == Enums::GameVersion::GG)
            {
                return;
            }
            constexpr uint16_t pikachuSpecies = 25;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            auto threeFlawless = base->clone();
            for (int statIndex = 0; statIndex < 3; ++statIndex)
            {
                threeFlawless->setIV(statIndex, 31);
            }
            // A Virtual Console transfer arrives with 3 perfect IVs.
            constexpr uint8_t redVersion = 35;
            auto virtualConsoleControl = threeFlawless->clone();
            virtualConsoleControl->setOriginGame(redVersion);
            auto virtualConsoleDefective = base->clone();
            virtualConsoleDefective->setOriginGame(redVersion);
            const Legality::EncounterInfo uncheckedEncounter;
            const auto virtualConsoleTransfer = Legality::Encounters::makeEncounter<Legality::Encounters::EncounterTransfer7>(Legality::Encounters::EncounterTransfer7::getVC1(pikachuSpecies, 30));
            expectVerifierFlags(context, Legality::verifyIndividualValues, *virtualConsoleControl, *virtualConsoleDefective, uncheckedEncounter, virtualConsoleTransfer, {Legality::Severity::Invalid, Legality::CheckIdentifier::IndividualValues, Legality::ResultCode::IVFlawlessCountAtLeast});

            if (formatGeneration == 7)
            {
                // A hidden ability from a Sun/Moon SOS chain comes with 2 perfect IVs.
                constexpr uint8_t hiddenAbilityNumber = 4;
                const Legality::EncounterInfo slotEncounter{Legality::EncounterType::Wild, Enums::GameVersion::SM};
                const Legality::Encounters::EncounterReference sunSlot = findAreaSlot(Legality::Encounters::getAreas7SN(), [](const Legality::Encounters::EncounterSlot7 &) { return true; });
                auto slotControl = threeFlawless->clone();
                slotControl->setAbilityNumber(hiddenAbilityNumber);
                auto slotDefective = base->clone();
                slotDefective->setAbilityNumber(hiddenAbilityNumber);
                expectVerifierFlags(context, Legality::verifyIndividualValues, *slotControl, *slotDefective, slotEncounter, sunSlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::IndividualValues, Legality::ResultCode::IVFlawlessCountAtLeast});
            }
        }

        void plantedTrainingDefects(Context &context, const Trainer::Trainer &trainer)
        {
            constexpr uint16_t pikachuSpecies = 25;
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            if (group == Enums::GameVersion::GG)
            {
                // Each level gained in Let's Go awakens one stat.
                auto awakened = createPokemon(trainer, pikachuSpecies, 30);
                if (awakened)
                {
                    awakened->setMetLevel(1);
                    auto control = awakened->clone();
                    for (int statIndex = 0; statIndex < 6; ++statIndex)
                    {
                        control->setAV(statIndex, 200);
                    }
                    expectVerifierFlags(context, Legality::verifyAwakeningValues, *control, *awakened, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::AwakeningValues, Legality::ResultCode::AwakenedStatAtLeast});
                }
            }

            auto trainable = createPokemon(trainer, pikachuSpecies, 100);
            if (!trainable || !trainable->hasHyperTraining())
            {
                return;
            }
            if (group == Enums::GameVersion::PLA)
            {
                // Legends: Arceus sells no Bottle Caps; only a HOME visit explains the flags.
                trainable->setHomeTracker(0);
                auto defective = trainable->clone();
                defective->setHyperTrained(Pokemon::Pokemon::HT_HP, true);
                expectVerifierFlags(context, Legality::verifyHyperTraining, *trainable, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::HyperPerfectUnavailable});
                return;
            }
            auto lowLevel = createPokemon(trainer, pikachuSpecies, 30);
            auto lowLevelDefective = lowLevel->clone();
            lowLevelDefective->setHyperTrained(Pokemon::Pokemon::HT_HP, true);
            expectVerifierFlags(context, Legality::verifyHyperTraining, *lowLevel, *lowLevelDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::HyperTrainLevelAtLeast});

            auto perfect = trainable->clone();
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                perfect->setIV(statIndex, 31);
            }
            auto perfectDefective = perfect->clone();
            perfectDefective->setHyperTrained(Pokemon::Pokemon::HT_HP, true);
            expectVerifierFlags(context, Legality::verifyHyperTraining, *perfect, *perfectDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::HyperPerfectAll});

            auto onePerfect = trainable->clone();
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                onePerfect->setIV(statIndex, 0);
            }
            onePerfect->setIV(0, 31);
            auto onePerfectControl = onePerfect->clone();
            onePerfectControl->setHyperTrained(Pokemon::Pokemon::HT_ATK, true);
            auto onePerfectDefective = onePerfect->clone();
            onePerfectDefective->setHyperTrained(Pokemon::Pokemon::HT_HP, true);
            expectVerifierFlags(context, Legality::verifyHyperTraining, *onePerfectControl, *onePerfectDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::HyperPerfectOne});
        }

        /// The word filter has no regex engine (PKSE expands PKHeX's patterns at generation time and
        /// matches them itself), so its matcher is tested on its own: a plain substring pattern, one
        /// that needs case folding, a character-class expression, and names that must pass.
        void wordFilterMatcher(Context &context)
        {
            Legality::WordFilterMatch filterMatch;
            context.check(Legality::isWordFiltered(u"ab14eightyeightcd", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter misses a Switch substring pattern", "message=ab14eightyeightcd");
            context.check(Legality::isWordFiltered(u"14EightyEight", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter does not fold case before matching", "message=14EightyEight");
            context.check(Legality::isWordFiltered(u"xxgilipxx", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter misses a character-class expression", "message=xxgilipxx");
            context.check(Legality::isWordFiltered(u"xxjilipxx", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter misses the second alternative of a character class", "message=xxjilipxx");
            context.check(!Legality::isWordFiltered(u"Pikachu", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter flags a species name", "message=Pikachu");
            context.check(!Legality::isWordFiltered(u"Ash", Enums::GameVersion::SWSH, Enums::GameVersion::SWSH, filterMatch), "word filter flags an ordinary trainer name", "message=Ash");
            context.check(Utils::isGen2KoreanCharacter(u'\uAC00'), "Korean Gold/Silver character table is missing a Hangul syllable", "character=U+AC00");
            context.check(!Utils::isGen2KoreanCharacter(u'\u6F22'), "Korean Gold/Silver character table holds a kanji", "character=U+6F22");
        }

        void plantedNameDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            constexpr uint16_t pikachuSpecies = 25;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            base->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            base->setOTName(u"Ash");
            base->setTID16(1000);
            base->setSID16(2000);
            base->refreshChecksum();
            Legality::EncounterInfo wildEncounter;
            wildEncounter.type = Legality::EncounterType::Wild;

            if (formatGeneration < 3)
            {
                return; // a Game Boy record has no language byte to plant a bad value in
            }
            {
                auto defective = base->clone();
                defective->setLanguage(50);
                expectVerifierFlags(context, Legality::verifyLanguage, *base, *defective, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Language, Legality::ResultCode::OTLanguageShouldBeAtMost});
            }
            {
                auto defective = base->clone();
                defective->setTID16(12345);
                defective->setSID16(54321);
                expectVerifierFlags(context, Legality::verifyTrainerId, *base, *defective, wildEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::Trainer, Legality::ResultCode::OTSuspicious});
            }
            {
                auto defective = base->clone();
                defective->setSID16(1000);
                expectVerifierFlags(context, Legality::verifyTrainerId, *base, *defective, wildEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::Trainer, Legality::ResultCode::OTIdEqual});
            }
            {
                auto defective = base->clone();
                defective->setOTName(u"pkhex");
                expectVerifierFlags(context, Legality::verifyTrainerName, *base, *defective, wildEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::Trainer, Legality::ResultCode::OTSuspicious});
            }
            if (formatGeneration >= 6)
            {
                // Six digits: one past the limit for a name entered in Gen 6 or later.
                auto defective = base->clone();
                defective->setOTName(u"123456");
                expectVerifierFlags(context, Legality::verifyTrainerName, *base, *defective, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Trainer, Legality::ResultCode::WordFilterTooManyNumbers});
            }
        }

        void plantedVerifierDefects(Context &context, const Trainer::Trainer &trainer)
        {
            const int formatGeneration = getGroupGeneration(context.group);
            wordFilterMatcher(context);
            plantedNameDefects(context, trainer, formatGeneration);
            plantedEffortValueDefects(context, trainer, formatGeneration);
            if (formatGeneration < 3)
            {
                return;
            }
            plantedPIDDefects(context, trainer, formatGeneration);
            plantedGenderDefects(context, trainer, formatGeneration);
            plantedIndividualValueDefects(context, trainer, formatGeneration);
            plantedTrainingDefects(context, trainer);
        }
    }

    // ---- Planted defects for the level, PP, held item, contest stat, marking, console region, ground
    // tile and medal verifiers: the branches neither the real saves nor PKHeX's test files reach. The
    // record fields these verifiers read have no setter (the editor never writes them), so the defects
    // are written straight into the record at the offsets the format's accessors read.
    namespace
    {
        void writeRecordByte(Pokemon::Pokemon &pokemon, size_t recordOffset, uint8_t byteValue)
        {
            std::span<std::byte> recordData = pokemon.getData();
            if (recordOffset < recordData.size())
            {
                recordData[recordOffset] = std::byte{byteValue};
            }
        }

        std::unique_ptr<Pokemon::Pokemon> createOwnPokemon(const Trainer::Trainer &trainer, Enums::GameVersion group)
        {
            constexpr uint16_t pikachuSpecies = 25;
            constexpr uint16_t tackleMove = 33;
            auto pokemon = createPokemon(trainer, pikachuSpecies, 30);
            if (!pokemon)
            {
                return nullptr;
            }
            pokemon->setOriginGame(Enums::getGroupRepVersion(group));
            pokemon->setMove(0, tackleMove);
            pokemon->setMovePP(0, 1);
            pokemon->refreshChecksum();
            return pokemon;
        }

        /// Where the format keeps its markings, and a value that breaks them; offset 0 for none.
        struct MarkingDefect
        {
            size_t recordOffset;
            uint8_t defectValue;
            Legality::ResultCode code;
        };

        MarkingDefect getMarkingDefect(Enums::GameVersion group)
        {
            switch (group)
            {
            case Enums::GameVersion::RSE:
            case Enums::GameVersion::FRLG:
                return {0x1B, 0x10, Legality::ResultCode::MarkValueUnusedBitsPresent};
            case Enums::GameVersion::DP:
            case Enums::GameVersion::PT:
            case Enums::GameVersion::HGSS:
            case Enums::GameVersion::BW:
            case Enums::GameVersion::B2W2:
                return {0x16, 0x40, Legality::ResultCode::MarkValueUnusedBitsPresent};
            case Enums::GameVersion::XY:
            case Enums::GameVersion::ORAS:
                return {0x2A, 0x40, Legality::ResultCode::MarkValueUnusedBitsPresent};
            case Enums::GameVersion::SM:
            case Enums::GameVersion::USUM:
            case Enums::GameVersion::GG:
                return {0x16, 0x03, Legality::ResultCode::MarkValueOutOfRange};
            case Enums::GameVersion::SWSH:
            case Enums::GameVersion::BDSP:
            case Enums::GameVersion::PLA:
            case Enums::GameVersion::SV:
            case Enums::GameVersion::ZA:
                return {0x18, 0x03, Legality::ResultCode::MarkValueOutOfRange};
            default:
                return {0, 0, Legality::ResultCode::MarkValueUnusedBitsPresent};
            }
        }

        /// The day-care egg template (PKHeX EncounterEgg2-9) of the save group's games; null for a
        /// group with no breeding (Gen 1, Let's Go, Legends: Arceus, Legends: Z-A).
        Legality::Encounters::EncounterReference makeEggTemplate(Enums::GameVersion group, uint16_t speciesId, uint8_t formId)
        {
            namespace Encounters = Legality::Encounters;
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(Enums::getGroupRepVersion(group));
            switch (group)
            {
            case Enums::GameVersion::GSC:
                return Encounters::makeEncounter<Encounters::EncounterEgg2>(speciesId, version);
            case Enums::GameVersion::RSE:
            case Enums::GameVersion::FRLG:
                return Encounters::makeEncounter<Encounters::EncounterEgg3>(speciesId, version);
            case Enums::GameVersion::DP:
            case Enums::GameVersion::PT:
            case Enums::GameVersion::HGSS:
                return Encounters::makeEncounter<Encounters::EncounterEgg4>(speciesId, version);
            case Enums::GameVersion::BW:
            case Enums::GameVersion::B2W2:
                return Encounters::makeEncounter<Encounters::EncounterEgg5>(speciesId, formId, version);
            case Enums::GameVersion::XY:
            case Enums::GameVersion::ORAS:
                return Encounters::makeEncounter<Encounters::EncounterEgg6>(speciesId, formId, version);
            case Enums::GameVersion::SM:
            case Enums::GameVersion::USUM:
                return Encounters::makeEncounter<Encounters::EncounterEgg7>(speciesId, formId, version);
            case Enums::GameVersion::SWSH:
                return Encounters::makeEncounter<Encounters::EncounterEgg8>(speciesId, formId, version);
            case Enums::GameVersion::BDSP:
                return Encounters::makeEncounter<Encounters::EncounterEgg8b>(speciesId, formId, version);
            case Enums::GameVersion::SV:
                return Encounters::makeEncounter<Encounters::EncounterEgg9>(speciesId, formId, version);
            default:
                return nullptr;
            }
        }

        void plantedLevelAndMoveDefects(Context &context, const Pokemon::Pokemon &base)
        {
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            {
                auto defective = base.clone();
                defective->setMovePP(0, 99);
                expectVerifierFlags(context, Legality::verifyMovePP, base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::CurrentMove, Legality::ResultCode::MovePPTooHigh});
            }
            // The words: an empty slot holding PP is called empty, not "above the amount allowed" with
            // the stored PP printed as if it were the limit (a HOME gift Fuecoco's empty fourth slot
            // holds 35 PP), and a real move's message names both the stored PP and its limit.
            {
                auto defective = base.clone();
                defective->setMove(3, 0);
                defective->setMovePPUps(3, 0);
                defective->setMovePP(3, 99);
                defective->setMovePP(0, 99);
                defective->refreshChecksum();
                bool emptySlotDescribed = false;
                bool movePPDescribed = false;
                std::string reportedTexts;
                for (const Legality::Issue &issue : runVerifier(Legality::verifyMovePP, *defective, group, uncheckedEncounter).issues)
                {
                    if (issue.code != Legality::ResultCode::MovePPTooHigh)
                    {
                        continue;
                    }
                    reportedTexts += " \"" + issue.text + "\"";
                    emptySlotDescribed = emptySlotDescribed || issue.text.rfind("Move 4 is empty, but the slot still stores 99 PP.", 0) == 0;
                    movePPDescribed = movePPDescribed || (issue.text.rfind("Move 1 has 99 PP, more than the ", 0) == 0 && defective->move(0) != 0);
                }
                context.check(emptySlotDescribed, "an empty move slot holding PP is not described as empty", "texts:" + reportedTexts);
                context.check(movePPDescribed || defective->move(0) == 0, "a move with too much PP does not name its stored PP and its limit", "texts:" + reportedTexts);
            }
            const bool hasPPUps = group != Enums::GameVersion::PLA && group != Enums::GameVersion::ZA;
            if (hasPPUps)
            {
                constexpr uint16_t sketchMove = 166;
                auto control = base.clone();
                control->setMovePPUps(0, 1);
                auto defective = control->clone();
                defective->setMove(0, sketchMove);
                expectVerifierFlags(context, Legality::verifyMovePP, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::CurrentMove, Legality::ResultCode::MovePPUpsTooHigh});
            }

            const Legality::Encounters::EncounterReference eggTemplate = makeEggTemplate(group, base.speciesID(), base.form());
            auto egg = base.clone();
            egg->setEgg(true);
            if (!eggTemplate || !egg->isEgg())
            {
                return;
            }
            const uint8_t growthRate = Pokemon::getGrowthRate(base.speciesID());
            const uint8_t hatchLevel = eggTemplate->levelMin();
            egg->setExp(Pokemon::getExpForLevel(hatchLevel, growthRate));
            egg->setMovePP(0, 0);
            const Legality::EncounterInfo eggEncounter{Legality::EncounterType::Egg, group};
            {
                auto defective = egg->clone();
                defective->setExp(Pokemon::getExpForLevel(hatchLevel, growthRate) + 1);
                expectVerifierFlags(context, Legality::verifyLevel, *egg, *defective, eggEncounter, eggTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::Level, Legality::ResultCode::EggEXP});
            }
            {
                auto defective = egg->clone();
                defective->setExp(Pokemon::getExpForLevel(10, growthRate));
                expectVerifierFlags(context, Legality::verifyLevel, *egg, *defective, eggEncounter, eggTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::Level, Legality::ResultCode::EggFMetLevel});
            }
            {
                auto defective = egg->clone();
                defective->setHeldItem(1);
                expectVerifierFlags(context, Legality::verifyHeldItem, *egg, *defective, eggEncounter, eggTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::HeldItem, Legality::ResultCode::ItemEgg});
            }
            if (hasPPUps)
            {
                auto defective = egg->clone();
                defective->setMovePPUps(0, 1);
                expectVerifierFlags(context, Legality::verifyMovePP, *egg, *defective, eggEncounter, eggTemplate, {Legality::Severity::Invalid, Legality::CheckIdentifier::Egg, Legality::ResultCode::EggPPUp});
            }
        }

        void plantedRecordFieldDefects(Context &context, const Pokemon::Pokemon &base, int formatGeneration)
        {
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            {
                // Beyond every format's item list.
                auto defective = base.clone();
                defective->setHeldItem(2000);
                expectVerifierFlags(context, Legality::verifyHeldItem, base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::HeldItem, Legality::ResultCode::ItemUnreleased});
            }
            if (formatGeneration == 3)
            {
                constexpr uint16_t enigmaBerryItemGen3 = 175;
                auto defective = base.clone();
                defective->setHeldItem(enigmaBerryItemGen3);
                expectVerifierFlags(context, Legality::verifyHeldItem, base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::HeldItem, Legality::ResultCode::ItemUnreleased});
            }

            const MarkingDefect markingDefect = getMarkingDefect(group);
            if (markingDefect.recordOffset != 0)
            {
                auto defective = base.clone();
                writeRecordByte(*defective, markingDefect.recordOffset, markingDefect.defectValue);
                expectVerifierFlags(context, Legality::verifyMarkings, base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Marking, markingDefect.code});
            }
            if (base.hasFavoriteFlag() && group != Enums::GameVersion::GG)
            {
                constexpr size_t favoriteOffset = 0x16;
                auto defective = base.clone();
                writeRecordByte(*defective, favoriteOffset, 0x08);
                expectVerifierFlags(context, Legality::verifyMarkings, base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Marking, Legality::ResultCode::FavoriteMarkingUnavailable});
            }

            if (base.hasConsoleRegion())
            {
                constexpr size_t consoleCountryOffset = 0xE0;
                constexpr size_t consoleRegionOffset = 0xE2;
                constexpr uint8_t japanCountry = 1;
                auto control = base.clone();
                writeRecordByte(*control, consoleCountryOffset, japanCountry);
                writeRecordByte(*control, consoleRegionOffset, 0);
                auto outOfRange = control->clone();
                writeRecordByte(*outOfRange, consoleRegionOffset, 7);
                expectVerifierFlags(context, Legality::verifyConsoleRegion, *control, *outOfRange, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Geography, Legality::ResultCode::GeographyHardwareRange});
                auto mismatched = control->clone();
                writeRecordByte(*mismatched, consoleRegionOffset, 1);
                expectVerifierFlags(context, Legality::verifyConsoleRegion, *control, *mismatched, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Geography, Legality::ResultCode::GeographyHardwareInvalid});
            }

            if (base.hasGroundTile())
            {
                const size_t groundTileOffset = formatGeneration == 6 ? 0xDE : 0x85;
                auto tiled = base.clone();
                writeRecordByte(*tiled, groundTileOffset, 2);
                const Legality::EncounterInfo wildEncounter{Legality::EncounterType::Wild, group};
                if (formatGeneration == 4)
                {
                    // A Gen 4 trade is never met on a tile; a wild one may be.
                    const Legality::EncounterInfo tradeEncounter{Legality::EncounterType::Trade, group};
                    context.check(runVerifier(Legality::verifyGroundTile, *tiled, group, wildEncounter).issues.empty(), "legality verifier flags a ground tile on a Gen 4 wild encounter, which may have one", Enums::getGameVersionName(group));
                    expectVerifierFlags(context, Legality::verifyGroundTile, base, *tiled, tradeEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::EncounterTypeMismatch});
                }
                else
                {
                    expectVerifierFlags(context, Legality::verifyGroundTile, base, *tiled, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::EncounterTypeMismatch});
                }
            }

            if (base.hasSuperTraining())
            {
                constexpr size_t superTrainFlagsOffset = 0x2C;
                constexpr size_t distributionFlagsOffset = 0x3A;
                auto unusedFlag = base.clone();
                writeRecordByte(*unusedFlag, superTrainFlagsOffset, 0x01);
                expectVerifierFlags(context, Legality::verifyMedals, base, *unusedFlag, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::SuperUnused});
                auto distribution = base.clone();
                writeRecordByte(*distribution, distributionFlagsOffset, 0x01);
                expectVerifierFlags(context, Legality::verifyMedals, base, *distribution, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::SuperDistribution});
            }
            if (base.hasTrainingBag())
            {
                constexpr size_t trainingBagHitsOffset = 0x16;
                constexpr size_t trainingBagOffset = 0x17;
                auto unknownBag = base.clone();
                writeRecordByte(*unknownBag, trainingBagOffset, 27);
                expectVerifierFlags(context, Legality::verifyMedals, base, *unknownBag, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::Gen6SuperTrainBagInvalid});
                // The smallest HP bag takes two hits.
                auto control = base.clone();
                writeRecordByte(*control, trainingBagOffset, 1);
                writeRecordByte(*control, trainingBagHitsOffset, 2);
                auto overHit = control->clone();
                writeRecordByte(*overHit, trainingBagHitsOffset, 3);
                expectVerifierFlags(context, Legality::verifyMedals, *control, *overHit, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Training, Legality::ResultCode::Gen6SuperTrainBagHitsInvalid});
            }

            // Contest stats with no way to have earned them: Sword/Shield, Legends: Arceus and Scarlet/Violet
            // hold no contests, and a Gen 3 Pokemon with no contest stat cannot have gained sheen.
            const Legality::EncounterInfo ownWildEncounter{Legality::EncounterType::Wild, group};
            if (group == Enums::GameVersion::SWSH || group == Enums::GameVersion::PLA || group == Enums::GameVersion::SV)
            {
                constexpr size_t coolnessOffset = 0x2C;
                auto defective = base.clone();
                writeRecordByte(*defective, coolnessOffset, 10);
                expectVerifierFlags(context, Legality::verifyContestStats, base, *defective, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::ContestZero});
            }
            if (formatGeneration == 3)
            {
                constexpr size_t sheenOffset = 0x3E + 5;
                auto defective = base.clone();
                writeRecordByte(*defective, sheenOffset, 255);
                expectVerifierFlags(context, Legality::verifyContestStats, base, *defective, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::ContestSheenAtMost});
            }
        }

        void plantedRecordDefects(Context &context, const Trainer::Trainer &trainer)
        {
            const int formatGeneration = getGroupGeneration(context.group);
            if (formatGeneration < 3)
            {
                return;
            }
            auto base = createOwnPokemon(trainer, context.group);
            if (!context.check(base != nullptr, "createBlankPokemon() returned nothing"))
            {
                return;
            }
            plantedLevelAndMoveDefects(context, *base);
            plantedRecordFieldDefects(context, *base, formatGeneration);
        }
    }

    // ---- Planted defects for the transfer and Colosseum/XD verifiers: the transfer routes (Pal Park,
    // Poke Transfer, the Lock Capsule, the 3DS and Switch Virtual Consoles, HOME) the real saves and
    // PKHeX's test files leave unflagged.
    namespace
    {
        constexpr uint16_t POKE_TRANSFER_LOCATION = 30001;

        void plantedGameBoyTransferDefects(Context &context, const Trainer::Trainer &trainer)
        {
            constexpr uint16_t pikachuSpecies = 25;
            constexpr uint16_t unownSpecies = 201;
            constexpr uint8_t redVersion = 35;
            constexpr uint8_t goldVersion = 39;
            constexpr uint16_t transferGenerationOneLocation = 30013;
            constexpr size_t consoleRegionOffset = 0xE2;
            const Legality::EncounterInfo uncheckedEncounter;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            base->setOriginGame(redVersion);
            base->setMetLocation(transferGenerationOneLocation);
            base->setMetLevel(30);
            const bool isGenerationSevenFormat = getGroupGeneration(context.group) == 7;
            base->setEggLocation(context.group == Enums::GameVersion::BDSP ? 0xFFFF : 0);
            base->setOTGender(0);
            base->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            if (isGenerationSevenFormat)
            {
                writeRecordByte(*base, consoleRegionOffset, 1);
            }
            base->setId32(0);
            setPersonality(*base, 0x12345678, 0x12345678);
            {
                auto defective = base->clone();
                defective->setOTGender(1);
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::Gen2OTGender});
            }
            {
                auto defective = base->clone();
                defective->setMetLocation(POKE_TRANSFER_LOCATION);
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferMetLocation});
            }
            {
                auto defective = base->clone();
                defective->setEggLocation(POKE_TRANSFER_LOCATION);
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::EggLocationNone});
            }
            {
                // Medium Fast at met level 2 cannot be Brave (nature 2); Hardy (0) it can.
                auto control = base->clone();
                control->setMetLevel(2);
                control->setNature(0);
                auto defective = control->clone();
                defective->setNature(2);
                expectVerifierFlags(context, Legality::verifyTransfer, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferNature});
            }
            {
                auto defective = base->clone();
                setPersonality(*defective, 0x00010000, 0x00010000);
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::PID, Legality::ResultCode::EncounterStaticPIDShiny});
            }
            if (isGenerationSevenFormat)
            {
                auto defective = base->clone();
                writeRecordByte(*defective, consoleRegionOffset, 5);
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Language, Legality::ResultCode::OTLanguageCannotTransferToConsoleRegion});
            }
            {
                // A Gen 2 shiny Unown's DVs spell only I or V.
                auto control = base->clone();
                control->setSpecies(unownSpecies);
                control->setOriginGame(goldVersion);
                control->setMetLocation(30017);
                control->setForm(8);
                setPersonality(*control, 0, 0);
                auto defective = control->clone();
                defective->setForm(0);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::PID, Legality::ResultCode::EncounterStaticPIDShiny});
            }
        }

        void plantedEarlierGenerationTransferDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            constexpr uint16_t pikachuSpecies = 25;
            constexpr uint16_t celebiSpecies = 251;
            constexpr uint8_t rubyVersion = 2;
            constexpr uint8_t diamondVersion = 10;
            constexpr uint16_t palParkLocation = 0x37;
            constexpr uint16_t celebiLockCapsuleLocation = 30010;
            const Legality::EncounterInfo uncheckedEncounter;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            if (formatGeneration > 3)
            {
                auto control = base->clone();
                control->setOriginGame(rubyVersion);
                control->setMetLocation(formatGeneration == 4 ? palParkLocation : POKE_TRANSFER_LOCATION);
                control->refreshChecksum();
                auto defective = control->clone();
                defective->setMetLocation(1);
                defective->refreshChecksum();
                const Legality::ResultCode expectedCode = formatGeneration == 4 ? Legality::ResultCode::EggLocationPalPark : Legality::ResultCode::TransferEggLocationTransporter;
                expectVerifierFlags(context, Legality::verifyTransfer, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, expectedCode});
            }
            if (formatGeneration >= 5)
            {
                auto control = base->clone();
                control->setOriginGame(diamondVersion);
                control->setSpecies(celebiSpecies);
                control->setMetLocation(celebiLockCapsuleLocation);
                control->refreshChecksum();
                auto defective = control->clone();
                defective->setMetLocation(1);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferMet});
                auto lockCapsulePikachu = control->clone();
                lockCapsulePikachu->setSpecies(pikachuSpecies);
                lockCapsulePikachu->refreshChecksum();
                auto transferredPikachu = lockCapsulePikachu->clone();
                transferredPikachu->setMetLocation(POKE_TRANSFER_LOCATION);
                transferredPikachu->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *transferredPikachu, *lockCapsulePikachu, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferEggLocationTransporter});
            }
            if (formatGeneration == 3)
            {
                constexpr uint8_t colosseumXDVersion = 15;
                auto control = base->clone();
                control->setOriginGame(colosseumXDVersion);
                control->setOTGender(0);
                control->refreshChecksum();
                auto defective = control->clone();
                defective->setOTGender(1);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyColosseumXD, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Trainer, Legality::ResultCode::Gen3OTGender});
            }
        }

        void plantedHomeTransferDefects(Context &context, const Trainer::Trainer &trainer)
        {
            constexpr uint16_t pikachuSpecies = 25;
            constexpr uint16_t rockruffSpecies = 744;
            constexpr uint8_t sunVersion = 30;
            const Enums::GameVersion group = context.group;
            const Legality::EncounterInfo uncheckedEncounter;
            auto base = createPokemon(trainer, pikachuSpecies, 30);
            if (!base)
            {
                return;
            }
            base->setOriginGame(sunVersion);
            base->setMetLocation(POKE_TRANSFER_LOCATION);
            base->setHomeTracker(1);
            base->refreshChecksum();
            {
                auto defective = base->clone();
                defective->setHomeTracker(0);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferTrackerMissing});
            }
            {
                // No game holds a Pikachu form past its form count.
                auto defective = base->clone();
                defective->setForm(60);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferBad});
            }
            if (group == Enums::GameVersion::SV)
            {
                // Rockruff never reached Legends: Arceus, the one route that leaves a (0,0) size.
                constexpr size_t heightScalarOffset = 0x48;
                constexpr size_t weightScalarOffset = 0x49;
                auto control = base->clone();
                control->setSpecies(rockruffSpecies);
                writeRecordByte(*control, heightScalarOffset, 1);
                writeRecordByte(*control, weightScalarOffset, 0);
                control->refreshChecksum();
                auto defective = control->clone();
                writeRecordByte(*defective, heightScalarOffset, 0);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *control, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferBad});

                // Switch FireRed/LeafGreen through HOME.
                constexpr uint8_t fireRedVirtualConsoleVersion = 56;
                constexpr uint16_t homeVirtualConsoleLocation = 30007;
                constexpr uint8_t premierBall = 12;
                auto fireRedControl = base->clone();
                fireRedControl->setOriginGame(fireRedVirtualConsoleVersion);
                fireRedControl->setMetLocation(homeVirtualConsoleLocation);
                fireRedControl->setMetYear(26);
                fireRedControl->setMetMonth(10);
                fireRedControl->setMetDay(10);
                fireRedControl->setBall(4);
                fireRedControl->refreshChecksum();
                auto earlyDefective = fireRedControl->clone();
                earlyDefective->setMetDay(5);
                earlyDefective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *fireRedControl, *earlyDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::DateOutsideDistributionWindow});
                auto ballDefective = fireRedControl->clone();
                ballDefective->setBall(premierBall);
                ballDefective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *fireRedControl, *ballDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::BallUnavailable});
                auto locationDefective = fireRedControl->clone();
                locationDefective->setMetLocation(POKE_TRANSFER_LOCATION);
                locationDefective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyTransfer, *fireRedControl, *locationDefective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::TransferMetLocation});
            }
        }

        void plantedTransferDefects(Context &context, const Trainer::Trainer &trainer)
        {
            const int formatGeneration = getGroupGeneration(context.group);
            if (formatGeneration < 3)
            {
                return;
            }
            plantedEarlierGenerationTransferDefects(context, trainer, formatGeneration);
            if (formatGeneration >= 7 && context.group != Enums::GameVersion::GG)
            {
                plantedGameBoyTransferDefects(context, trainer);
            }
            if (formatGeneration >= 8)
            {
                plantedHomeTransferDefects(context, trainer);
            }
        }
    }

    // ---- Planted defects for the ribbon and mark verifiers: the real saves hold no ribbon PKHeX
    // rejects, so these are the only Invalid cases they meet. Ribbons are written through the
    // format's ribbon table, the same rows the verifiers read.
    namespace
    {
        bool setRibbon(Pokemon::Pokemon &pokemon, Enums::GameVersion group, std::string_view spriteStem, uint8_t ribbonValue)
        {
            for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(group))
            {
                if (spriteStem == ribbon.spriteStem)
                {
                    Names::setRibbonValue(reinterpret_cast<uint8_t *>(pokemon.getData().data()), ribbon, ribbonValue);
                    pokemon.refreshChecksum();
                    return true;
                }
            }
            return false;
        }

        void plantedRibbonDefects(Context &context, const Trainer::Trainer &trainer)
        {
            constexpr uint8_t ultraSunVersion = 32;
            auto base = createOwnPokemon(trainer, context.group);
            if (!base)
            {
                return;
            }
            Legality::EncounterInfo wildEncounter;
            wildEncounter.type = Legality::EncounterType::Wild;
            wildEncounter.originGroup = context.group;
            {
                // An event ribbon on a wild catch.
                auto defective = base->clone();
                if (setRibbon(*defective, context.group, "ribboncountry", 1))
                {
                    expectVerifierFlags(context, Legality::verifyRibbons, *base, *defective, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsInvalid});
                }
            }
            if (context.group == Enums::GameVersion::ORAS || context.group == Enums::GameVersion::BDSP)
            {
                // Every Master Rank contest won, but no Contest Star.
                auto control = base->clone();
                for (const std::string_view spriteStem : {"ribbonmastercoolness", "ribbonmasterbeauty", "ribbonmastercuteness", "ribbonmastercleverness", "ribbonmastertoughness"})
                {
                    setRibbon(*control, context.group, spriteStem, 1);
                }
                auto defective = control->clone();
                setRibbon(*control, context.group, "ribbonconteststar", 1);
                expectVerifierFlags(context, Legality::verifyRibbons, *control, *defective, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsMissing});
            }
            const size_t affixedOffset = Conversion::affixedRibbonOffset(context.group);
            if (affixedOffset == 0)
            {
                return;
            }
            writeRecordByte(*base, affixedOffset, Conversion::AFFIXED_RIBBON_NONE);
            base->refreshChecksum();
            {
                // A Sword/Shield mark on a Pokemon from Ultra Sun.
                auto control = base->clone();
                control->setOriginGame(ultraSunVersion);
                auto defective = control->clone();
                setRibbon(*defective, context.group, "ribbonmarkrare", 1);
                expectVerifierFlags(context, Legality::verifyMarks, *control, *defective, Legality::EncounterInfo{}, {Legality::Severity::Invalid, Legality::CheckIdentifier::RibbonMark, Legality::ResultCode::RibbonMarkingInvalid});
            }
            {
                // The Kalos Champion Ribbon (RibbonIndex 0) affixed without being held.
                auto defective = base->clone();
                writeRecordByte(*defective, affixedOffset, 0);
                defective->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyMarks, *base, *defective, wildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::RibbonMark, Legality::ResultCode::RibbonMarkingAffixed});
            }
        }
    }

    int legalityPlan(Context &context)
    {
        if (!context.trainer) return 0;
        return 1 + (int)context.trainer->getBoxCount();
    }

    // ---- Planted defects for the ability, ball and form verifiers: the Safari Zone's fixed ball, a
    // Gen 3 Unown letter and the form arguments no test save holds.
    namespace
    {
        void writeRecordUInt32(Pokemon::Pokemon &pokemon, size_t recordOffset, uint32_t recordValue)
        {
            for (size_t byteIndex = 0; byteIndex < 4; byteIndex++)
            {
                writeRecordByte(pokemon, recordOffset + byteIndex, static_cast<uint8_t>(recordValue >> (8 * byteIndex)));
            }
            pokemon.refreshChecksum();
        }

        void plantedAbilityDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            if (formatGeneration < 4 || context.group == Enums::GameVersion::ZA)
            {
                return;
            }
            constexpr uint16_t bulbasaurSpecies = 1;
            constexpr uint16_t overgrowAbility = 65;
            constexpr uint16_t blazeAbility = 66;
            const Legality::EncounterInfo uncheckedEncounter;
            auto bulbasaur = createPokemon(trainer, bulbasaurSpecies, 30);
            if (!bulbasaur)
            {
                return;
            }
            bulbasaur->setOriginGame(Enums::getGroupRepVersion(context.group));
            bulbasaur->setAbility(overgrowAbility);
            bulbasaur->setAbilityNumber(1);
            bulbasaur->refreshChecksum();
            auto foreignAbility = bulbasaur->clone();
            foreignAbility->setAbility(blazeAbility);
            foreignAbility->refreshChecksum();
            expectVerifierFlags(context, Legality::verifyAbility, *bulbasaur, *foreignAbility, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ability, Legality::ResultCode::AbilityUnexpected});
            if (formatGeneration >= 6)
            {
                // Overgrow claimed from the Hidden Ability slot, which holds Chlorophyll.
                auto wrongSlot = bulbasaur->clone();
                wrongSlot->setAbilityNumber(4);
                wrongSlot->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyAbility, *bulbasaur, *wrongSlot, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ability, Legality::ResultCode::AbilityMismatch});
            }
        }

        void plantedBallDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            if (formatGeneration != 3 && formatGeneration != 4)
            {
                return;
            }
            // The Safari Zone and the Great Marsh fix the Safari Ball without the table saying so.
            constexpr uint16_t nidoranSpecies = 29;
            constexpr uint8_t fireRedVersion = 4;
            constexpr uint8_t diamondVersion = 10;
            constexpr uint16_t kantoSafariZone = 136;
            constexpr uint16_t greatMarsh = 52;
            constexpr uint8_t pokeBall = 4;
            constexpr uint8_t safariBall = 5;
            auto nidoran = createPokemon(trainer, nidoranSpecies, 25);
            if (!nidoran)
            {
                return;
            }
            const bool isGenerationThree = formatGeneration == 3;
            nidoran->setOriginGame(isGenerationThree ? fireRedVersion : diamondVersion);
            nidoran->setBall(safariBall);
            nidoran->refreshChecksum();
            const Legality::EncounterInfo safariEncounter{Legality::EncounterType::Wild, isGenerationThree ? Enums::GameVersion::FRLG : Enums::GameVersion::DP};
            const auto isSafariSlot = [isGenerationThree, kantoSafariZone, greatMarsh](const auto &slot) { return slot.location() == (isGenerationThree ? kantoSafariZone : greatMarsh); };
            const Legality::Encounters::EncounterReference safariSlot = isGenerationThree ? findAreaSlot(Legality::Encounters::getAreas3FR(), isSafariSlot) : findAreaSlot(Legality::Encounters::getAreas4D(), isSafariSlot);
            auto pokeBallCatch = nidoran->clone();
            pokeBallCatch->setBall(pokeBall);
            pokeBallCatch->refreshChecksum();
            expectVerifierFlags(context, Legality::verifyBall, *nidoran, *pokeBallCatch, safariEncounter, safariSlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ball, Legality::ResultCode::BallEncounterMismatch});
        }

        void plantedFormDefects(Context &context, const Trainer::Trainer &trainer, int formatGeneration)
        {
            const Legality::EncounterInfo uncheckedEncounter;
            if (formatGeneration == 4)
            {
                // A Gen 3 Unown's letter is its PID's; PID 0 is A.
                constexpr uint16_t unownSpecies = 201;
                constexpr uint8_t fireRedVersion = 4;
                auto unown = createPokemon(trainer, unownSpecies, 25);
                if (unown)
                {
                    unown->setOriginGame(fireRedVersion);
                    setPersonality(*unown, 0, 0);
                    const Legality::EncounterInfo tanobyEncounter{Legality::EncounterType::Wild, Enums::GameVersion::FRLG};
                    const Legality::Encounters::EncounterReference tanobySlot = findAreaSlotOfSpecies(Legality::Encounters::getAreas3FR(), unownSpecies);
                    auto wrongLetter = unown->clone();
                    wrongLetter->setForm(1);
                    wrongLetter->refreshChecksum();
                    expectVerifierFlags(context, Legality::verifyForm, *unown, *wrongLetter, tanobyEncounter, tanobySlot, {Legality::Severity::Invalid, Legality::CheckIdentifier::Form, Legality::ResultCode::FormInvalidExpect});
                }
            }
            constexpr uint16_t meowsticSpecies = 678;
            if (formatGeneration >= 6 && Pokemon::getPersonalRecord(context.group, meowsticSpecies, 0).formCount >= 2)
            {
                // Meowstic's form is its gender.
                auto meowstic = createPokemon(trainer, meowsticSpecies, 30);
                if (meowstic)
                {
                    meowstic->setOriginGame(Enums::getGroupRepVersion(context.group));
                    meowstic->setGender(0);
                    meowstic->setForm(0);
                    meowstic->refreshChecksum();
                    auto femaleForm = meowstic->clone();
                    femaleForm->setForm(1);
                    femaleForm->refreshChecksum();
                    expectVerifierFlags(context, Legality::verifyForm, *meowstic, *femaleForm, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Form, Legality::ResultCode::GenderInvalidNone});
                }
            }
            // Runerigus counts the damage it took as a Galarian Yamask: at least 49.
            const bool isSwordShield = context.group == Enums::GameVersion::SWSH;
            if (!isSwordShield && context.group != Enums::GameVersion::SV)
            {
                return;
            }
            constexpr uint16_t yamaskSpecies = 562;
            constexpr uint16_t runerigusSpecies = 867;
            const size_t formArgumentOffset = isSwordShield ? 0xE4 : 0xD0;
            auto runerigus = createPokemon(trainer, runerigusSpecies, 40);
            if (!runerigus || Pokemon::getPersonalRecord(context.group, runerigusSpecies, 0).formCount == 0)
            {
                return;
            }
            runerigus->setOriginGame(Enums::getGroupRepVersion(context.group));
            writeRecordUInt32(*runerigus, formArgumentOffset, 49);
            // A hatched Galarian Yamask.
            constexpr uint8_t galarianForm = 1;
            const Legality::EncounterInfo yamaskEncounter{Legality::EncounterType::Egg, context.group};
            const Legality::Encounters::EncounterReference yamaskEgg = makeEggTemplate(context.group, yamaskSpecies, galarianForm);
            auto uncounted = runerigus->clone();
            writeRecordUInt32(*uncounted, formArgumentOffset, 0);
            expectVerifierFlags(context, Legality::verifyForm, *runerigus, *uncounted, yamaskEncounter, yamaskEgg, {Legality::Severity::Invalid, Legality::CheckIdentifier::Form, Legality::ResultCode::FormArgumentAtLeast});
        }

        void plantedAbilityBallFormDefects(Context &context, const Trainer::Trainer &trainer)
        {
            const int formatGeneration = getGroupGeneration(context.group);
            plantedAbilityDefects(context, trainer, formatGeneration);
            plantedBallDefects(context, trainer, formatGeneration);
            plantedFormDefects(context, trainer, formatGeneration);
        }

        /// Runs `verifier` as if `pokemon` sat in the save of its own original trainer (PKHeX
        /// ParseSettings.InitFromSaveFileData's ActiveTrainer).
        Legality::Report runVerifierWithOwnTrainer(VerifierFunction verifier, const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const Legality::EncounterInfo &encounter)
        {
            Legality::ActiveTrainer activeTrainer;
            activeTrainer.trainerName = pokemon.otName();
            activeTrainer.TID16 = pokemon.tid16();
            activeTrainer.SID16 = pokemon.sid16();
            activeTrainer.trainerGender = pokemon.otGender();
            activeTrainer.gameVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
            Legality::AnalysisOptions analysisOptions;
            analysisOptions.activeTrainer = &activeTrainer;
            Legality::Report legalityReport;
            const Legality::AnalysisContext analysisContext{pokemon, group, getGroupGeneration(group), analysisOptions, legalityReport, encounter};
            verifier(analysisContext);
            return legalityReport;
        }

        /// Planted defects for the history and memory verifiers: the handler state, handler friendship
        /// and affection, and the memories, on an untraded Pokemon of the format's own game.
        void plantedHistoryMemoryDefects(Context &context, const Trainer::Trainer &trainer)
        {
            const Enums::GameVersion group = context.group;
            const int formatGeneration = getGroupGeneration(group);
            if (formatGeneration < 6)
            {
                return;
            }
            auto base = createOwnPokemon(trainer, group);
            if (!base || !base->hasHandler())
            {
                return;
            }
            const Legality::EncounterInfo ownWildEncounter{Legality::EncounterType::Wild, group};

            // The open save's own trainer must be the current handler of a Pokemon it caught.
            auto handled = base->clone();
            handled->setCurrentHandler(1);
            handled->refreshChecksum();
            const std::string handlerLabel = "planted defect PKHeX Memory/TransferCurrentHandlerInvalid (Invalid) on a blank " + Enums::getGameVersionName(group) + " entity";
            context.check(!reportsCode(runVerifierWithOwnTrainer(Legality::verifyHistory, *base, group, ownWildEncounter), Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::TransferCurrentHandlerInvalid), "legality verifier flags a planted defect on the control Pokemon that does not have it", handlerLabel);
            context.check(reportsCode(runVerifierWithOwnTrainer(Legality::verifyHistory, *handled, group, ownWildEncounter), Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::TransferCurrentHandlerInvalid), "legality verifier did not flag a planted defect", handlerLabel);

            // An untraded Pokemon has no handler to be current, befriend or be affectionate with.
            expectVerifierFlags(context, Legality::verifyHistory, *base, *handled, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryHandlerFlagInvalid});
            auto befriended = base->clone();
            befriended->setHTFriendship(70);
            befriended->refreshChecksum();
            expectVerifierFlags(context, Legality::verifyHistory, *base, *befriended, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryStatFriendshipHandler0});
            if (base->hasHandlerAffection())
            {
                auto affectionate = base->clone();
                affectionate->setHTAffection(10);
                affectionate->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyHistory, *base, *affectionate, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryStatAffectionHandler0});
            }
            // A Pokemon met in an earlier format's game must have been handled on the way here.
            if (formatGeneration >= 7)
            {
                const Legality::EncounterInfo earlierFormatEncounter{Legality::EncounterType::Wild, Enums::GameVersion::XY};
                expectVerifierFlags(context, Legality::verifyHistory, *handled, *base, earlierFormatEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::TransferHandlerFlagRequired});
            }

            if (!base->hasHandlerMemories())
            {
                return;
            }
            auto remembered = base->clone();
            remembered->setHTMemory(5);
            remembered->setHTMemoryIntensity(3);
            remembered->refreshChecksum();
            expectVerifierFlags(context, Legality::verifyMemories, *base, *remembered, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryCleared});
            if (formatGeneration <= 7)
            {
                // A 3DS original trainer memory can never be the link trade one.
                constexpr size_t originalTrainerMemoryOffset = 0xCD;
                auto linkTraded = base->clone();
                writeRecordByte(*linkTraded, originalTrainerMemoryOffset, 4);
                linkTraded->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyMemories, *base, *linkTraded, ownWildEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryArgumentBadOTEgg});
            }
            if (group == Enums::GameVersion::SWSH)
            {
                // Sword/Shield's off-by-one memory roll can leave a new handler with no memory.
                auto traded = base->clone();
                traded->setHTName(u"Planted");
                traded->refreshChecksum();
                expectVerifierFlags(context, Legality::verifyMemories, *base, *traded, ownWildEncounter, {Legality::Severity::Warning, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryMissingHandler});
            }
        }

        /// A field PKHeX's MiscVerifier family checks, at its decrypted-record offset (PKHeX's own layout), and the value that breaks it.
        struct MiscDefect
        {
            size_t recordOffset;
            uint8_t defectValue;
            Legality::CheckIdentifier identifier;
            Legality::ResultCode code;
        };

        /// One planted MiscVerifier defect per format whose branch neither test corpus reaches.
        MiscDefect getMiscDefect(Enums::GameVersion group)
        {
            switch (group)
            {
            case Enums::GameVersion::DP:
            case Enums::GameVersion::PT:
            case Enums::GameVersion::HGSS:
                return {0x41, 0x40, Legality::CheckIdentifier::Misc, Legality::ResultCode::Gen4ShinyLeafBitsInvalid};
            case Enums::GameVersion::BW:
            case Enums::GameVersion::B2W2:
                return {0x87, 1, Legality::CheckIdentifier::Misc, Legality::ResultCode::Gen5PokeStarImpossibleValue};
            case Enums::GameVersion::SM:
            case Enums::GameVersion::USUM:
                return {0x2A, 20, Legality::CheckIdentifier::Misc, Legality::ResultCode::TransferBad};
            case Enums::GameVersion::SWSH:
                return {0x90, 11, Legality::CheckIdentifier::Misc, Legality::ResultCode::StatDynamaxInvalid};
            case Enums::GameVersion::BDSP:
                return {0x52, 1, Legality::CheckIdentifier::Misc, Legality::ResultCode::TransferFlagIllegal};
            case Enums::GameVersion::PLA:
                return {0x16, 0x40, Legality::CheckIdentifier::RelearnMove, Legality::ResultCode::StatNobleInvalid};
            case Enums::GameVersion::SV:
                return {0xCF, 44, Legality::CheckIdentifier::Misc, Legality::ResultCode::StatBattleVersionInvalid};
            case Enums::GameVersion::ZA:
                return {0x4B, 1, Legality::CheckIdentifier::RelearnMove, Legality::ResultCode::LevelBoostNotZero};
            default:
                return {0, 0, Legality::CheckIdentifier::Misc, Legality::ResultCode::TransferBad};
            }
        }

        void plantedMiscDefects(Context &context, const Trainer::Trainer &trainer)
        {
            auto base = createOwnPokemon(trainer, context.group);
            if (!base)
            {
                return;
            }
            const Legality::EncounterInfo uncheckedEncounter;
            const MiscDefect miscDefect = getMiscDefect(context.group);
            if (miscDefect.recordOffset != 0)
            {
                auto defective = base->clone();
                writeRecordByte(*defective, miscDefect.recordOffset, miscDefect.defectValue);
                expectVerifierFlags(context, Legality::verifyMisc, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, miscDefect.identifier, miscDefect.code});
            }
            if (context.group == Enums::GameVersion::GG)
            {
                // The stored Combat Power PKSE writes must equal PKHeX's PB7.CalcCP, which the verifier recomputes.
                constexpr size_t combatPowerOffset = 0xFE;
                auto defective = base->clone();
                writeRecordByte(*defective, combatPowerOffset, static_cast<uint8_t>(static_cast<uint8_t>(defective->getData()[combatPowerOffset]) + 1));
                expectVerifierFlags(context, Legality::verifyMisc, *base, *defective, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Encounter, Legality::ResultCode::StatIncorrectCP});
            }
        }

        // ---- Planted defects for PKHeX's era rules (ParseSettings.InitFromSaveFileData) and the Gen 3
        // Misc and Egg checks: no test save is a Nintendo Switch FireRed/LeafGreen one with a foreign
        // Pokemon in it, and no save holds an egg with contest stats or a Japanese OT with bad trash.

        Legality::Report runVerifierWithTrainer(VerifierFunction verifier, const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const Legality::EncounterInfo &encounter, const Legality::ActiveTrainer *activeTrainer)
        {
            Legality::Report legalityReport;
            Legality::AnalysisOptions analysisOptions;
            analysisOptions.activeTrainer = activeTrainer;
            const Legality::AnalysisContext analysisContext{pokemon, group, getGroupGeneration(group), analysisOptions, legalityReport, encounter};
            verifier(analysisContext);
            return legalityReport;
        }

        /// As expectVerifierFlags, analyzed with `activeTrainer` as the open save.
        void expectVerifierFlagsWithTrainer(Context &context, VerifierFunction verifier, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const Legality::EncounterInfo &encounter, const Legality::ActiveTrainer *activeTrainer, const VerifierExpectation &expectation)
        {
            const std::string defectLabel = std::string("planted defect PKHeX ") + Legality::getCheckIdentifierSourceName(expectation.identifier) + "/" + Legality::getResultCodeSourceName(expectation.code) + " on a blank " + Enums::getGameVersionName(context.group) + " entity, " + (activeTrainer == nullptr ? "no save open" : activeTrainer->isVirtualConsole ? "Virtual Console save open" : "cartridge save open");
            context.check(!reportsCode(runVerifierWithTrainer(verifier, control, context.group, encounter, activeTrainer), expectation.severity, expectation.identifier, expectation.code), "legality verifier flags a planted defect on the control Pokemon that does not have it", defectLabel);
            context.check(reportsCode(runVerifierWithTrainer(verifier, defective, context.group, encounter, activeTrainer), expectation.severity, expectation.identifier, expectation.code), "legality verifier did not flag a planted defect", defectLabel);
        }

        /// The defect is legal without the Virtual Console save open: PKHeX's cartridge era allows it.
        void expectCartridgeEraAllows(Context &context, VerifierFunction verifier, const Pokemon::Pokemon &defective, const Legality::EncounterInfo &encounter, const VerifierExpectation &expectation)
        {
            const std::string defectLabel = std::string("PKHeX ") + Legality::getResultCodeSourceName(expectation.code) + " on a blank " + Enums::getGameVersionName(context.group) + " entity with no save open";
            context.check(!reportsCode(runVerifierWithTrainer(verifier, defective, context.group, encounter, nullptr), expectation.severity, expectation.identifier, expectation.code), "legality verifier applies a Virtual Console rule with no Virtual Console save open", defectLabel);
        }

        void plantedEraDefects(Context &context, const Trainer::Trainer &trainer)
        {
            if (!Enums::isGen3Group(context.group))
            {
                return;
            }
            constexpr uint8_t emeraldVersion = 3;
            constexpr uint16_t safariBallItem = 5;
            constexpr uint8_t pokerusStrainOne = 0x10;
            constexpr size_t contestCoolOffset = 0x3E;
            constexpr size_t trainerNameOffset = 0x14;
            const Legality::EncounterInfo uncheckedEncounter;
            Legality::ActiveTrainer switchTrainer;
            switchTrainer.gameVersion = Enums::GameVersion::FR;
            switchTrainer.isVirtualConsole = true;

            auto base = createOwnPokemon(trainer, context.group);
            if (!base)
            {
                return;
            }
            base->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::FR));
            base->refreshChecksum();

            // MiscVerifierG3: only FireRed/LeafGreen reach the Switch release, and not every item does.
            auto foreignOrigin = base->clone();
            foreignOrigin->setOriginGame(emeraldVersion);
            foreignOrigin->refreshChecksum();
            const VerifierExpectation tradeNotAvailable{Legality::Severity::Invalid, Legality::CheckIdentifier::Misc, Legality::ResultCode::TradeNotAvailable};
            expectVerifierFlagsWithTrainer(context, Legality::verifyMisc, *base, *foreignOrigin, uncheckedEncounter, &switchTrainer, tradeNotAvailable);
            expectCartridgeEraAllows(context, Legality::verifyMisc, *foreignOrigin, uncheckedEncounter, tradeNotAvailable);

            auto unreleasedItem = base->clone();
            unreleasedItem->setHeldItem(safariBallItem);
            unreleasedItem->refreshChecksum();
            const VerifierExpectation itemUnreleased{Legality::Severity::Invalid, Legality::CheckIdentifier::Misc, Legality::ResultCode::ItemUnreleased};
            expectVerifierFlagsWithTrainer(context, Legality::verifyMisc, *base, *unreleasedItem, uncheckedEncounter, &switchTrainer, itemUnreleased);
            expectCartridgeEraAllows(context, Legality::verifyMisc, *unreleasedItem, uncheckedEncounter, itemUnreleased);

            // MiscPokerusVerifier: the Switch release has no Pokerus.
            auto infected = base->clone();
            infected->setPokerus(pokerusStrainOne | 1);
            infected->refreshChecksum();
            const VerifierExpectation pokerusUnobtainable{Legality::Severity::Invalid, Legality::CheckIdentifier::Misc, Legality::ResultCode::PokerusStrainUnobtainable};
            expectVerifierFlagsWithTrainer(context, Legality::verifyMisc, *base, *infected, uncheckedEncounter, &switchTrainer, pokerusUnobtainable);
            expectCartridgeEraAllows(context, Legality::verifyMisc, *infected, uncheckedEncounter, pokerusUnobtainable);

            // MiscVerifierG3.VerifyTrashJPN: a Japanese OT is terminated, padded with terminators to six
            // bytes, then zero. The Switch save keeps the trash from being explained as a Colosseum/XD reset.
            Legality::EncounterInfo wildEncounter;
            wildEncounter.type = Legality::EncounterType::Wild;
            wildEncounter.originGroup = Enums::GameVersion::FRLG;
            auto japaneseTrainer = base->clone();
            japaneseTrainer->setLanguage(static_cast<uint8_t>(Enums::LanguageID::Japanese));
            const uint8_t cleanJapaneseTrainerName[7] = {0x01, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
            for (size_t byteIndex = 0; byteIndex < sizeof(cleanJapaneseTrainerName); ++byteIndex)
            {
                writeRecordByte(*japaneseTrainer, trainerNameOffset + byteIndex, cleanJapaneseTrainerName[byteIndex]);
            }
            japaneseTrainer->refreshChecksum();
            auto unpaddedJapaneseTrainer = japaneseTrainer->clone();
            writeRecordByte(*unpaddedJapaneseTrainer, trainerNameOffset + 3, 0x00);
            unpaddedJapaneseTrainer->refreshChecksum();
            expectVerifierFlagsWithTrainer(context, Legality::verifyMisc, *japaneseTrainer, *unpaddedJapaneseTrainer, wildEncounter, &switchTrainer, {Legality::Severity::Invalid, Legality::CheckIdentifier::Trainer, Legality::ResultCode::TrashBytesMissingTerminatorFinal});

            // EggVerifier.VerifyWhileEgg: an egg has no contest stats.
            auto egg = base->clone();
            egg->setEgg(true);
            egg->setLanguage(static_cast<uint8_t>(Enums::LanguageID::Japanese));
            egg->refreshChecksum();
            auto contestEgg = egg->clone();
            writeRecordByte(*contestEgg, contestCoolOffset, 10);
            contestEgg->refreshChecksum();
            expectVerifierFlags(context, Legality::verifyMisc, *egg, *contestEgg, uncheckedEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Egg, Legality::ResultCode::EggContest});
        }
    }

    namespace
    {
        /// One PKHeX BreedTests row: the game, the egg species and form, and up to four moves.
        struct BreedVector
        {
            Enums::GameVersion version;
            uint8_t generation;
            uint16_t speciesId;
            uint8_t formId;
            std::array<uint16_t, 4> moves;
        };

        std::string describeBreedVector(const BreedVector &vector)
        {
            return Enums::getGameVersionName(vector.version) + " species " + std::to_string(vector.speciesId) + " moves " + std::to_string(vector.moves[0]) + "," + std::to_string(vector.moves[1]) + "," + std::to_string(vector.moves[2]) + "," + std::to_string(vector.moves[3]);
        }

        /// PKHeX Tests/Legality/BreedTests.cs (VerifyBreed, CheckBad, CheckFix), with the move and
        /// species ids PKHeX's enums give.
        void breedVectors(Context &context)
        {
            using Enums::GameVersion;
            // VerifyBreed: every order here is one breeding produces.
            const BreedVector validVectors[] = {
                {GameVersion::GD, 2, 1, 0, {33, 45, 0, 0}},          // Bulbasaur: Tackle, Growl
                {GameVersion::SI, 2, 174, 0, {185, 1, 174, 192}},    // Igglybuff: Feint Attack, Pound, Curse, Zap Cannon
                {GameVersion::C, 2, 174, 0, {185, 1, 53, 47}},       // Igglybuff: Feint Attack, Pound, Flamethrower, Sing
                {GameVersion::B, 5, 214, 0, {224, 400, 370, 444}},   // Heracross: Megahorn, Night Slash, Close Combat, Stone Edge
                {GameVersion::B, 5, 214, 0, {117, 224, 68, 179}},    // Heracross: Bide, Megahorn, Counter, Reversal
                {GameVersion::B, 5, 214, 0, {30, 203, 224, 36}},     // Heracross: Horn Attack, Endure, Megahorn, Take Down
                {GameVersion::B, 5, 214, 0, {203, 224, 264, 364}},   // Heracross: Endure, Megahorn, Focus Punch, Feint
                {GameVersion::B, 5, 214, 0, {224, 179, 523, 374}},   // Heracross: Megahorn, Reversal, Bulldoze, Fling
                {GameVersion::X, 6, 58, 0, {44, 46, 394, 234}},      // Growlithe: Bite, Roar, Flare Blitz, Morning Sun
                {GameVersion::OR, 6, 58, 0, {234, 231, 242, 257}},   // Growlithe: Morning Sun, Iron Tail, Crunch, Heat Wave
                {GameVersion::OR, 6, 147, 0, {35, 43, 349, 245}},    // Dratini: Wrap, Leer, Dragon Dance, Extreme Speed
                {GameVersion::OR, 6, 479, 0, {310, 86, 84, 109}},    // Rotom: Astonish, Thunder Wave, Thunder Shock, Confuse Ray
                {GameVersion::BD, 8, 443, 0, {442, 34, 328, 200}},   // Gible: Iron Head, Body Slam, Sand Tomb, Outrage
                {GameVersion::BD, 8, 443, 0, {442, 34, 200, 328}},   // Gible: Iron Head, Body Slam, Outrage, Sand Tomb
                {GameVersion::BD, 8, 443, 0, {34, 200, 328, 225}},   // Gible: Body Slam, Outrage, Sand Tomb, Dragon Breath
            };
            for (const BreedVector &vector : validVectors)
            {
                std::array<uint8_t, 4> origins{};
                const bool isValid = Legality::LearnSource::validateBreedMoves(vector.generation, vector.speciesId, vector.formId, vector.version, vector.moves, origins);
                context.check(isValid, "MoveBreed.Validate refuses a moveset PKHeX's BreedTests accept", describeBreedVector(vector));
                bool isOrdered = true;
                for (int slotIndex = 1; slotIndex < 4; ++slotIndex)
                {
                    // Gen 2 lists the base moves first; later generations sort by source.
                    const bool isOutOfOrder = vector.generation == 2 ? (origins[slotIndex] == 1 && origins[slotIndex - 1] != 1) : origins[slotIndex] < origins[slotIndex - 1];
                    isOrdered = isOrdered && !isOutOfOrder;
                }
                context.check(isOrdered, "MoveBreed.Validate's sources are out of order for a valid moveset", describeBreedVector(vector));
            }

            // CheckBad: no breeding produces these.
            const BreedVector invalidVectors[] = {
                {GameVersion::C, 2, 174, 0, {204, 111, 47, 53}},     // Igglybuff: invalid push-out order
                {GameVersion::SH, 8, 679, 0, {210, 469, 194, 0}},    // Honedge: too few moves
                {GameVersion::OR, 6, 479, 0, {435, 268, 271, 109}},  // Rotom: invalid push-out order
                {GameVersion::OR, 6, 479, 0, {86, 84, 109, 435}},    // Rotom: no inherited level-up moves
            };
            for (const BreedVector &vector : invalidVectors)
            {
                std::array<uint8_t, 4> origins{};
                const bool isValid = Legality::LearnSource::validateBreedMoves(vector.generation, vector.speciesId, vector.formId, vector.version, vector.moves, origins);
                context.check(!isValid, "MoveBreed.Validate accepts a moveset PKHeX's BreedTests refuse", describeBreedVector(vector));
            }

            // CheckFix: invalid as given, valid once reordered, with the same number of moves.
            const BreedVector fixableVectors[] = {
                {GameVersion::GD, 2, 1, 0, {45, 33, 0, 0}},          // Bulbasaur: base moves swapped
                {GameVersion::UM, 7, 4, 0, {52, 187, 10, 45}},       // Charmander: inherited and egg moves swapped
                {GameVersion::BD, 8, 443, 0, {34, 328, 200, 225}},   // Gible
            };
            for (const BreedVector &vector : fixableVectors)
            {
                std::array<uint8_t, 4> origins{};
                const bool isValid = Legality::LearnSource::validateBreedMoves(vector.generation, vector.speciesId, vector.formId, vector.version, vector.moves, origins);
                context.check(!isValid, "MoveBreed.Validate accepts a misordered moveset PKHeX's BreedTests refuse", describeBreedVector(vector));
                std::array<uint16_t, 4> expected{};
                const bool isFixed = Legality::LearnSource::getExpectedBreedMoves(vector.generation, vector.speciesId, vector.formId, vector.version, vector.moves, expected);
                context.check(isFixed && expected != vector.moves, "MoveBreed.GetExpectedMoves does not reorder a fixable moveset", describeBreedVector(vector));
                const auto countEmpty = [](const std::array<uint16_t, 4> &moves) { return std::count(moves.begin(), moves.end(), static_cast<uint16_t>(0)); };
                context.check(countEmpty(expected) == countEmpty(vector.moves), "MoveBreed.GetExpectedMoves changes the number of moves", describeBreedVector(vector));
            }
        }

        /// PKHeX LearnVerifier.Finalize, LearnVerifierRelearn and MoveInfo.IsSketchValid on fixed inputs.
        struct ChainBreedVector
        {
            Enums::GameVersion version;
            uint16_t speciesId;
            bool isExpectedValid;
            /// PKHeX ChainBreedEraFlags.NintendoSwitchGBA (ChainBreedUtil.InferFlagsFrom(FRX)).
            bool isSwitchGameBoyAdvanceEra;
            std::array<uint16_t, 4> moves;
        };

        /// PKHeX Tests/PKHeX.Core.Tests/Legality/ChainBreedLegalityTests.cs, every case. PKHeX's
        /// aggregate versions are named by a game of the group (GS as GD, HGSS as HG, BW as B, R as RU).
        void chainBreedVectors(Context &context)
        {
            using Enums::GameVersion;
            const ChainBreedVector vectors[] = {
                // DetectsInvalidChains
                {GameVersion::B, 183, false, false, {187, 453, 0, 0}},     // Marill: Belly Drum, Aqua Jet
                {GameVersion::B2, 298, false, false, {187, 453, 0, 0}},    // Azurill: Belly Drum, Aqua Jet
                {GameVersion::FR, 7, false, false, {114, 175, 0, 0}},      // Squirtle: Haze, Flail
                {GameVersion::B2, 113, false, false, {121, 0, 0, 0}},      // Chansey: Egg Bomb
                {GameVersion::GD, 43, false, false, {175, 75, 14, 235}},   // Oddish: Flail, Razor Leaf, Swords Dance, Synthesis
                {GameVersion::GD, 238, false, false, {142, 0, 0, 0}},     // Smoochum: Lovely Kiss (no parents)
                // DetectsValidChains
                {GameVersion::HG, 218, true, false, {108, 257, 0, 0}},     // Slugma: Smokescreen, Heat Wave (HG/SS tutor)
                {GameVersion::HG, 147, true, false, {245, 349, 0, 0}},     // Dratini: Extreme Speed (gift), Dragon Dance
                {GameVersion::GD, 46, true, false, {68, 175, 113, 0}},     // Paras: Counter, Flail, Light Screen
                {GameVersion::HG, 56, true, false, {227, 96, 265, 0}},     // Mankey: Encore, Meditate, Smelling Salts
                {GameVersion::GD, 113, true, false, {38, 0, 0, 0}},        // Chansey: Double-Edge via Jigglypuff
                {GameVersion::Pt, 90, true, false, {229, 333, 0, 0}},      // Shellder: Rapid Spin, Icicle Spear
                {GameVersion::RU, 313, true, false, {270, 0, 0, 0}},       // Volbeat: Helping Hand, bred with Illumise
                {GameVersion::GD, 227, true, false, {65, 18, 0, 0}},       // Skarmory: Drill Peck, Whirlwind (Gen 1 TM04)
                {GameVersion::C, 104, true, false, {157, 14, 0, 0}},       // Cubone: Rock Slide, Swords Dance (via Yellow)
                // DetectValidChainPastFather
                {GameVersion::B2, 90, true, false, {36, 0, 0, 0}},         // Shellder: Take Down (XD encounter move)
                {GameVersion::B2, 90, true, false, {419, 0, 0, 0}},        // Shellder: Avalanche (Gen 4 TM)
                {GameVersion::B2, 90, true, false, {419, 36, 0, 0}},       // Shellder: Avalanche, Take Down (Gen 3 => 4 => 5)
                {GameVersion::B, 446, true, false, {174, 120, 68, 0}},     // Munchlax: Curse, Self-Destruct, Counter
                {GameVersion::HG, 341, true, false, {232, 349, 0, 0}},     // Corphish: Metal Claw, Dragon Dance
                // DetectValidChainCyclic
                {GameVersion::GD, 236, true, false, {136, 183, 229, 0}},   // Tyrogue: High Jump Kick, Mach Punch, Rapid Spin
                // DetectsValidChainSmeargle
                {GameVersion::HG, 56, true, false, {227, 96, 265, 0}},     // Mankey via Smeargle
                {GameVersion::EM, 172, true, false, {179, 227, 273, 217}}, // Pichu: Reversal, Encore, Wish, Present
                // DetectsInvalidInheritedLevelUpMove
                {GameVersion::BD, 238, false, false, {181, 0, 0, 0}},      // Smoochum: Powder Snow (no Human-Like father)
                {GameVersion::BD, 238, true, false, {93, 0, 0, 0}},        // Smoochum: Confusion via Alakazam
                // DetectsInvalidVirtualConsole3
                {GameVersion::FR, 143, false, true, {174, 90, 0, 0}},      // Snorlax: Curse, Fissure (XD out of reach)
                {GameVersion::FR, 143, false, true, {90, 0, 0, 0}},        // Snorlax: Fissure (XD out of reach)
                // DetectInvalidSpeciesMaleSplit
                {GameVersion::US, 313, false, false, {146, 679, 0, 0}},    // Volbeat: Dizzy Punch, Lunge
                {GameVersion::B2, 313, true, false, {146, 69, 0, 0}},      // Volbeat: Dizzy Punch, Seismic Toss
                {GameVersion::B2, 313, true, false, {146, 405, 0, 0}},     // Volbeat: Dizzy Punch, Bug Buzz
                {GameVersion::B2, 313, true, false, {146, 271, 0, 0}},     // Volbeat: Dizzy Punch, Trick (B2/W2 tutor)
                {GameVersion::HG, 313, false, false, {146, 271, 0, 0}},    // Volbeat: Dizzy Punch, Trick (no HG/SS tutor)
                {GameVersion::X, 32, true, false, {133, 457, 0, 0}},       // Nidoran-M: Amnesia, Head Smash (Smeargle)
                {GameVersion::Y, 32, true, false, {251, 457, 0, 0}},       // Nidoran-M: Beat Up, Head Smash
                {GameVersion::B2, 32, true, false, {251, 457, 0, 0}},      // Nidoran-M: Beat Up, Head Smash
            };
            for (const ChainBreedVector &vector : vectors)
            {
                const bool isValid = Legality::LearnSource::isChainBreedValid(vector.speciesId, 0, vector.version, vector.isSwitchGameBoyAdvanceEra, vector.moves);
                const std::string detail = Enums::getGameVersionName(vector.version) + " species " + std::to_string(vector.speciesId) + " moves " + std::to_string(vector.moves[0]) + "," + std::to_string(vector.moves[1]) + "," + std::to_string(vector.moves[2]) + "," + std::to_string(vector.moves[3]);
                context.check(isValid == vector.isExpectedValid, vector.isExpectedValid ? "ChainBreedLegality refuses a chain PKHeX's ChainBreedLegalityTests accept" : "ChainBreedLegality accepts a chain PKHeX's ChainBreedLegalityTests refuse", detail);
            }
        }

        void learnVerifierVectors(Context &context)
        {
            using Enums::GameVersion;
            using Legality::LearnSource::LearnMethod;
            using Legality::LearnSource::MoveResult;

            // Finalize: the earlier slot of a duplicate pair, every empty slot before a move, and an
            // empty first slot; Legends: Z-A allows the gaps.
            std::array<MoveResult, 4> results{};
            const std::array<uint16_t, 4> duplicated = {33, 45, 33, 0};
            Legality::LearnSource::finalizeMoveSlots(results, duplicated, GameVersion::SV);
            context.check(results[0].info.method == LearnMethod::Duplicate && results[2].info.method != LearnMethod::Duplicate, "LearnVerifier.Finalize does not flag the earlier slot of a duplicate move");
            results = {};
            const std::array<uint16_t, 4> gapped = {0, 33, 0, 45};
            Legality::LearnSource::finalizeMoveSlots(results, gapped, GameVersion::SV);
            context.check(results[0].info.method == LearnMethod::EmptyInvalid && results[2].info.method == LearnMethod::EmptyInvalid, "LearnVerifier.Finalize does not flag the empty slots before a move");
            results = {};
            Legality::LearnSource::finalizeMoveSlots(results, gapped, GameVersion::ZA);
            context.check(results[0].info.method == LearnMethod::Empty && results[2].info.method == LearnMethod::Empty, "LearnVerifier keeps flagging gaps between moves in Legends: Z-A");

            // LearnVerifierRelearn: before Gen 6 there are no relearn moves to have.
            Legality::LearnSource::LearnEncounter generation5Encounter;
            generation5Encounter.generation = 5;
            Legality::LearnSource::LearnEntity entity;
            entity.relearnMoves = {33, 0, 0, 0};
            Legality::LearnSource::verifyRelearnMoves(results, generation5Encounter, entity);
            context.check(results[0].info.method == LearnMethod::UnobtainableExpect && results[0].expectedMove == 0 && results[1].info.method == LearnMethod::Empty, "LearnVerifierRelearn accepts a relearn move on a Gen 5 encounter");
            // A template's fixed relearn moves must be matched exactly.
            Legality::LearnSource::LearnEncounter fixedRelearnEncounter;
            fixedRelearnEncounter.generation = 7;
            fixedRelearnEncounter.relearnMoves = {33, 45, 0, 0};
            entity.relearnMoves = {33, 0, 0, 0};
            Legality::LearnSource::verifyRelearnMoves(results, fixedRelearnEncounter, entity);
            context.check(results[0].info.method == LearnMethod::Relearn && results[1].info.method == LearnMethod::UnobtainableExpect && results[1].expectedMove == 45, "LearnVerifierRelearn does not expect a template's fixed relearn move");

            // MoveInfo.IsSketchValid.
            context.check(Legality::LearnSource::isSketchValid(33, GameVersion::RSE), "Sketch refuses Tackle in Gen 3");
            context.check(!Legality::LearnSource::isSketchValid(165, GameVersion::SV), "Sketch accepts Struggle");
            context.check(!Legality::LearnSource::isSketchValid(448, GameVersion::DP), "Sketch accepts Chatter");
            context.check(!Legality::LearnSource::isSketchValid(400, GameVersion::GSC), "Sketch accepts a move Gen 2 does not have");
            context.check(!Legality::LearnSource::isSketchValid(120, GameVersion::GSC), "Sketch accepts Self-Destruct in Gen 2");
            context.check(!Legality::LearnSource::isSketchValid(617, GameVersion::SM), "Sketch accepts Light of Ruin in Gen 7");
            context.check(!Legality::LearnSource::isSketchValid(622, GameVersion::SM), "Sketch accepts a Z-Move");
            context.check(!Legality::LearnSource::isSketchValid(863, GameVersion::SV), "Sketch accepts Revival Blessing in Scarlet/Violet");
        }

        /// The move verdicts through the analyzer, on the Gen 3 targets the encounter engine handles:
        /// a move Pikachu learns nowhere is reported for its slot, and a learnable moveset is not.
        /// Gen 4 encounter-engine branches neither parity corpus reaches: the honey-tree overlap rule,
        /// the hatch-location table, the ground-tile permits, the random-form slot marker and the
        /// Ranger Manaphy egg. Each expectation is worked from PKHeX's own code.
        void encounter4Vectors(Context &context, Trainer::Trainer &trainer)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            // HoneyTreeUtil: ID32 1935328924 is bytes {115, 90, 198, 156} -> trees {10, 6, 9, 9}, and the
            // overlap pass moves the last to 10 (PKHeX's own comment gives {10, 6, 9, 10}). Trees 10 and
            // 9 are on Route 208 (27) and tree 6 on Route 207 (25); Route 205 (20) has none.
            const Encounters::EncounterArea4 routeTwoZeroEight(GameVersion::D, Legality::RNG::SlotType4::HoneyTree, 0, 27, Encounters::GROUND_TILE_GRASS, nullptr, 0);
            const Encounters::EncounterArea4 routeTwoZeroFive(GameVersion::D, Legality::RNG::SlotType4::HoneyTree, 0, 20, Encounters::GROUND_TILE_GRASS, nullptr, 0);
            context.check(routeTwoZeroEight.isMunchlaxTree(1935328924u), "honey tree: ID32 1935328924 has a Munchlax tree on Route 208");
            context.check(!routeTwoZeroFive.isMunchlaxTree(1935328924u), "honey tree: ID32 1935328924 has no Munchlax tree on Route 205");
            // ID32 0: every byte is tree 0, the overlap pass moves them to 1, 2, 3 -> 20, 20, 21, 22.
            context.check(routeTwoZeroFive.isMunchlaxTree(0), "honey tree: ID32 0 has a Munchlax tree on Route 205");
            context.check(!routeTwoZeroEight.isMunchlaxTree(0), "honey tree: ID32 0 has no Munchlax tree on Route 208");

            // EggHatchLocation4: Twinleaf Town (1) is Sinnoh, New Bark Town (126) Johto, location 63
            // exists only in Platinum, and location 86 hatches nothing.
            const auto version = [](GameVersion game) { return static_cast<uint8_t>(game); };
            context.check(Encounters::isValidMet4(1, version(GameVersion::D)), "hatch location: Twinleaf Town is valid in Diamond");
            context.check(!Encounters::isValidMet4(1, version(GameVersion::HG)), "hatch location: Twinleaf Town is invalid in HeartGold");
            context.check(Encounters::isValidMet4(126, version(GameVersion::SS)), "hatch location: New Bark Town is valid in SoulSilver");
            context.check(!Encounters::isValidMet4(126, version(GameVersion::P)), "hatch location: New Bark Town is invalid in Pearl");
            context.check(Encounters::isValidMet4(63, version(GameVersion::Pt)) && !Encounters::isValidMet4(63, version(GameVersion::D)), "hatch location: location 63 is Platinum-only");
            context.check(!Encounters::isValidMet4Any(86) && !Encounters::isValidMet4Any(235), "hatch location: locations 86 and 235 hatch nothing");

            // GroundTileAllowed: a grass permit takes the grass tile and refuses water.
            context.check(Encounters::isGroundTileAllowed(Encounters::GROUND_TILE_GRASS, Encounters::GROUND_TILE_TYPE_GRASS), "ground tile: grass permit takes the grass tile");
            context.check(!Encounters::isGroundTileAllowed(Encounters::GROUND_TILE_GRASS, Encounters::GROUND_TILE_TYPE_WATER), "ground tile: grass permit refuses the water tile");

            // EncounterUtil.FormRandom (31) marks HeartGold's Ruins of Alph Unown slots as any form;
            // reading it as a literal form id left every Unown there unmatched.
            bool foundUnownSlot = false;
            for (const Encounters::EncounterArea4 &area : Encounters::getAreas4HG())
            {
                for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                {
                    const Encounters::EncounterSlot4 &slot = area.slotAt(slotIndex);
                    if (slot.speciesId() != 201)
                    {
                        continue;
                    }
                    foundUnownSlot = true;
                    context.check(slot.isRandomUnspecificForm(), "HeartGold Unown slot is a random-form slot", "location=" + std::to_string(area.location()) + " form=" + std::to_string(slot.formId()));
                }
            }
            context.check(foundUnownSlot, "HeartGold has Unown wild slots");

            // PGT Ranger Manaphy: an unhatched egg in Gen 4 with egg location 3001 and met location 0.
            auto manaphy = trainer.createBlankPokemon();
            if (!manaphy || Encounters::getFormatGeneration(*manaphy) != 4)
            {
                return;
            }
            manaphy->setSpecies(490);
            manaphy->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            manaphy->setEgg(true);
            manaphy->setEggLocation(Encounters::LOCATION_RANGER4);
            manaphy->setFatefulEncounter(true);
            manaphy->refreshChecksum();
            context.check(Encounters::MysteryGiftPGT::isRangerManaphy(*manaphy), "Ranger Manaphy: an unhatched Ranger egg is the PGT gift");
            auto korean = manaphy->clone();
            if (context.check(korean != nullptr, "clone() returned nothing for this format"))
            {
                korean->setLanguage(static_cast<uint8_t>(Enums::LanguageID::Korean));
                context.check(!Encounters::MysteryGiftPGT::isRangerManaphy(*korean), "Ranger Manaphy: a Korean egg is never the PGT gift");
            }
            auto hatched = manaphy->clone();
            if (context.check(hatched != nullptr, "clone() returned nothing for this format"))
            {
                hatched->setEgg(false);
                hatched->setEggLocation(1);
                context.check(!Encounters::MysteryGiftPGT::isRangerManaphy(*hatched), "Ranger Manaphy: a hatched Manaphy with a Sinnoh egg location is not the PGT gift");
            }
        }

        /// The template PKSE's encounter engine matches for a Pokemon, as PKHeX's LongName; empty when none.
        std::string getMatchedEncounterName(const Pokemon::Pokemon &pokemon)
        {
            Legality::Encounters::LegalityInfo info(pokemon, Legality::AnalysisOptions{});
            Legality::Encounters::EncounterFinder::findVerifiedEncounter(pokemon, info);
            return info.encounterMatch() ? info.encounterMatch()->longName() : std::string();
        }

        /// True when the encounter engine's parse for a Pokemon holds `code` with the given validity.
        bool hasParseResult(const Pokemon::Pokemon &pokemon, Legality::ResultCode code, bool isValid)
        {
            Legality::Encounters::LegalityInfo info(pokemon, Legality::AnalysisOptions{});
            Legality::Encounters::EncounterFinder::findVerifiedEncounter(pokemon, info);
            for (const Legality::Encounters::CheckResult &result : info.parse)
            {
                if (result.code == code && result.isValid() == isValid)
                {
                    return true;
                }
            }
            return false;
        }

        /// Gen 5 encounter-engine branches neither parity corpus reaches: the hatch-location table, the
        /// Global Link language locks, the Dream Radar levels, the Entree Forest move split, hidden
        /// grottoes, N's Pokemon and the unhatched-egg locations. Each expectation is worked from
        /// PKHeX's own code and data.
        void encounter5Vectors(Context &context, Trainer::Trainer &trainer)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;
            const auto version = [](GameVersion game) { return static_cast<uint8_t>(game); };

            // EggHatchLocation5: Black Gate (112) is Black/Black 2 only, White Gate (113) White/White 2
            // only, Aspertia City (117) Black 2/White 2 only, location 138 and anything past the table nowhere.
            context.check(Encounters::isValidMet5(4, version(GameVersion::W)) && Encounters::isValidMet5(4, version(GameVersion::B2)), "Gen 5 hatch location: location 4 is valid everywhere");
            context.check(Encounters::isValidMet5(112, version(GameVersion::B)) && !Encounters::isValidMet5(112, version(GameVersion::W)), "Gen 5 hatch location: Black Gate is Black-only");
            context.check(Encounters::isValidMet5(113, version(GameVersion::W2)) && !Encounters::isValidMet5(113, version(GameVersion::B2)), "Gen 5 hatch location: White Gate is White-only");
            context.check(Encounters::isValidMet5(117, version(GameVersion::B2)) && !Encounters::isValidMet5(117, version(GameVersion::B)), "Gen 5 hatch location: Aspertia City is Black 2/White 2 only");
            context.check(!Encounters::isValidMet5(138, version(GameVersion::W2)) && !Encounters::isValidMet5(200, version(GameVersion::W2)) && !Encounters::isValidMet5(4, version(GameVersion::HG)), "Gen 5 hatch location: location 138, a location past the table and a Gen 4 game are invalid");

            // GlobalLinkPromotion language locks.
            constexpr uint8_t languageJapanese = static_cast<uint8_t>(Enums::LanguageID::Japanese);
            constexpr uint8_t languageEnglish = static_cast<uint8_t>(Enums::LanguageID::English);
            constexpr uint8_t languageKorean = static_cast<uint8_t>(Enums::LanguageID::Korean);
            context.check(Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Croagunk2, languageEnglish) && !Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Croagunk2, languageJapanese), "Global Link: the Poison Jab Croagunk was international only");
            context.check(Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Rayquaza, languageKorean) && !Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Rayquaza, languageEnglish), "Global Link: Rayquaza was Korean only");
            context.check(Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Arceus, languageJapanese) && !Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::Arceus, languageKorean), "Global Link: Arceus was never Korean");
            context.check(Encounters::canPromotionBeReceivedBy(Encounters::GlobalLinkPromotion::NotPromotion, languageKorean), "Global Link: a Dream World Pokemon has no language lock");

            // EncounterStatic5Radar.IsMatchLevel: 5 to 40 in steps of 5.
            context.check(Encounters::EncounterStatic5Radar::isMatchLevel(5) && Encounters::EncounterStatic5Radar::isMatchLevel(40), "Dream Radar: levels 5 and 40 are valid");
            context.check(!Encounters::EncounterStatic5Radar::isMatchLevel(0) && !Encounters::EncounterStatic5Radar::isMatchLevel(7) && !Encounters::EncounterStatic5Radar::isMatchLevel(45), "Dream Radar: levels 0, 7 and 45 are invalid");

            // DreamWorldEntry.GetArray splits the Pleasant Forest Nidoran-F (moves 10, 389, 162) into
            // three templates, and Nidoran-F has a hidden ability in Black 2/White 2.
            const std::span<const Encounters::EncounterStatic5Entree> dreamWorld = Encounters::getDreamWorld5BW();
            if (context.check(dreamWorld.size() >= 3, "the Black/White Dream World table is filled"))
            {
                context.check(dreamWorld[0].speciesId() == 29 && dreamWorld[1].speciesId() == 29 && dreamWorld[2].speciesId() == 29 && dreamWorld[3].speciesId() != 29, "Dream World: Nidoran-F is split into one template per special move");
                context.check(dreamWorld[0].moves().moveIds[0] == 10 && dreamWorld[1].moves().moveIds[0] == 389 && dreamWorld[2].moves().moveIds[0] == 162, "Dream World: the split templates carry moves 10, 389 and 162");
                context.check(dreamWorld[0].ability() == Legality::RNG::AbilityPermission::OnlyHidden, "Dream World: Nidoran-F comes with its hidden ability");
                context.check(dreamWorld[0].name() == "Entree Forest Encounter (BW)", "Dream World: Black/White template name", dreamWorld[0].name());
            }
            const std::span<const Encounters::EncounterStatic5Entree> dreamWorldCommon = Encounters::getDreamWorld5Common();
            context.check(!dreamWorldCommon.empty() && dreamWorldCommon[0].name() == "Entree Forest Encounter (Gen5)", "Dream World: the shared table is named for every Gen 5 game");

            // Hidden grottoes: every grotto slot has its hidden ability and is never shiny.
            bool foundGrotto = false;
            for (const Encounters::EncounterArea5 &area : Encounters::getAreas5B2())
            {
                if (area.type() != Encounters::SlotType5::HiddenGrotto || area.slotCount() == 0)
                {
                    continue;
                }
                const Encounters::EncounterSlot5 &slot = area.slotAt(0);
                foundGrotto = true;
                context.check(slot.ability() == Legality::RNG::AbilityPermission::OnlyHidden && slot.shiny() == Legality::RNG::Shiny::Never, "hidden grotto slot: hidden ability, never shiny");
                context.check(slot.longName() == "Wild Encounter (B2) HiddenGrotto", "hidden grotto slot name", slot.longName());
                break;
            }
            context.check(foundGrotto, "Black 2 has hidden grotto slots");
            context.check(Encounters::getStatic5N().size() == 15, "Black 2/White 2 release fifteen of N's Pokemon");

            auto pokemon = trainer.createBlankPokemon();
            if (!pokemon || Encounters::getFormatGeneration(*pokemon) != 5)
            {
                return;
            }

            // N's Klink at Chargestone Cave: PID 0xFF000000, level 28, all IVs 30, trainer N (ID32 2).
            auto klink = pokemon->clone();
            if (context.check(klink != nullptr, "clone() returned nothing for this format"))
            {
                klink->setSpecies(599);
                klink->setOriginGame(version(GameVersion::B2));
                klink->setLanguage(languageEnglish);
                klink->setPID(0xFF000000);
                klink->setMetLocation(37);
                klink->setMetLevel(28);
                klink->setLevel(28);
                klink->setEggLocation(0);
                klink->setGender(2);
                klink->setOTGender(0);
                klink->setId32(Encounters::EncounterStatic5N::ID32);
                klink->setOTName(u"N");
                for (int statIndex = 0; statIndex < 6; statIndex++)
                {
                    klink->setIV(statIndex, Encounters::EncounterStatic5N::INDIVIDUAL_VALUE);
                }
                klink->setBall(static_cast<uint8_t>(Enums::Ball::Poke));
                // Its level 28 moves (Gear Grind, Bind, Charge Beam, Thunder Shock), so the finder keeps the match.
                klink->setMove(0, 544);
                klink->setMove(1, 20);
                klink->setMove(2, 451);
                klink->setMove(3, 84);
                klink->refreshChecksum();
                const std::string matched = getMatchedEncounterName(*klink);
                context.check(matched == "N's Pok\xC3\xA9mon", "N's Klink matches N's Pokemon", matched);
                klink->setIV(0, 31);
                klink->refreshChecksum();
                context.check(getMatchedEncounterName(*klink) != "N's Pok\xC3\xA9mon", "a Klink without N's IVs is not N's Pokemon");
            }

            // An Entree Forest Nidoran-F from Black at level 10 with Pound (move 1 slot holds 10).
            auto nidoran = pokemon->clone();
            if (context.check(nidoran != nullptr, "clone() returned nothing for this format"))
            {
                nidoran->setSpecies(29);
                nidoran->setOriginGame(version(GameVersion::B));
                nidoran->setLanguage(languageEnglish);
                nidoran->setMetLocation(Encounters::LOCATION_ENTREE_FOREST5);
                nidoran->setMetLevel(10);
                nidoran->setLevel(10);
                nidoran->setEggLocation(0);
                nidoran->setGender(1);
                nidoran->setBall(static_cast<uint8_t>(Enums::Ball::Dream));
                nidoran->setMove(0, 10);
                nidoran->refreshChecksum();
                const std::string matched = getMatchedEncounterName(*nidoran);
                context.check(matched == "Entree Forest Encounter (BW)", "a Dream World Nidoran-F matches the Black/White Entree Forest table", matched);
            }

            // VerifyUnhatchedEgg5: an egg met at level 1 at location 0 is a plain unhatched egg; one with a
            // Link Trade egg location was never traded as an egg.
            auto egg = pokemon->clone();
            if (context.check(egg != nullptr, "clone() returned nothing for this format"))
            {
                egg->setSpecies(172);
                egg->setOriginGame(version(GameVersion::W));
                egg->setLanguage(languageEnglish);
                egg->setEgg(true);
                egg->setEggLocation(Encounters::LOCATION_DAYCARE5);
                egg->setMetLocation(0);
                egg->setMetLevel(1);
                egg->setLevel(1);
                egg->refreshChecksum();
                context.check(getMatchedEncounterName(*egg) == "Egg", "a White Day Care egg matches an egg", getMatchedEncounterName(*egg));
                context.check(hasParseResult(*egg, Legality::ResultCode::EggUnhatched, true), "an unhatched Day Care egg met at location 0 is a plain unhatched egg");
                egg->setMetLocation(5);
                egg->refreshChecksum();
                context.check(hasParseResult(*egg, Legality::ResultCode::EggLocationNone, false), "an unhatched egg with a met location is EggLocationNone");
                egg->setMetLocation(Encounters::LOCATION_LINK_TRADE5);
                egg->setEggLocation(Encounters::LOCATION_LINK_TRADE5);
                egg->refreshChecksum();
                context.check(hasParseResult(*egg, Legality::ResultCode::EggLocationTradeFail, false), "an unhatched egg with a Link Trade egg location is EggLocationTradeFail");
            }

            // VerifyEncounterEgg5: a Black egg hatched at White Gate (113) hatched somewhere Black cannot reach.
            auto hatched = pokemon->clone();
            if (context.check(hatched != nullptr, "clone() returned nothing for this format"))
            {
                hatched->setSpecies(172);
                hatched->setOriginGame(version(GameVersion::B));
                hatched->setLanguage(languageEnglish);
                hatched->setEgg(false);
                hatched->setEggLocation(Encounters::LOCATION_DAYCARE5);
                hatched->setMetLocation(113);
                hatched->setMetLevel(1);
                hatched->setLevel(1);
                hatched->setBall(static_cast<uint8_t>(Enums::Ball::Poke));
                hatched->refreshChecksum();
                context.check(hasParseResult(*hatched, Legality::ResultCode::EggLocationInvalid, false), "a Black egg hatched at White Gate is EggLocationInvalid");
                hatched->setMetLocation(112);
                hatched->refreshChecksum();
                context.check(hasParseResult(*hatched, Legality::ResultCode::EggLocation, true), "a Black egg hatched at Black Gate is valid");
            }
        }

        /// Sword/Shield encounter-engine branches the parity corpus does not reach: the hatch-location
        /// table's edges, the generated location tables, the Wonder Card language and version rules, and
        /// egg creation from an evolution chain. Each expectation is worked from PKHeX's own code.
        void encounter8Vectors(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            // EggHatchLocation8: Route 5 (40) hatches, location 2 (index 1) does not, an odd location
            // never does, and anything past the 124-entry table is refused rather than read out of bounds.
            context.check(Encounters::isValidMet8SWSH(40), "Sword/Shield hatch location: Route 5 is valid");
            context.check(!Encounters::isValidMet8SWSH(2), "Sword/Shield hatch location: location 2 is invalid");
            context.check(!Encounters::isValidMet8SWSH(41), "Sword/Shield hatch location: an odd location is invalid");
            context.check(!Encounters::isValidMet8SWSH(248), "Sword/Shield hatch location: a location past the table is invalid");

            // EncounterArea8.GetAreasCanWanderTo: Rolling Fields (122) reaches 124, 128 and 130.
            const std::span<const uint8_t> wander = Encounters::getAreasCanWanderTo8(122);
            context.check(wander.size() == 3 && wander[0] == 124 && wander[1] == 128 && wander[2] == 130, "Rolling Fields symbol encounters wander to 124, 128 and 130");
            context.check(Encounters::getAreasCanWanderTo8(2).empty(), "a location with no wander list reaches nowhere");
            // Encounters8Nest: den 0 is in 144, 134 and 122; den 6 is water-only at East Lake Axewell
            // (128); Lake of Outrage (154) is a water-only distribution location, Rolling Fields is not.
            const std::span<const uint8_t> nestZero = Encounters::getNestLocations8(0);
            context.check(nestZero.size() == 3 && nestZero[0] == 144 && nestZero[2] == 122, "den 0 is in Stony Wilderness, South Lake Miloch and Rolling Fields");
            context.check(Encounters::isInaccessibleRank12Nest8(6, 128) && !Encounters::isInaccessibleRank12Nest8(0, 128), "den 6 is water-only at East Lake Axewell, den 0 is not");
            context.check(Encounters::isInaccessibleRank12DistributionLocation8(154) && !Encounters::isInaccessibleRank12DistributionLocation8(122), "Lake of Outrage is a water-only distribution location, Rolling Fields is not");

            // AreaWeather8's flags ToString: the named combinations win, printed smallest-first.
            context.check(Encounters::getWeatherName8(Encounters::WEATHER8_STORMY) == "Stormy", "AreaWeather8 Raining|Thunderstorm prints as Stormy", Encounters::getWeatherName8(Encounters::WEATHER8_STORMY));

            // WC8: card 9000 (the first HOME gift) is in the database, is a HOME gift, and Sword and
            // Shield can both receive it. A card language-locked to nothing accepts every language
            // from 1 to 10 and refuses 0 and 11.
            const Encounters::MysteryGiftWC8 *homeGift = nullptr;
            for (const Encounters::MysteryGiftWC8 &card : Encounters::getGifts8WC8())
            {
                if (card.card().cardId == 9000)
                {
                    homeGift = &card;
                    break;
                }
            }
            if (context.check(homeGift != nullptr, "the WC8 database holds card 9000"))
            {
                context.check(homeGift->isHomeGift(), "card 9000 is a HOME gift");
                context.check(homeGift->canBeReceivedByVersion(GameVersion::SW) && homeGift->canBeReceivedByVersion(GameVersion::SH), "card 9000 can be received by Sword and Shield");
            }
            Encounters::MysteryGiftWC8::Properties anyLanguage;
            anyLanguage.restrictVersion = 1;
            const Encounters::MysteryGiftWC8 anyLanguageCard(anyLanguage);
            context.check(anyLanguageCard.canHaveLanguage(1) && anyLanguageCard.canHaveLanguage(10), "a WC8 with no language lock accepts Japanese and Traditional Chinese");
            context.check(!anyLanguageCard.canHaveLanguage(0) && !anyLanguageCard.canHaveLanguage(11), "a WC8 refuses languages 0 and 11");
            context.check(anyLanguageCard.canBeReceivedByVersion(GameVersion::SW) && !anyLanguageCard.canBeReceivedByVersion(GameVersion::SH), "a Sword-only WC8 cannot be received in Shield");
            // Slot 6 is skipped: language 7 (Spanish) reads slot 5, so a card nicknamed only in slot 5
            // is nicknamed for Spanish and not for German (5, slot 4).
            Encounters::MysteryGiftWC8::Properties spanishNickname;
            spanishNickname.nicknamedSlots = 1u << 5;
            const Encounters::MysteryGiftWC8 spanishNicknameCard(spanishNickname);
            context.check(spanishNicknameCard.isNicknamed(7) && !spanishNicknameCard.isNicknamed(5), "WC8 language slots skip language 6");

            // EncounterGenerator8.TryGetEgg: a Pikachu whose chain reaches a level 1 Pichu hatched from a
            // Pichu egg, with no split-breed alternative; a chain whose lowest stage cannot be level 1
            // hatched from nothing.
            std::array<Legality::Evolutions::EvolutionCriteria, 2> pichuChain{};
            pichuChain[0].speciesId = 25;
            pichuChain[0].levelMin = 1;
            pichuChain[0].levelMax = 100;
            pichuChain[1].speciesId = 172;
            pichuChain[1].levelMin = 1;
            pichuChain[1].levelMax = 100;
            Encounters::EncounterEgg8 egg(0, 0, GameVersion::SW);
            if (context.check(Encounters::EncounterGenerator8::tryGetEgg(pichuChain, GameVersion::SW, egg), "a Pikachu reaching a level 1 Pichu hatched from an egg"))
            {
                context.check(egg.speciesId() == 172 && egg.canHaveVoltTackle(), "the Pikachu line's egg is a Pichu that can know Volt Tackle", "species=" + std::to_string(egg.speciesId()));
                Encounters::EncounterEgg8 split(0, 0, GameVersion::SW);
                context.check(!Encounters::EncounterGenerator8::tryGetSplit(egg, pichuChain, split), "a Pichu egg has no split-breed alternative");
            }
            std::array<Legality::Evolutions::EvolutionCriteria, 1> notLevelOne{};
            notLevelOne[0].speciesId = 172;
            notLevelOne[0].levelMin = 5;
            notLevelOne[0].levelMax = 100;
            context.check(!Encounters::EncounterGenerator8::tryGetEgg(notLevelOne, GameVersion::SW, egg), "a chain whose lowest stage starts at level 5 did not hatch");
            // CreateEggEncounter: Rotom's appliance forms hatch as plain Rotom.
            std::array<Legality::Evolutions::EvolutionCriteria, 1> washRotom{};
            washRotom[0].speciesId = 479;
            washRotom[0].formId = 2;
            washRotom[0].levelMin = 1;
            washRotom[0].levelMax = 100;
            if (Encounters::EncounterGenerator8::tryGetEgg(washRotom, GameVersion::SH, egg))
            {
                context.check(egg.formId() == 0, "a Wash Rotom's egg is the plain form", "form=" + std::to_string(egg.formId()));
            }
        }

        /// A zeroed decrypted record of `group`, built through the entity factory.
        std::unique_ptr<Pokemon::Pokemon> makeBlankRecord9(Context &context, Enums::GameVersion group)
        {
            const std::vector<std::byte> decryptedRecord(Trainer::Bank::recordSizeFor(group), std::byte{0});
            std::byte *encryptedRecord = Trainer::Bank::encryptRecord(group, decryptedRecord, 0);
            if (!context.check(encryptedRecord != nullptr, "no entity class for a Gen 9 test group", Enums::getGameVersionName(group)))
            {
                return nullptr;
            }
            std::unique_ptr<Pokemon::Pokemon> pokemon = Trainer::Bank::makePokemon(group, std::span<const std::byte>(encryptedRecord, decryptedRecord.size()));
            delete[] encryptedRecord;
            return pokemon;
        }

        void encounter9Vectors(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            // EggHatchLocation9: South Province (Area One) (6) hatches in both games; Naranja Academy
            // (130) only in Scarlet, Uva Academy (131) only in Violet; the Terarium entry (200) is the
            // last entry and anything past it is refused.
            context.check(Encounters::isValidMet9SL(6) && Encounters::isValidMet9VL(6), "Scarlet/Violet hatch location: South Province (Area One) is valid in both");
            context.check(Encounters::isValidMet9SL(130) && !Encounters::isValidMet9VL(130), "Scarlet/Violet hatch location: Naranja Academy is Scarlet-only");
            context.check(!Encounters::isValidMet9SL(131) && Encounters::isValidMet9VL(131), "Scarlet/Violet hatch location: Uva Academy is Violet-only");
            context.check(Encounters::isValidMet9VL(200) && !Encounters::isValidMet9VL(201) && !Encounters::isValidMet9SL(0), "Scarlet/Violet hatch location: the table ends at the Terarium entry");

            // LocationsHOME: a Scarlet/Violet origin read in Sword/Shield is always remapped, in its own
            // game never, and a Gen 8 origin read in Scarlet/Violet either way.
            context.check(Encounters::getRemapState(GameVersion::SV, GameVersion::SWSH) == Encounters::LocationRemapState::Remapped, "a Scarlet/Violet origin in Sword/Shield is remapped");
            context.check(Encounters::getRemapState(GameVersion::SV, GameVersion::SV) == Encounters::LocationRemapState::Original, "a Scarlet/Violet origin in Scarlet/Violet keeps its location");
            context.check(Encounters::getRemapState(GameVersion::PLA, GameVersion::SV) == Encounters::LocationRemapState::Either, "a Legends: Arceus origin in Scarlet/Violet may be either");
            context.check(Encounters::getRemapState(GameVersion::SV, GameVersion::BDSP) == Encounters::LocationRemapState::Either, "a Scarlet/Violet origin moved down to Brilliant Diamond/Shining Pearl may be either");
            context.check(Encounters::getMetSwordShield(6, static_cast<uint8_t>(GameVersion::SL)) == Encounters::LOCATION_HOME_SWSL && Encounters::getMetSwordShield(6, static_cast<uint8_t>(GameVersion::VL)) == Encounters::LOCATION_HOME_SHVL, "HOME writes Scarlet as SWSL and Violet as SHVL");
            context.check(Encounters::isValidMetScarletViolet(Encounters::LOCATION_HOME_SWSL, static_cast<uint8_t>(GameVersion::SW)) && !Encounters::isValidMetScarletViolet(Encounters::LOCATION_HOME_SWSL, static_cast<uint8_t>(GameVersion::SH)), "SWSL pairs with Sword only");

            // AreaWeather9: a Dry mark never matches a Gen 9 slot; Sandstorm needs the sandstorm flag.
            context.check(!Encounters::isMarkCompatible9(0xFF, Encounters::WeatherMark::Dry), "a Dry mark is never compatible with Gen 9 weather");
            context.check(Encounters::isMarkCompatible9(Encounters::WEATHER9_SANDSTORM, Encounters::WeatherMark::Sandstorm) && !Encounters::isMarkCompatible9(Encounters::WEATHER9_RAINING, Encounters::WeatherMark::Sandstorm), "a Sandstorm mark needs a sandstorm slot");
            // SizeType9: XS is below 0x10, XL from 0xF0; VALUE has no range.
            context.check(Encounters::isSizeWithinRange(Legality::RNG::SizeType9::XS, 0x0F) && !Encounters::isSizeWithinRange(Legality::RNG::SizeType9::XS, 0x10), "SizeType9 XS ends at 0x0F");
            context.check(Encounters::isSizeWithinRange(Legality::RNG::SizeType9::XL, 0xF0) && !Encounters::isSizeWithinRange(Legality::RNG::SizeType9::VALUE, 0x80), "SizeType9 XL starts at 0xF0 and VALUE is no range");

            // Generated data: the tables are filled, a Tera raid prints PKHeX's name, and a Wonder
            // Card's internal species is converted to the National Dex (card 1007 is Baxcalibur, 998).
            context.check(!Encounters::getAreas9SV().empty() && !Encounters::getStatic9SV().empty() && !Encounters::getTrade9SV().empty() && !Encounters::getOutbreak9().empty() && !Encounters::getMight9().empty() && !Encounters::getDist9().empty() && !Encounters::getFixed9().empty(), "a Scarlet/Violet encounter table is empty");
            if (context.check(!Encounters::getTera9Base().empty(), "the base-game Tera raid table is empty"))
            {
                const std::string teraName = Encounters::getTera9Base().front().name();
                context.check(teraName.rfind("Tera Raid Encounter [Base] ", 0) == 0, "a base-game Tera raid is named as PKHeX names it", teraName);
            }
            bool isBaxcaliburCardFound = false;
            for (const Encounters::MysteryGiftWC9 &card : Encounters::getGifts9WC9())
            {
                if (card.card().cardId == 1007)
                {
                    isBaxcaliburCardFound = card.card().speciesId == 998;
                    break;
                }
            }
            context.check(isBaxcaliburCardFound, "WC9 card 1007 is Baxcalibur after the internal species conversion");

            // EncounterGenerator9.TryGetEgg: no split-breed, Scatterbug always hatches Fancy, and a
            // chain starting above level 1 hatched from nothing.
            std::array<Legality::Evolutions::EvolutionCriteria, 1> scatterbugChain{};
            scatterbugChain[0].speciesId = 664;
            scatterbugChain[0].formId = 0;
            scatterbugChain[0].levelMin = 1;
            scatterbugChain[0].levelMax = 100;
            Encounters::EncounterEgg9 egg(0, 0, GameVersion::SL);
            if (context.check(Encounters::EncounterGenerator9::tryGetEgg(scatterbugChain, GameVersion::SL, egg), "a Scatterbug hatched from a Scarlet/Violet egg"))
            {
                context.check(egg.formId() == 18, "a Scarlet/Violet Scatterbug egg is the Fancy pattern", "form=" + std::to_string(egg.formId()));
            }
            std::array<Legality::Evolutions::EvolutionCriteria, 2> pikachuChain{};
            pikachuChain[0].speciesId = 25;
            pikachuChain[0].levelMin = 1;
            pikachuChain[0].levelMax = 100;
            pikachuChain[1].speciesId = 172;
            pikachuChain[1].levelMin = 1;
            pikachuChain[1].levelMax = 100;
            if (context.check(Encounters::EncounterGenerator9::tryGetEgg(pikachuChain, GameVersion::VL, egg), "a Pikachu reaching a level 1 Pichu hatched from a Scarlet/Violet egg"))
            {
                context.check(egg.speciesId() == 172 && egg.canHaveVoltTackle(), "the Scarlet/Violet Pikachu-line egg is a Pichu that can know Volt Tackle");
            }
            std::array<Legality::Evolutions::EvolutionCriteria, 1> notLevelOne{};
            notLevelOne[0].speciesId = 172;
            notLevelOne[0].levelMin = 5;
            notLevelOne[0].levelMax = 100;
            context.check(!Encounters::EncounterGenerator9::tryGetEgg(notLevelOne, GameVersion::VL, egg), "a chain starting at level 5 did not hatch from a Scarlet/Violet egg");

            // EncounterEnumerator9SWSH: a Scarlet Lechonk HOME moved to Sword (met at SWSL, met level 5)
            // matches a Paldea slot even though its met location names no Paldea area; the same record
            // as a Scarlet/Violet entity is not a Sword/Shield record, so the SWSH route yields nothing.
            Legality::Evolutions::EvolutionChain lechonkChain;
            lechonkChain.stageCount = 1;
            lechonkChain.stages[0].speciesId = 915;
            lechonkChain.stages[0].levelMin = 5;
            lechonkChain.stages[0].levelMax = 5;
            if (std::unique_ptr<Pokemon::Pokemon> swordRecord = makeBlankRecord9(context, GameVersion::SWSH))
            {
                swordRecord->setSpecies(915);
                swordRecord->setOriginGame(static_cast<uint8_t>(GameVersion::SL));
                swordRecord->setMetLocation(Encounters::LOCATION_HOME_SWSL);
                swordRecord->setMetLevel(5);
                swordRecord->setLevel(5);
                bool isSlotFound = false;
                for (const Encounters::MatchedEncounter &match : Encounters::EncounterEnumerator9::enumerateSWSH(*swordRecord, lechonkChain, GameVersion::SL))
                {
                    if (match.encounter && match.encounter->templateType() == Encounters::EncounterTemplateType::Slot9)
                    {
                        isSlotFound = true;
                        break;
                    }
                }
                context.check(isSlotFound, "a Scarlet Lechonk moved to Sword by HOME matches a Paldea wild slot");
            }
            if (std::unique_ptr<Pokemon::Pokemon> violetRecord = makeBlankRecord9(context, GameVersion::SV))
            {
                violetRecord->setSpecies(915);
                violetRecord->setOriginGame(static_cast<uint8_t>(GameVersion::SL));
                violetRecord->setMetLevel(5);
                bool isAnyYielded = false;
                for (const Encounters::EncounterReference &encounter : Encounters::EncounterGenerator9::getEncountersSWSH(*violetRecord, lechonkChain, GameVersion::SL))
                {
                    (void)encounter;
                    isAnyYielded = true;
                }
                context.check(!isAnyYielded, "the Sword/Shield route yields nothing for a Scarlet/Violet entity");
            }
        }

        void encounter9aVectors(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;
            namespace RNG = Legality::RNG;

            // Generated data: every table is filled, the Hyperspace areas are typed as such, and an
            // Alpha slot carries its three flawless IVs and PKHeX's long name.
            context.check(!Encounters::getAreas9aStandard().empty() && !Encounters::getAreas9aHyperspace().empty() && !Encounters::getStatic9a().empty() && !Encounters::getGifts9a().empty() && !Encounters::getTrade9a().empty() && !Encounters::getGifts9aWA9().empty(), "a Legends: Z-A encounter table is empty");
            bool isAlphaSlotFound = false;
            for (const Encounters::EncounterArea9a &area : Encounters::getAreas9aStandard())
            {
                for (uint16_t slotIndex = 0; slotIndex < area.slotCount() && !isAlphaSlotFound; slotIndex++)
                {
                    const Encounters::EncounterSlot9a &slot = area.slotAt(slotIndex);
                    if (slot.isAlpha())
                    {
                        isAlphaSlotFound = true;
                        context.check(slot.flawlessIVCount() == 3, "a Legends: Z-A Alpha slot has three flawless IVs", "count=" + std::to_string(slot.flawlessIVCount()));
                        context.check(slot.longName() == "Wild Encounter Standard", "a Legends: Z-A standard slot is named as PKHeX names it", slot.longName());
                    }
                }
            }
            context.check(isAlphaSlotFound, "the Legends: Z-A wild table has an Alpha slot");
            if (!Encounters::getAreas9aHyperspace().empty())
            {
                const Encounters::EncounterArea9a &hyperspace = Encounters::getAreas9aHyperspace().front();
                context.check(hyperspace.type() == Encounters::SlotType9a::Hyperspace && hyperspace.slotCount() > 0 && hyperspace.slotAt(0).longName() == "Wild Encounter Hyperspace", "a Hyperspace area is typed and named as PKHeX has it");
            }
            context.check(Encounters::getTrade9a().front().name() == "Trade Encounter" && Encounters::getGifts9a().front().name() == "Gift Encounter", "the Legends: Z-A trades and gifts are named as PKHeX names them");

            // WA9: card 101 (Audino, PokeCenter) lists Japanese in every slot but English and Traditional
            // Chinese, so French cannot redeem it; the OT name for language 6 reads the English slot;
            // the stored ID32 carries the card id in its millions (IsOldIDFormat).
            const Encounters::MysteryGiftWA9 *audinoCard = nullptr;
            const Encounters::MysteryGiftWA9 *worldsCard = nullptr;
            for (const Encounters::MysteryGiftWA9 &card : Encounters::getGifts9aWA9())
            {
                if (card.card().cardId == 101)
                {
                    audinoCard = &card;
                }
                else if (card.card().cardId == 603)
                {
                    worldsCard = &card;
                }
            }
            if (context.check(audinoCard != nullptr, "WA9 card 101 is in the generated table"))
            {
                context.check(audinoCard->card().speciesId == 531, "WA9 card 101 is Audino", "species=" + std::to_string(audinoCard->card().speciesId));
                context.check(audinoCard->canHaveLanguage(2) && !audinoCard->canHaveLanguage(3) && !audinoCard->canHaveLanguage(12), "WA9 card 101 redeems in English but not French or an invalid language");
                context.check(audinoCard->getTrainerName(2) == u"Pok\u00E9Center" && audinoCard->getTrainerName(6) == u"Pok\u00E9Center", "a WA9 OT name for language 6 reads the English slot");
                context.check(audinoCard->ID32Old() == 251101u, "WA9 ID32Old strips the card id", "ID32=" + std::to_string(audinoCard->ID32Old()));
                context.check(!audinoCard->isHomeGift() && audinoCard->flawlessIVCount() == 0 && audinoCard->getParameters().correlation == RNG::LumioseCorrelation::SkipTrainer, "a WA9 event card is seeded with the SkipTrainer correlation");
            }
            if (context.check(worldsCard != nullptr, "WA9 card 603 is in the generated table"))
            {
                // Latin American Spanish (11) is the tenth name slot. PKHeX CanHaveLanguage reads only nine
                // language words, then requires the redeeming slot's word to be 0, so it refuses 11 here.
                context.check(worldsCard->getTrainerName(11) == u"Worlds26" && worldsCard->getTrainerName(8) == u"WCS26", "WA9 card 603 maps Korean and Latin American Spanish to their own slots");
                context.check(worldsCard->canHaveLanguage(10) && !worldsCard->canHaveLanguage(11), "WA9 CanHaveLanguage reads nine language words, as PKHeX does");
                const RNG::GenerateParam9a parameters = worldsCard->getParameters();
                context.check(parameters.individualValues.isSpecified && parameters.sizeType == RNG::SizeType9::VALUE && parameters.scale == 128 && parameters.ability == RNG::AbilityPermission::OnlyHidden, "WA9 card 603 seeds its fixed IVs, scale and hidden ability");
            }

            // LumioseSolver: a never-shiny genderless Pokemon generated from a seed (EC, fake trainer id,
            // PID, six IVs, ability) gives that seed back; one PID bit off, and no seed is found.
            constexpr uint64_t generatedSeed = 0x0123456789ABCDEFull;
            RNG::Xoroshiro128Plus random(generatedSeed);
            const uint32_t encryptionConstant = static_cast<uint32_t>(random.nextInt(UINT32_MAX));
            const uint32_t fakeTrainerID = static_cast<uint32_t>(random.nextInt());
            uint32_t PID = static_cast<uint32_t>(random.nextInt());
            constexpr uint32_t recordTrainerID = 0x00010002u;
            if (RNG::ShinyUtil::isShiny6(fakeTrainerID, PID))
            {
                PID = RNG::LumioseRNG::antiShiny(PID);
            }
            if (RNG::ShinyUtil::isShiny6(recordTrainerID, PID))
            {
                PID = RNG::LumioseRNG::antiShiny(PID);
            }
            int generatedValues[6] = {};
            for (int &generatedValue : generatedValues)
            {
                generatedValue = static_cast<int>(random.nextInt(32));
            }
            const uint8_t abilityNumber = static_cast<uint8_t>(1 << random.nextInt(2));
            RNG::GenerateParam9a parameters;
            parameters.genderRatio = 255;
            parameters.correlation = RNG::LumioseCorrelation::Normal;
            parameters.sizeType = RNG::SizeType9::VALUE;
            parameters.scale = 0;
            parameters.nature = 3;
            parameters.ability = RNG::AbilityPermission::Any12;
            parameters.shiny = RNG::Shiny::Never;
            if (std::unique_ptr<Pokemon::Pokemon> record = makeBlankRecord9(context, GameVersion::ZA))
            {
                record->setId32(recordTrainerID);
                record->setEncryptionConstant(encryptionConstant);
                record->setPID(PID);
                // The generator rolls HP, Atk, Def, SpA, SpD, Spe; the record stores Spe before SpA.
                const int storageIndex[6] = {0, 1, 2, 4, 5, 3};
                for (int statIndex = 0; statIndex < 6; statIndex++)
                {
                    record->setIV(storageIndex[statIndex], static_cast<uint8_t>(generatedValues[statIndex]));
                }
                record->setAbilityNumber(abilityNumber);
                record->setGender(2);
                record->setNature(3);
                const bool isStoredAsGenerated = record->ivSPA() == generatedValues[3] && record->ivSPE() == generatedValues[5] && record->gender() == 2 && record->pid() == PID;
                if (context.check(isStoredAsGenerated, "the Legends: Z-A test record stores the generated values"))
                {
                    uint64_t foundSeed = 0;
                    context.check(RNG::LumioseSolver::tryGetSeed(parameters, *record, foundSeed) && foundSeed == generatedSeed, "LumioseSolver recovers the seed a Legends: Z-A Pokemon was generated from");
                    context.check(Encounters::isSeedFound9a(parameters, *record), "isSeedFound9a accepts a Pokemon generated from a seed");
                    record->setPID(PID ^ 1u);
                    context.check(!RNG::LumioseSolver::tryGetSeed(parameters, *record, foundSeed), "LumioseSolver finds no seed once the PID is changed");
                    // A shiny record with a random shiny state is left unsearched, as PKHeX leaves it by default.
                    const uint32_t lowPID = PID & 0xFFFFu;
                    record->setPID((((recordTrainerID >> 16) ^ (recordTrainerID & 0xFFFFu) ^ lowPID) << 16) | lowPID);
                    parameters.shiny = RNG::Shiny::Random;
                    context.check(!RNG::LumioseSolver::tryGetSeed(parameters, *record, foundSeed), "LumioseSolver does not search a shiny Pokemon with a random shiny state");
                }
            }
        }

        /// Returns true when `matches` yields a template of `type`.
        bool yieldsTemplateType8b(Legality::Encounters::MatchedEncounters matches, Legality::Encounters::EncounterTemplateType type)
        {
            for (const Legality::Encounters::MatchedEncounter &match : matches)
            {
                if (match.encounter && match.encounter->templateType() == type)
                {
                    return true;
                }
            }
            return false;
        }

        /// Brilliant Diamond/Shining Pearl encounter-engine branches: the hatch-location table, the
        /// generated tables, a wild slot read in its own game and HOME-remapped into Sword/Shield, the
        /// roamer correlation requirement, the WB8 version rule, egg creation and the hatched-egg check.
        /// Each expectation is worked from PKHeX's own code and data.
        void encounter8bVectors(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            // EggHatchLocation8b: Spear Pillar is 216 in Brilliant Diamond and 217 in Shining Pearl,
            // Hearthome City's 94 is unobtainable, and the table ends at 657.
            context.check(Encounters::isValidMet8BD(216) && !Encounters::isValidMet8SP(216), "BDSP hatch location: Spear Pillar 216 is Brilliant Diamond only");
            context.check(!Encounters::isValidMet8BD(217) && Encounters::isValidMet8SP(217), "BDSP hatch location: Spear Pillar 217 is Shining Pearl only");
            context.check(!Encounters::isValidMet8BD(94) && !Encounters::isValidMet8SP(94), "BDSP hatch location: Hearthome City 94 is unobtainable");
            context.check(Encounters::isValidMet8BD(657) && !Encounters::isValidMet8BD(658), "BDSP hatch location: the table ends at 657");
            context.check(Encounters::isUnderground8b(508) && !Encounters::isUnderground8b(507) && Encounters::isMarsh8b(219) && !Encounters::isMarsh8b(225), "the Grand Underground starts at 508 and the Great Marsh spans 219-224");
            context.check(Encounters::getRemapState(GameVersion::BDSP, GameVersion::SWSH) == Encounters::LocationRemapState::Remapped, "a BDSP origin in Sword/Shield is remapped");

            context.check(!Encounters::getAreas8bBD().empty() && !Encounters::getAreas8bSP().empty() && !Encounters::getStatic8bBDSP().empty() && !Encounters::getTrade8bBDSP().empty() && !Encounters::getGifts8bWB8().empty(), "a BDSP encounter table is empty");

            // A Brilliant Diamond overworld grass slot: matched in its own game at its own location and
            // nowhere else, and matched anywhere once HOME moved it to Sword (met at SWBD).
            const Encounters::EncounterArea8b *grassArea = nullptr;
            for (const Encounters::EncounterArea8b &area : Encounters::getAreas8bBD())
            {
                if (area.type() == Encounters::SlotType8b::Grass && !Encounters::isUnderground8b(area.location()) && !Encounters::isMarsh8b(area.location()) && area.slotCount() > 0)
                {
                    grassArea = &area;
                    break;
                }
            }
            if (context.check(grassArea != nullptr, "Brilliant Diamond has an overworld grass area"))
            {
                const Encounters::EncounterSlot8b slot = grassArea->slotAt(0);
                Legality::Evolutions::EvolutionChain slotChain;
                slotChain.stageCount = 1;
                slotChain.stages[0].speciesId = slot.speciesId();
                slotChain.stages[0].formId = slot.formId();
                slotChain.stages[0].levelMin = slot.levelMin();
                slotChain.stages[0].levelMax = slot.levelMin();
                const std::string slotNote = "species=" + std::to_string(slot.speciesId()) + " location=" + std::to_string(grassArea->location());
                if (std::unique_ptr<Pokemon::Pokemon> diamondRecord = makeBlankRecord9(context, GameVersion::BDSP))
                {
                    diamondRecord->setSpecies(slot.speciesId());
                    diamondRecord->setForm(slot.formId());
                    diamondRecord->setOriginGame(static_cast<uint8_t>(GameVersion::BD));
                    diamondRecord->setEggLocation(Encounters::LOCATION_DEFAULT8B_NONE);
                    diamondRecord->setMetLocation(grassArea->location());
                    diamondRecord->setMetLevel(slot.levelMin());
                    diamondRecord->setLevel(slot.levelMin());
                    context.check(yieldsTemplateType8b(Encounters::EncounterEnumerator8b::enumerate(*diamondRecord, slotChain, GameVersion::BD), Encounters::EncounterTemplateType::Slot8b), "a Brilliant Diamond wild Pokemon matches its grass slot", slotNote);
                    diamondRecord->setMetLocation(0);
                    context.check(!yieldsTemplateType8b(Encounters::EncounterEnumerator8b::enumerate(*diamondRecord, slotChain, GameVersion::BD), Encounters::EncounterTemplateType::Slot8b), "a Brilliant Diamond wild Pokemon met nowhere matches no slot", slotNote);
                }
                if (std::unique_ptr<Pokemon::Pokemon> swordRecord = makeBlankRecord9(context, GameVersion::SWSH))
                {
                    swordRecord->setSpecies(slot.speciesId());
                    swordRecord->setForm(slot.formId());
                    // HOME rewrites a Brilliant Diamond Pokemon's version to Sword and its met location to SWBD.
                    swordRecord->setOriginGame(static_cast<uint8_t>(GameVersion::SW));
                    swordRecord->setMetLocation(Encounters::LOCATION_HOME_SWBD);
                    swordRecord->setMetLevel(slot.levelMin());
                    swordRecord->setLevel(slot.levelMin());
                    context.check(yieldsTemplateType8b(Encounters::EncounterEnumerator8b::enumerateSwordShield(*swordRecord, slotChain, GameVersion::BD), Encounters::EncounterTemplateType::Slot8b), "a Brilliant Diamond wild Pokemon moved to Sword by HOME matches its slot", slotNote);

                    // WB8.CanBeReceivedByVersion: a BDSP game always; a Sword/Shield record only at the
                    // HOME location paired with its version (Brilliant Diamond -> Sword).
                    const Encounters::MysteryGiftWB8 &anyCard = Encounters::getGifts8bWB8().front();
                    context.check(anyCard.canRecordBeReceivedByVersion(GameVersion::SP, *swordRecord), "a WB8 can be received in Shining Pearl");
                    context.check(anyCard.canRecordBeReceivedByVersion(GameVersion::SW, *swordRecord), "a WB8 reaches Sword through HOME at SWBD");
                    swordRecord->setOriginGame(static_cast<uint8_t>(GameVersion::SH));
                    context.check(!anyCard.canRecordBeReceivedByVersion(GameVersion::SH, *swordRecord), "a WB8 does not reach Shield at SWBD");
                }
            }

            // EncounterStatic8b: Mesprit roams and must carry the roamer correlation; a fixed static
            // must not.
            const Encounters::EncounterStatic8b *roamer = nullptr;
            const Encounters::EncounterStatic8b *stationary = nullptr;
            for (const Encounters::EncounterStatic8b &encounter : Encounters::getStatic8bBDSP())
            {
                if (encounter.isRoaming() && encounter.speciesId() == 481 && roamer == nullptr)
                {
                    roamer = &encounter;
                }
                if (!encounter.isRoaming() && stationary == nullptr)
                {
                    stationary = &encounter;
                }
            }
            if (context.check(roamer != nullptr && stationary != nullptr, "the BDSP static table holds a roaming Mesprit and a stationary encounter"))
            {
                if (std::unique_ptr<Pokemon::Pokemon> roamerRecord = makeBlankRecord9(context, GameVersion::BDSP))
                {
                    context.check(roamer->getRequirement(*roamerRecord) == Encounters::StaticCorrelation8bRequirement::MustHave, "a roaming Mesprit must carry the roamer correlation");
                    context.check(stationary->getRequirement(*roamerRecord) == Encounters::StaticCorrelation8bRequirement::MustNotHave, "a stationary BDSP static must not carry the roamer correlation");
                }
            }

            // EncounterGenerator8b.TryGetEgg: a Pikachu reaching a level 1 Pichu hatched from a Pichu egg.
            std::array<Legality::Evolutions::EvolutionCriteria, 2> pichuChain{};
            pichuChain[0].speciesId = 25;
            pichuChain[0].levelMin = 1;
            pichuChain[0].levelMax = 100;
            pichuChain[1].speciesId = 172;
            pichuChain[1].levelMin = 1;
            pichuChain[1].levelMax = 100;
            Encounters::EncounterEgg8b egg(0, 0, GameVersion::BD);
            if (context.check(Encounters::EncounterGenerator8b::tryGetEgg(pichuChain, GameVersion::BD, egg), "a Pikachu reaching a level 1 Pichu hatched from a BDSP egg"))
            {
                context.check(egg.speciesId() == 172 && egg.contextGroup() == GameVersion::BDSP, "the BDSP Pikachu-line egg is a Pichu", "species=" + std::to_string(egg.speciesId()));
            }

            // VerifyEncounterEgg8BDSP: a Shining Pearl egg hatched at Brilliant Diamond's Spear Pillar
            // (216) hatched somewhere Shining Pearl cannot reach; at its own Spear Pillar (217) it is valid.
            if (std::unique_ptr<Pokemon::Pokemon> hatched = makeBlankRecord9(context, GameVersion::BDSP))
            {
                hatched->setSpecies(172);
                hatched->setOriginGame(static_cast<uint8_t>(GameVersion::SP));
                hatched->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                hatched->setEggLocation(Encounters::LOCATION_DAYCARE8B);
                hatched->setMetLocation(216);
                hatched->setMetLevel(1);
                hatched->setLevel(1);
                hatched->setBall(static_cast<uint8_t>(Enums::Ball::Poke));
                hatched->refreshChecksum();
                context.check(hasParseResult(*hatched, Legality::ResultCode::EggLocationInvalid, false), "a Shining Pearl egg hatched at Brilliant Diamond's Spear Pillar is EggLocationInvalid");
                hatched->setMetLocation(217);
                hatched->refreshChecksum();
                context.check(hasParseResult(*hatched, Legality::ResultCode::EggLocation, true), "a Shining Pearl egg hatched at its own Spear Pillar is valid");
            }
        }

        /**
         * Legends: Arceus move-shop mastery, which neither parity corpus breaks: a wild slot's initial
         * moves carry the mastered flags PKHeX's SetEncounterMasteryFlags gives them
         * (IsForcedMasteryCorrect, EncMasteryInitial), and LegendsArceusVerifier.CheckMastery's
         * MoveShopMasterInvalid / MoveShopMasterNotLearned on a later mastered move.
         */
        void masteryVectors8a(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;
            namespace LearnSource = Legality::LearnSource;

            // A plain slot whose initial moves include a permitted move-shop move the game masters at
            // capture, with a later permitted level-up shop move and an unpermitted shop move.
            const Encounters::EncounterArea8a *masteryArea = nullptr;
            uint16_t masterySlotIndex = 0;
            int initialShopIndex = -1;
            int laterShopIndex = -1;
            int unpermittedShopIndex = -1;
            std::array<uint16_t, 4> initialMoves{};
            const std::span<const Encounters::EncounterArea8a> areas = Encounters::getAreas8aLA();
            for (size_t areaIndex = 1; areaIndex < areas.size() && masteryArea == nullptr; areaIndex++)
            {
                if (areas[areaIndex].type() != Encounters::SlotType8a::Standard)
                {
                    continue;
                }
                for (uint16_t slotIndex = 0; slotIndex < areas[areaIndex].slotCount() && masteryArea == nullptr; slotIndex++)
                {
                    const Encounters::EncounterSlot8a candidate = areas[areaIndex].slotAt(slotIndex);
                    if (candidate.isAlpha() || candidate.fixedGender() != Encounters::GENDER_RANDOM || candidate.flawlessIVCount() != 0 || candidate.speciesId() == 265)
                    {
                        continue;
                    }
                    const uint8_t metLevel = candidate.levelMin();
                    const uint64_t permissions = Pokemon::getMoveShopPermissions8LA(candidate.speciesId(), candidate.formId());
                    const LearnSource::Learnset learnset = LearnSource::getLearnset(LearnSource::LearnEnvironment::PLA, candidate.speciesId(), candidate.formId());
                    const std::span<const Pokemon::LevelUpMove> masteryLevels = Pokemon::getMasteryLevels8LA(candidate.speciesId(), candidate.formId());
                    std::array<uint16_t, 4> candidateMoves{};
                    learnset.setEncounterMoves(metLevel, candidateMoves);
                    int candidateInitial = -1;
                    for (const uint16_t moveId : candidateMoves)
                    {
                        const int shopIndex = Encounters::getMoveShopIndex8a(moveId);
                        if (moveId == 0 || shopIndex < 0 || ((permissions >> shopIndex) & 1) == 0)
                        {
                            continue;
                        }
                        const auto masteryEntry = std::find_if(masteryLevels.begin(), masteryLevels.end(), [moveId](const Pokemon::LevelUpMove &entry) { return entry.moveId == moveId; });
                        if (masteryEntry == masteryLevels.end() || masteryEntry->level <= metLevel)
                        {
                            candidateInitial = shopIndex;
                            break;
                        }
                    }
                    int candidateLater = -1;
                    int candidateUnpermitted = -1;
                    for (int shopIndex = 0; shopIndex < Pokemon::MOVE_SHOP_COUNT_8LA; shopIndex++)
                    {
                        uint8_t learnLevel = 0;
                        const bool isPermitted = ((permissions >> shopIndex) & 1) != 0;
                        if (!isPermitted && candidateUnpermitted < 0)
                        {
                            candidateUnpermitted = shopIndex;
                        }
                        if (isPermitted && candidateLater < 0 && learnset.tryGetLevelLearnMove(Pokemon::MOVE_SHOP_MOVES_8LA[shopIndex], learnLevel) && learnLevel > metLevel)
                        {
                            candidateLater = shopIndex;
                        }
                    }
                    if (candidateInitial >= 0 && candidateLater >= 0 && candidateUnpermitted >= 0)
                    {
                        masteryArea = &areas[areaIndex];
                        masterySlotIndex = slotIndex;
                        initialShopIndex = candidateInitial;
                        laterShopIndex = candidateLater;
                        unpermittedShopIndex = candidateUnpermitted;
                        initialMoves = candidateMoves;
                    }
                }
            }
            if (!context.check(masteryArea != nullptr, "Legends: Arceus has a plain slot whose initial moves include a mastered move-shop move"))
            {
                return;
            }
            const Encounters::EncounterSlot8a slot = masteryArea->slotAt(masterySlotIndex);
            const std::string slotNote = "species=" + std::to_string(slot.speciesId()) + " level=" + std::to_string(slot.levelMin()) + " move=" + std::to_string(Pokemon::MOVE_SHOP_MOVES_8LA[initialShopIndex]);
            std::unique_ptr<Pokemon::Pokemon> record = makeBlankRecord9(context, GameVersion::PLA);
            if (!record)
            {
                return;
            }
            auto &legendsRecord = static_cast<Pokemon::Pokemon8LA &>(*record);
            legendsRecord.setSpecies(slot.speciesId());
            legendsRecord.setForm(slot.formId());
            legendsRecord.setOriginGame(static_cast<uint8_t>(GameVersion::PLA));
            legendsRecord.setMetLocation(masteryArea->location());
            legendsRecord.setMetLevel(slot.levelMin());
            legendsRecord.setLevel(slot.levelMin());
            for (int moveSlot = 0; moveSlot < 4; moveSlot++)
            {
                legendsRecord.setMove(moveSlot, initialMoves[static_cast<size_t>(moveSlot)]);
            }
            // PKHeX SetEncounterMasteryFlags: every permitted initial shop move mastered by its met level.
            const uint64_t permissions = Pokemon::getMoveShopPermissions8LA(slot.speciesId(), slot.formId());
            const std::span<const Pokemon::LevelUpMove> masteryLevels = Pokemon::getMasteryLevels8LA(slot.speciesId(), slot.formId());
            for (const uint16_t moveId : initialMoves)
            {
                const int shopIndex = Encounters::getMoveShopIndex8a(moveId);
                if (moveId == 0 || shopIndex < 0 || ((permissions >> shopIndex) & 1) == 0)
                {
                    continue;
                }
                const auto masteryEntry = std::find_if(masteryLevels.begin(), masteryLevels.end(), [moveId](const Pokemon::LevelUpMove &entry) { return entry.moveId == moveId; });
                if (masteryEntry == masteryLevels.end() || slot.levelMin() >= masteryEntry->level)
                {
                    legendsRecord.setMoveShopMastered(shopIndex, true);
                }
            }
            legendsRecord.refreshChecksum();
            context.check(slot.isForcedMasteryCorrect(legendsRecord), "a wild Legends: Arceus capture with the game's initial mastery flags is correct", slotNote);
            context.check(!reportsCode(Legality::analyze(legendsRecord, GameVersion::PLA), Legality::Severity::Invalid, Legality::CheckIdentifier::Misc, Legality::ResultCode::EncounterMasteryInitial), "correct initial mastery flags are not flagged", slotNote);

            legendsRecord.setMoveShopMastered(initialShopIndex, false);
            legendsRecord.refreshChecksum();
            context.check(!slot.isForcedMasteryCorrect(legendsRecord), "an initial move the game masters at capture, left unmastered, is incorrect", slotNote);
            context.check(reportsCode(Legality::analyze(legendsRecord, GameVersion::PLA), Legality::Severity::Invalid, Legality::CheckIdentifier::Misc, Legality::ResultCode::EncounterMasteryInitial), "an unmastered initial move is flagged EncMasteryInitial", slotNote);

            legendsRecord.setMoveShopMastered(initialShopIndex, true);
            legendsRecord.setMoveShopPurchased(initialShopIndex, true);
            context.check(!slot.isForcedMasteryCorrect(legendsRecord), "a move-shop purchase of a move the capture already knew is incorrect", slotNote);
            legendsRecord.setMoveShopPurchased(initialShopIndex, false);

            legendsRecord.setMoveShopMastered(laterShopIndex, true);
            legendsRecord.refreshChecksum();
            context.check(reportsCode(Legality::analyze(legendsRecord, GameVersion::PLA), Legality::Severity::Invalid, Legality::CheckIdentifier::RelearnMove, Legality::ResultCode::MoveShopMasterNotLearned), "a mastered move above the current level is flagged MoveShopMasterNotLearned", slotNote);
            legendsRecord.setMoveShopMastered(laterShopIndex, false);

            legendsRecord.setMoveShopMastered(unpermittedShopIndex, true);
            legendsRecord.refreshChecksum();
            context.check(reportsCode(Legality::analyze(legendsRecord, GameVersion::PLA), Legality::Severity::Invalid, Legality::CheckIdentifier::RelearnMove, Legality::ResultCode::MoveShopMasterInvalid), "a mastered move the species may not master is flagged MoveShopMasterInvalid", slotNote);
        }

        /// Legends: Arceus encounter-engine branches: the generated tables, a wild slot matched at its
        /// own location and HOME-remapped into Sword/Shield (SWLA), the Alpha flag deferral, a static,
        /// and the WA8 version rule. Each expectation is worked from PKHeX's own code and data.
        void encounter8aVectors(Context &context)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            context.check(!Encounters::getAreas8aLA().empty() && !Encounters::getStatic8aLA().empty() && !Encounters::getGifts8aWA8().empty(), "a Legends: Arceus encounter table is empty");
            context.check(Encounters::getStatic8aLA().front().speciesId() == 722 && Encounters::getStatic8aLA().front().fixedBall() == Enums::Ball::LAPoke, "StaticLA starts with the Rowlet gift in a Legends: Arceus Poke Ball");

            // A plain spawner slot: no Alpha, any gender, no flawless IVs, and not in the first area
            // (whose first slots PKHeX's enumerator skips).
            const std::span<const Encounters::EncounterArea8a> areas = Encounters::getAreas8aLA();
            const Encounters::EncounterArea8a *standardArea = nullptr;
            uint16_t standardSlotIndex = 0;
            for (size_t areaIndex = 1; areaIndex < areas.size() && standardArea == nullptr; areaIndex++)
            {
                if (areas[areaIndex].type() != Encounters::SlotType8a::Standard)
                {
                    continue;
                }
                for (uint16_t slotIndex = 0; slotIndex < areas[areaIndex].slotCount(); slotIndex++)
                {
                    const Encounters::EncounterSlot8a candidate = areas[areaIndex].slotAt(slotIndex);
                    if (!candidate.isAlpha() && candidate.fixedGender() == Encounters::GENDER_RANDOM && candidate.flawlessIVCount() == 0 && candidate.speciesId() != 265)
                    {
                        standardArea = &areas[areaIndex];
                        standardSlotIndex = slotIndex;
                        break;
                    }
                }
            }
            if (context.check(standardArea != nullptr, "Legends: Arceus has a plain spawner slot"))
            {
                const Encounters::EncounterSlot8a slot = standardArea->slotAt(standardSlotIndex);
                Legality::Evolutions::EvolutionChain slotChain;
                slotChain.stageCount = 1;
                slotChain.stages[0].speciesId = slot.speciesId();
                slotChain.stages[0].formId = slot.formId();
                slotChain.stages[0].levelMin = slot.levelMin();
                slotChain.stages[0].levelMax = slot.levelMin();
                const std::string slotNote = "species=" + std::to_string(slot.speciesId()) + " location=" + std::to_string(standardArea->location());
                context.check(slot.longName() == "Wild Encounter (PLA) Standard", "a Legends: Arceus spawner slot is named as PKHeX names it", slot.longName());
                if (std::unique_ptr<Pokemon::Pokemon> legendsRecord = makeBlankRecord9(context, GameVersion::PLA))
                {
                    legendsRecord->setSpecies(slot.speciesId());
                    legendsRecord->setForm(slot.formId());
                    legendsRecord->setOriginGame(static_cast<uint8_t>(GameVersion::PLA));
                    legendsRecord->setMetLocation(standardArea->location());
                    legendsRecord->setMetLevel(slot.levelMin());
                    legendsRecord->setLevel(slot.levelMin());
                    bool isMatched = false;
                    for (const Encounters::MatchedEncounter &match : Encounters::EncounterEnumerator8a::enumerate(*legendsRecord, slotChain))
                    {
                        if (match.encounter && match.encounter->templateType() == Encounters::EncounterTemplateType::Slot8a && match.rating == Encounters::EncounterMatchRating::Match)
                        {
                            isMatched = true;
                            break;
                        }
                    }
                    context.check(isMatched, "a Legends: Arceus wild Pokemon matches its spawner slot", slotNote);
                    // An Alpha flag on a slot that never spawns Alphas is a deferred match with errors.
                    legendsRecord->setAlpha(true);
                    bool isDeferred = false;
                    for (const Encounters::MatchedEncounter &match : Encounters::EncounterEnumerator8a::enumerate(*legendsRecord, slotChain))
                    {
                        if (match.encounter && match.encounter->templateType() == Encounters::EncounterTemplateType::Slot8a)
                        {
                            isDeferred = match.rating == Encounters::EncounterMatchRating::DeferredErrors;
                        }
                    }
                    context.check(isDeferred, "an Alpha met at a non-Alpha slot is a deferred match", slotNote);
                    legendsRecord->setAlpha(false);
                    legendsRecord->setMetLocation(0);
                    context.check(!yieldsTemplateType8b(Encounters::EncounterEnumerator8a::enumerate(*legendsRecord, slotChain), Encounters::EncounterTemplateType::Slot8a), "a Legends: Arceus wild Pokemon met nowhere matches no slot", slotNote);
                }
                if (std::unique_ptr<Pokemon::Pokemon> swordRecord = makeBlankRecord9(context, GameVersion::SWSH))
                {
                    swordRecord->setSpecies(slot.speciesId());
                    swordRecord->setForm(slot.formId());
                    swordRecord->setOriginGame(static_cast<uint8_t>(GameVersion::SW));
                    swordRecord->setMetLocation(Encounters::LOCATION_HOME_SWLA);
                    swordRecord->setMetLevel(slot.levelMin());
                    swordRecord->setLevel(slot.levelMin());
                    context.check(yieldsTemplateType8b(Encounters::EncounterEnumerator8a::enumerate(*swordRecord, slotChain), Encounters::EncounterTemplateType::Slot8a), "a Legends: Arceus wild Pokemon moved to Sword by HOME (SWLA) matches its slot", slotNote);

                    // WA8.CanBeReceivedByVersion: Legends: Arceus, or Sword for a PK8.
                    const Encounters::MysteryGiftWA8 &anyCard = Encounters::getGifts8aWA8().front();
                    context.check(anyCard.canRecordBeReceivedByVersion(GameVersion::PLA, *swordRecord) && anyCard.canRecordBeReceivedByVersion(GameVersion::SW, *swordRecord) && !anyCard.canRecordBeReceivedByVersion(GameVersion::SH, *swordRecord), "a WA8 reaches Legends: Arceus, and Sword (not Shield) through HOME");
                }
            }

            // EncounterGenerator3's HOME Celebi workaround: a Celebi from the Switch FireRed (version 56),
            // received through HOME, is WC8 card 9036 and is matched by the Gen 8 generator. (In Sword/
            // Shield HOME rewrites the version to Sword, so the record that keeps version 56 is a PK9.)
            if (std::unique_ptr<Pokemon::Pokemon> celebi = makeBlankRecord9(context, GameVersion::SV))
            {
                celebi->setSpecies(251);
                celebi->setOriginGame(56);
                celebi->setTID16(60720);
                celebi->setSID16(0);
                celebi->setOTGender(0);
                celebi->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                celebi->setOTName(u"HOME");
                celebi->setEncryptionConstant(0x10BE2E7B);
                celebi->setPID(0xD1F72269);
                celebi->setMetLocation(59995);
                celebi->setMetLevel(50);
                celebi->setLevel(50);
                celebi->setBall(static_cast<uint8_t>(Enums::Ball::Cherish));
                celebi->setNature(24);
                celebi->setFatefulEncounter(true);
                Legality::Evolutions::EvolutionChain celebiChain;
                celebiChain.stageCount = 1;
                celebiChain.stages[0].speciesId = 251;
                celebiChain.stages[0].levelMin = 50;
                celebiChain.stages[0].levelMax = 50;
                Legality::Encounters::LegalityInfo info(*celebi, Legality::AnalysisOptions{});
                bool isHomeGift = false;
                for (const Encounters::EncounterReference &encounter : Encounters::EncounterGenerator3::getEncounters(*celebi, celebiChain, info, static_cast<GameVersion>(56)))
                {
                    isHomeGift = isHomeGift || (encounter && encounter->templateType() == Encounters::EncounterTemplateType::MysteryGiftWC8);
                }
                context.check(isHomeGift, "a Switch FireRed Celebi received through HOME matches the Gen 8 HOME gift");
            }

            // StaticLA: Zorua-1 at Fieldlands Camp (27) spans levels 27-29.
            Legality::Evolutions::EvolutionChain zoruaChain;
            zoruaChain.stageCount = 1;
            zoruaChain.stages[0].speciesId = 570;
            zoruaChain.stages[0].formId = 1;
            zoruaChain.stages[0].levelMin = 29;
            zoruaChain.stages[0].levelMax = 29;
            if (std::unique_ptr<Pokemon::Pokemon> zorua = makeBlankRecord9(context, GameVersion::PLA))
            {
                zorua->setSpecies(570);
                zorua->setForm(1);
                zorua->setOriginGame(static_cast<uint8_t>(GameVersion::PLA));
                zorua->setMetLocation(27);
                zorua->setMetLevel(29);
                zorua->setLevel(29);
                context.check(yieldsTemplateType8b(Encounters::EncounterEnumerator8a::enumerate(*zorua, zoruaChain), Encounters::EncounterTemplateType::Static8a), "a level 29 Hisuian Zorua met at its static location matches StaticLA");
                zorua->setMetLevel(30);
                context.check(!yieldsTemplateType8b(Encounters::EncounterEnumerator8a::enumerate(*zorua, zoruaChain), Encounters::EncounterTemplateType::Static8a), "a level 30 Hisuian Zorua is past the static's level range");
            }
        }

        void plantedMoveDefects(Context &context, Trainer::Trainer &trainer)
        {
            if (!Enums::isGen3Group(context.group))
            {
                return;
            }
            auto control = trainer.createBlankPokemon();
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing")) return;
            control->setSpecies(25);  // Pikachu
            // A blank record names no game; the encounter engine runs for a Gen 3 origin.
            control->setOriginGame(static_cast<uint8_t>(Enums::getGroupRepVersion(context.group)));
            control->setMove(0, 84);  // Thunder Shock, its level 1 move in every Gen 3 game
            control->setMove(1, 45);  // Growl, likewise
            control->refreshChecksum();
            auto defective = control->clone();
            if (!context.check(defective != nullptr, "clone() returned nothing for this format")) return;
            defective->setMove(1, 166); // Sketch: Smeargle's alone
            defective->refreshChecksum();
            const auto listIssues = [](const Legality::Report &legalityReport)
            {
                std::string issues;
                for (const auto &issue : legalityReport.issues)
                {
                    issues += (issues.empty() ? "" : "; ") + issue.text;
                }
                return issues;
            };
            const Legality::Report controlReport = Legality::analyze(*control, context.group);
            const Legality::Report defectiveReport = Legality::analyze(*defective, context.group);
            context.check(!reportsInvalid(controlReport, "Invalid Move"), "the move verifier flags a Gen 3 Pikachu's level 1 moves", listIssues(controlReport));
            context.check(reportsInvalid(defectiveReport, "Invalid Move 2:"), "the move verifier does not flag a Gen 3 Pikachu that knows Sketch", listIssues(defectiveReport));
        }
        /// The templates EncounterGenerator6 / EncounterGenerator7X yield for a Pokemon, in order, as
        /// "version:LongName" (the version as PKSE's GameVersion value).
        std::vector<std::string> getCandidates67(const Pokemon::Pokemon &pokemon)
        {
            namespace Encounters = Legality::Encounters;
            Encounters::LegalityInfo info(pokemon, Legality::AnalysisOptions{});
            std::vector<std::string> names;
            const uint8_t generation = Encounters::getOriginGeneration(pokemon);
            Encounters::EncounterCandidates candidates = generation == 6 ? Encounters::EncounterGenerator6::getEncounters(pokemon, info) : Encounters::EncounterGenerator7X::getEncounters(pokemon, info);
            for (const Encounters::EncounterReference &candidate : candidates)
            {
                names.push_back(std::to_string(static_cast<int>(candidate->version())) + ":" + candidate->longName());
            }
            return names;
        }

        std::string joinCandidates67(const std::vector<std::string> &names)
        {
            std::string joined;
            for (const std::string &name : names)
            {
                joined += (joined.empty() ? "" : ", ") + name;
            }
            return joined;
        }

        /// Gen 6 and Gen 7 encounter-engine branches neither parity corpus reaches: the hatch-location
        /// tables, the Friend Safari, Rock Smash and DexNav slots, the traded-egg version swap, the
        /// Virtual Console transfer templates and the Wonder Card version restrictions. Each expectation
        /// is worked from PKHeX's own code and data.
        void encounter67Vectors(Context &context, Trainer::Trainer &trainer)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;
            const auto version = [](GameVersion game) { return static_cast<uint8_t>(game); };

            // EggHatchLocation6: X/Y map ids are the even ids 6-168 but 80; Omega Ruby/Alpha Sapphire 170-354 but 348.
            context.check(Encounters::isValidMet6XY(38) && !Encounters::isValidMet6XY(80) && !Encounters::isValidMet6XY(39) && !Encounters::isValidMet6XY(170), "X/Y hatch location: 38 valid; 80, 39 and 170 invalid");
            context.check(Encounters::isValidMet6AO(318) && !Encounters::isValidMet6AO(348) && !Encounters::isValidMet6AO(38), "Omega Ruby/Alpha Sapphire hatch location: 318 valid; 348 and 38 invalid");
            // EggHatchLocation7: 78 hatches in all four games, 198 only in Ultra Sun/Ultra Moon, Poke Pelago everywhere.
            context.check(Encounters::isValidMet7SM(78) && Encounters::isValidMet7USUM(78), "Gen 7 hatch location: 78 is valid in all four games");
            context.check(!Encounters::isValidMet7SM(198) && Encounters::isValidMet7USUM(198), "Gen 7 hatch location: 198 is Ultra Sun/Ultra Moon only");
            context.check(Encounters::isValidMet7SM(Encounters::LOCATION_PELAGO7) && Encounters::isValidMet7USUM(Encounters::LOCATION_PELAGO7), "Gen 7 hatch location: Poke Pelago is valid in all four games");

            // EncounterArea6XY: both X and Y end with the shared Friend Safari, whose Vivillon is any pattern.
            for (const std::span<const Encounters::EncounterArea6XY> areas : {Encounters::getAreas6X(), Encounters::getAreas6Y()})
            {
                if (!context.check(!areas.empty(), "the X/Y wild tables are filled"))
                {
                    continue;
                }
                const Encounters::EncounterArea6XY &safari = areas.back();
                context.check(safari.type() == Encounters::SlotType6::FriendSafari && safari.location() == 148 && safari.version() == GameVersion::XY, "the last X/Y area is the Friend Safari");
                const Encounters::EncounterSlot6XY &vivillon = safari.slotAt(safari.slotCount() - 1);
                context.check(vivillon.speciesId() == 666 && vivillon.isRandomUnspecificForm() && vivillon.flawlessIVCount() == 2 && vivillon.ability() == Legality::RNG::AbilityPermission::Any12H, "Friend Safari Vivillon: any pattern, two perfect IVs, hidden ability possible");
                context.check(vivillon.longName() == "Wild Encounter (XY) FriendSafari", "Friend Safari slot name", vivillon.longName());
            }

            // EncounterSlot6AO: Rock Smash has no DexNav (no egg-move bonus, no hidden ability); other slots do.
            bool foundRockSmash = false;
            bool foundDexNav = false;
            for (const Encounters::EncounterArea6AO &area : Encounters::getAreas6AS())
            {
                if (area.slotCount() == 0)
                {
                    continue;
                }
                const Encounters::EncounterSlot6AO &slot = area.slotAt(0);
                if (area.type() == Encounters::SlotType6::Rock_Smash && !foundRockSmash)
                {
                    foundRockSmash = true;
                    context.check(!slot.isMoveBonusPossible() && slot.ability() == Legality::RNG::AbilityPermission::Any12, "Rock Smash slot: no DexNav bonus, no hidden ability");
                    context.check(slot.longName() == "Wild Encounter (AS) Rock Smash", "Rock Smash slot name", slot.longName());
                }
                else if (area.type() == Encounters::SlotType6::Standard && !foundDexNav)
                {
                    foundDexNav = true;
                    context.check(slot.isMoveBonusPossible() && slot.ability() == Legality::RNG::AbilityPermission::Any12H, "standard slot: DexNav bonus and hidden ability possible");
                }
            }
            context.check(foundRockSmash && foundDexNav, "Alpha Sapphire has Rock Smash and standard slots");

            // MutateEggTrade: X <-> Alpha Sapphire, Y <-> Omega Ruby; Sun <-> Ultra Sun, Moon <-> Ultra Moon.
            context.check(Encounters::EncounterGenerator6::mutateEggTrade(Encounters::EncounterEgg6(172, 0, GameVersion::X)).version() == GameVersion::AS, "a traded X egg may come from Alpha Sapphire");
            context.check(Encounters::EncounterGenerator6::mutateEggTrade(Encounters::EncounterEgg6(172, 0, GameVersion::OR)).version() == GameVersion::Y, "a traded Omega Ruby egg may come from Y");
            context.check(Encounters::EncounterGenerator7::mutateEggTrade(Encounters::EncounterEgg7(172, 0, GameVersion::SN)).version() == GameVersion::US, "a traded Sun egg may come from Ultra Sun");
            context.check(Encounters::EncounterGenerator7::mutateEggTrade(Encounters::EncounterEgg7(172, 0, GameVersion::UM)).version() == GameVersion::MN, "a traded Ultra Moon egg may come from Moon");

            // EncounterTransfer7: Mew is fateful, never shiny and has five perfect IVs; Gengar cannot have
            // its hidden ability through Poke Transporter, Pikachu always does.
            const Encounters::EncounterTransfer7 mew = Encounters::EncounterTransfer7::getVC1(151, 5);
            context.check(mew.isFatefulEncounter() && mew.shiny() == Legality::RNG::Shiny::Never && mew.flawlessIVCount() == 5 && mew.location() == Encounters::LOCATION_TRANSFER1, "Virtual Console Mew: fateful, never shiny, five perfect IVs, Gen 1 transfer location");
            context.check(mew.name() == "Virtual Console Transfer (RBY)", "Virtual Console Gen 1 template name", mew.name());
            context.check(Encounters::EncounterTransfer7::getVC1(94, 30).ability() == Legality::RNG::AbilityPermission::OnlyFirst, "Virtual Console Gengar: no hidden ability");
            const Encounters::EncounterTransfer7 pikachu = Encounters::EncounterTransfer7::getVC2(25, 10);
            context.check(pikachu.ability() == Legality::RNG::AbilityPermission::OnlyHidden && pikachu.flawlessIVCount() == 3 && pikachu.location() == Encounters::LOCATION_TRANSFER2, "Virtual Console Gen 2 Pikachu: hidden ability, three perfect IVs, Gen 2 transfer location");
            context.check(Encounters::EncounterTransfer7::getVC2(251, 30).isFatefulEncounter() && Encounters::EncounterTransfer7::getVC2(251, 30).ability() == Legality::RNG::AbilityPermission::OnlyFirst, "Virtual Console Celebi: fateful, no hidden ability");

            // WC6/WC7 CanBeReceivedByVersion: restriction bit 2 is Alpha Sapphire, bit 0 Sun.
            Encounters::MysteryGiftWC6::Properties alphaSapphireCard;
            alphaSapphireCard.restrictVersion = 4;
            const Encounters::MysteryGiftWC6 alphaSapphireGift(alphaSapphireCard);
            context.check(alphaSapphireGift.canBeReceivedByVersion(GameVersion::AS) && !alphaSapphireGift.canBeReceivedByVersion(GameVersion::OR) && !alphaSapphireGift.canBeReceivedByVersion(GameVersion::X), "WC6 restricted to Alpha Sapphire");
            context.check(alphaSapphireGift.version() == GameVersion::AS, "WC6 restriction 4 is the Alpha Sapphire card");
            Encounters::MysteryGiftWC7::Properties sunCard;
            sunCard.restrictVersion = 1;
            const Encounters::MysteryGiftWC7 sunGift(sunCard);
            context.check(sunGift.canBeReceivedByVersion(GameVersion::SN) && !sunGift.canBeReceivedByVersion(GameVersion::MN) && !sunGift.canBeReceivedByVersion(GameVersion::US), "WC7 restricted to Sun");
            context.check(!Encounters::getMysteryGifts6().empty() && !Encounters::getMysteryGifts7().empty(), "the WC6 and WC7 tables are filled");

            // Let's Go: an area also matches the neighbours its Pokemon can wander into, never location 0;
            // a slot crosses over only when its flag for that neighbour is set.
            bool foundCrossover = false;
            for (const Encounters::EncounterArea7b &area : Encounters::getAreas7bGP())
            {
                if (area.toAreaFirst() == 0 || area.slotCount() == 0)
                {
                    continue;
                }
                foundCrossover = true;
                context.check(area.isMatchLocation(area.location()) && area.isMatchLocation(area.toAreaFirst()) && !area.isMatchLocation(0), "Let's Go area: matches its own location and its first neighbour, never 0");
                break;
            }
            context.check(foundCrossover, "Let's Go Pikachu has an area with a crossover neighbour");
            context.check(!Encounters::getStatic7bGG().empty() && Encounters::getStatic7bGP().size() == 2 && Encounters::getStatic7bGE().size() == 2, "the Let's Go static tables are filled");
            context.check(Encounters::getTradeGift7bGG().size() == 6 && Encounters::getTradeGift7bGP().size() == 2 && Encounters::getTradeGift7bGE().size() == 2, "the Let's Go trade tables are filled");
            Encounters::MysteryGiftWB7::Properties eeveeCard;
            eeveeCard.restrictVersion = 2;
            const Encounters::MysteryGiftWB7 eeveeGift(eeveeCard);
            context.check(eeveeGift.canBeReceivedByVersion(GameVersion::GE) && !eeveeGift.canBeReceivedByVersion(GameVersion::GP) && !eeveeGift.canBeReceivedByVersion(GameVersion::SN), "WB7 restricted to Let's Go Eevee");
            context.check(eeveeGift.canBeAnyLanguage() && eeveeGift.canHaveLanguage(static_cast<int>(Enums::LanguageID::Korean)), "a WB7 without language locks takes any language");
            Encounters::MysteryGiftWB7::Properties chinaCard;
            chinaCard.cardId = 1502;
            const Encounters::MysteryGiftWB7 chinaGift(chinaCard);
            context.check(!chinaGift.canBeAnyLanguage() && chinaGift.canHaveLanguage(static_cast<int>(Enums::LanguageID::ChineseSimplified)) && !chinaGift.canHaveLanguage(static_cast<int>(Enums::LanguageID::English)), "a mainland China WB7 is Simplified Chinese only");

            // A hatched Pichu whose egg was traded (egg location Link Trade) yields its own game's egg, then
            // the paired game's, and no split-breed (Pichu is the lowest stage).
            auto pokemon = trainer.createBlankPokemon();
            if (!pokemon)
            {
                return;
            }
            // Let's Go records are Gen 7 too, but carry no egg data.
            const GameVersion contextGroup = Encounters::getEntityContextGroup(*pokemon);
            if (contextGroup != GameVersion::XY && contextGroup != GameVersion::SM)
            {
                return;
            }
            const bool isGen6 = contextGroup == GameVersion::XY;
            auto pichu = pokemon->clone();
            if (context.check(pichu != nullptr, "clone() returned nothing for this format"))
            {
                pichu->setSpecies(172);
                pichu->setOriginGame(version(isGen6 ? GameVersion::X : GameVersion::SN));
                pichu->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                pichu->setEggLocation(Encounters::LOCATION_LINK_TRADE6);
                pichu->setMetLocation(isGen6 ? 38 : 78);
                pichu->setMetLevel(1);
                pichu->setLevel(1);
                pichu->refreshChecksum();
                const std::vector<std::string> names = getCandidates67(*pichu);
                const GameVersion pairVersion = isGen6 ? GameVersion::AS : GameVersion::US;
                const bool isExpected = names.size() == 2 && names[0].starts_with(std::to_string(pichu->originGame()) + ":") && names[1].starts_with(std::to_string(static_cast<int>(pairVersion)) + ":");
                context.check(isExpected, "a traded-egg Pichu yields its own game's egg, then the paired game's", joinCandidates67(names));
            }

            // X/Y's Route 7 Snorlax: level 15 at location 38, never shiny.
            if (isGen6)
            {
                auto snorlax = pokemon->clone();
                if (context.check(snorlax != nullptr, "clone() returned nothing for this format"))
                {
                    snorlax->setSpecies(143);
                    snorlax->setOriginGame(version(GameVersion::Y));
                    snorlax->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                    snorlax->setEggLocation(0);
                    snorlax->setMetLocation(38);
                    snorlax->setMetLevel(15);
                    snorlax->setLevel(15);
                    snorlax->refreshChecksum();
                    const std::vector<std::string> names = getCandidates67(*snorlax);
                    context.check(!names.empty() && names[0] == std::to_string(static_cast<int>(GameVersion::XY)) + ":Static Encounter", "the Route 7 Snorlax matches the X/Y static", joinCandidates67(names));
                    snorlax->setMetLevel(16);
                    snorlax->refreshChecksum();
                    const std::vector<std::string> wrongLevel = getCandidates67(*snorlax);
                    context.check(wrongLevel.empty() || wrongLevel[0] != std::to_string(static_cast<int>(GameVersion::XY)) + ":Static Encounter", "a level 16 Snorlax at Route 7 is not the static", joinCandidates67(wrongLevel));
                }
            }
        }

        /// Gen 1-2 encounter-engine branches: the Yellow catch-rate table, the headbutt tree
        /// permissions, the tradeback guess, the Game Boy match order and the Gen 2 egg met data. Each
        /// expectation is worked from PKHeX's own code and data.
        void encounter12Vectors(Context &context, Trainer::Trainer &trainer)
        {
            using Enums::GameVersion;
            namespace Encounters = Legality::Encounters;

            // PersonalTable.Y has Pikachu at 163 (the Light Ball byte); Red/Blue's has it at 190.
            context.check(Encounters::GBRestrictions::getCatchRate1(GameVersion::YW, 25) == 163, "Gen 1 catch rate: Yellow's Pikachu is 163");
            context.check(Encounters::GBRestrictions::getCatchRate1(GameVersion::RD, 25) == 190, "Gen 1 catch rate: Red/Blue's Pikachu is 190");

            // Encounters1VC.Gift: the level 5 Mew with Pound.
            const std::span<const Encounters::EncounterGift1> virtualConsoleGifts = Encounters::getGifts1VirtualConsole();
            if (context.check(virtualConsoleGifts.size() == 1, "Gen 1 Virtual Console: one gift"))
            {
                context.check(virtualConsoleGifts[0].speciesId() == 151 && virtualConsoleGifts[0].levelMin() == 5 && virtualConsoleGifts[0].name() == "Event Gift", "Gen 1 Virtual Console: the gift is the level 5 Mew", virtualConsoleGifts[0].name());
            }
            context.check(Encounters::getTradeGift2GSC().size() == 11, "Gen 2 in-game trades: eleven");

            // EncounterSlot2.Trees: Route 29 (location 2) lets every trainer id headbutt; Route 34
            // (location 15, 0x0013FE; PKHeX's TreeIndexes put Ilex Forest at 14) refuses a common tree to an id ending in 0.
            bool foundRoute29 = false;
            bool foundRoute34 = false;
            for (const Encounters::EncounterArea2 &area : Encounters::getAreas2GD())
            {
                if (area.type() != Encounters::SlotType2::Headbutt || area.slotCount() == 0)
                {
                    continue;
                }
                const Encounters::EncounterSlot2 &slot = area.slotAt(0);
                if (area.location() == 2 && !foundRoute29)
                {
                    foundRoute29 = true;
                    context.check(slot.isTreeAvailable(10) && slot.isTreeAvailable(19), "headbutt trees: Route 29 has a tree for every trainer id");
                    context.check(slot.longName() == "Wild Encounter (GD) Headbutt", "headbutt slot name", slot.longName());
                }
                if (area.location() == 15 && !foundRoute34)
                {
                    foundRoute34 = true;
                    context.check(!slot.isTreeAvailable(20) && slot.isTreeAvailable(21), "headbutt trees: Route 34 has no common tree for a trainer id ending in 0");
                }
            }
            context.check(foundRoute29 && foundRoute34, "Gold has headbutt slots on Routes 29 and 34");

            auto pokemon = trainer.createBlankPokemon();
            if (!pokemon)
            {
                return;
            }
            const uint8_t formatGeneration = Encounters::getFormatGeneration(*pokemon);
            if (formatGeneration == 1)
            {
                // A Pidgey whose catch rate is its own (255) never left Gen 1, and matches Red's grass first.
                auto pidgey = pokemon->clone();
                if (!context.check(pidgey != nullptr, "clone() returned nothing for this format"))
                {
                    return;
                }
                pidgey->setSpecies(16);
                pidgey->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                pidgey->setLevel(5);
                // Gust, its level 1 move in Red/Blue.
                pidgey->setMove(0, 16);
                pidgey->setMove(1, 0);
                pidgey->setMove(2, 0);
                pidgey->setMove(3, 0);
                static_cast<Pokemon::Pokemon1RBY &>(*pidgey).setCatchRate(255);
                pidgey->refreshChecksum();
                context.check(Encounters::GBRestrictions::getTradebackStatusInitial(*pidgey) == Encounters::PotentialGBOrigin::Gen1Only, "tradeback: a Pidgey holding its own catch rate is Gen 1 only");
                const std::string pidgeyName = getMatchedEncounterName(*pidgey);
                context.check(pidgeyName == "Wild Encounter (RD) Grass", "Gen 1 match: a level 5 Pidgey is Red's grass slot", pidgeyName);

                // Catch rate 0 is a cleared held item: either generation.
                static_cast<Pokemon::Pokemon1RBY &>(*pidgey).setCatchRate(0);
                context.check(Encounters::GBRestrictions::getTradebackStatusInitial(*pidgey) == Encounters::PotentialGBOrigin::Either, "tradeback: catch rate 0 could be either generation");
                return;
            }
            if (formatGeneration != 2 || Encounters::GameBoyEntity::isKorean(*pokemon))
            {
                return;
            }

            // A Pichu hatched in Crystal at level 5 (met level 1, New Bark Town) is the Crystal egg.
            auto pichu = pokemon->clone();
            if (!context.check(pichu != nullptr, "clone() returned nothing for this format"))
            {
                return;
            }
            pichu->setSpecies(172);
            pichu->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            pichu->setEgg(false);
            pichu->setLevel(5);
            pichu->setMove(0, 84);
            pichu->setMove(1, 204);
            pichu->setMove(2, 0);
            pichu->setMove(3, 0);
            pichu->setMetLocation(4);
            pichu->setMetLevel(1);
            pichu->setOTGender(0);
            pichu->refreshChecksum();
            const std::string pichuName = getMatchedEncounterName(*pichu);
            context.check(pichuName == "Egg", "Gen 2 match: a hatched Pichu is an egg", pichuName);
            context.check(hasParseResult(*pichu, Legality::ResultCode::EggLocation, true), "Gen 2 egg: met level 1 at a Johto location is a valid hatch");

            // A hatched egg met at level 3 is not how Crystal records a hatch.
            pichu->setMetLevel(3);
            pichu->refreshChecksum();
            context.check(hasParseResult(*pichu, Legality::ResultCode::EggFMetLevel, false), "Gen 2 egg: a hatched egg met at level 3 is flagged");

            // Past location 95 there is nowhere to hatch.
            pichu->setMetLevel(1);
            pichu->setMetLocation(100);
            pichu->refreshChecksum();
            context.check(hasParseResult(*pichu, Legality::ResultCode::EggMetLocationFail, false), "Gen 2 egg: a hatch past location 95 is flagged");
        }

        /// PKHeX's legal "144 - 3008 - BDFCED1D5D5B.pk3" (PKHeX.Core.Tests/Legality/Legal/Generation 3):
        /// XD's Shadow Articuno, purified, with the National Ribbon that purifying awards.
        constexpr std::array<uint8_t, 100> XD_SHADOW_ARTICUNO_PK3 = {
            0x5b, 0x5d, 0x1d, 0xed, 0x07, 0x00, 0x68, 0x9a, 0xa4, 0xa1, 0xa1, 0xa9, 0xff, 0xe9, 0xe7, 0xff,
            0x00, 0x00, 0x02, 0x02, 0xce, 0xbf, 0xcd, 0xce, 0xff, 0x00, 0x00, 0x00, 0xfc, 0xbd, 0x00, 0x00,
            0x90, 0x00, 0xc8, 0x00, 0xd0, 0x12, 0x13, 0x00, 0xff, 0x55, 0x00, 0x00, 0xa4, 0x00, 0x9c, 0x00,
            0x3a, 0x00, 0x5c, 0x00, 0x10, 0x10, 0x10, 0x10, 0xfc, 0x00, 0x00, 0xe2, 0x00, 0x20, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0xb2, 0x27, 0x1f, 0xfc, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x81,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00};

        /// The Colosseum/XD search (EncounterGenerator3GC) and the rules that read its templates.
        void encounter3GCVectors(Context &context)
        {
            namespace Encounters = Legality::Encounters;

            // Encounters3Colo: the Espeon and Umbreon starters; every e-Reader card is met at location 128.
            context.check(Encounters::getStarters3Colo().size() == 2, "Colosseum: two starters");
            bool isEveryEReaderMarked = !Encounters::getShadow3ColoEReader().empty();
            for (const Encounters::EncounterShadow3Colo &shadow : Encounters::getShadow3ColoEReader())
            {
                isEveryEReaderMarked = isEveryEReaderMarked && shadow.isEReader() && Encounters::isShadowEncounter(shadow);
            }
            context.check(isEveryEReaderMarked, "Colosseum: the e-Reader shadows are e-Reader Shadow Pokemon");
            if (context.check(!Encounters::getShadow3XD().empty(), "XD: shadow table is populated"))
            {
                context.check(Encounters::getShadow3XD()[0].isFatefulEncounter(), "XD: a Shadow Pokemon is a fateful encounter");
            }

            const std::span<const std::byte> articunoBytes(reinterpret_cast<const std::byte *>(XD_SHADOW_ARTICUNO_PK3.data()), XD_SHADOW_ARTICUNO_PK3.size());
            Trainer::PokemonFile::LoadResult loaded = Trainer::PokemonFile::parse(articunoBytes, "articuno.pk3");
            if (!context.check(static_cast<bool>(loaded), "XD Shadow Articuno: the .pk3 parses", loaded.error))
            {
                return;
            }
            Pokemon::Pokemon &articuno = *loaded.pokemon;
            const std::string articunoName = getMatchedEncounterName(articuno);
            context.check(articunoName == "XD Shadow Encounter 76", "XD match: the Articuno is XD's Shadow Articuno", articunoName);
            const Legality::Report articunoReport = Legality::analyze(articuno, articuno.getGameGroup());
            context.check(!reportsInvalid(articunoReport, ""), "XD Shadow Articuno: a purified Shadow Pokemon with its National Ribbon is legal");

            // A PID the shadow-lock RNG cannot produce leaves no Colosseum/XD correlation.
            articuno.setPID(articuno.pid() ^ 0x00010000U);
            articuno.refreshChecksum();
            context.check(reportsInvalid(Legality::analyze(articuno, articuno.getGameGroup()), ""), "XD Shadow Articuno: a PID off its RNG frame is flagged");
        }

        /// The Pokemon Center New York gifts (PKHeX EncounterGift3NY), on a Gen 3 record rebuilt from the
        /// Articuno above: PCNY's first template, Evolution's level 50 Pikachu, generated as PKHeX's
        /// SetPINGA does (an anti-shiny BACD PID, then the IVs).
        void encounterGift3DistributionVectors(Context &context)
        {
            const std::span<const std::byte> baseBytes(reinterpret_cast<const std::byte *>(XD_SHADOW_ARTICUNO_PK3.data()), XD_SHADOW_ARTICUNO_PK3.size());
            Trainer::PokemonFile::LoadResult loaded = Trainer::PokemonFile::parse(baseBytes, "pcny.pk3");
            if (!context.check(static_cast<bool>(loaded), "PCNY base record: the .pk3 parses", loaded.error))
            {
                return;
            }
            const std::span<const Legality::Encounters::EncounterGift3NY> giftsNewYork = Legality::Encounters::getGifts3PCNY();
            context.check(giftsNewYork.size() == 50, "PCNY: fifty gifts");
            context.check(Legality::Encounters::getGifts3PCJP().size() == 58, "PCJP: fifty-eight gifts");
            Pokemon::Pokemon &pikachu = *loaded.pokemon;
            constexpr uint16_t TRAINER_ID = 1234;
            constexpr uint8_t VERSION_RUBY = 2;
            pikachu.setSpecies(25);
            pikachu.setOriginGame(VERSION_RUBY);
            pikachu.setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            pikachu.setTID16(TRAINER_ID);
            pikachu.setSID16(0);
            pikachu.setOTName(u"PCNYc");
            pikachu.setOTGender(0);
            pikachu.setFatefulEncounter(false);
            pikachu.setBall(static_cast<uint8_t>(Enums::Ball::Poke));
            pikachu.setLevel(50);
            pikachu.setMetLevel(50);
            pikachu.setMetLocation(255);
            const uint16_t pikachuMoves[4] = {85, 97, 87, 113};
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                pikachu.setMove(moveIndex, pikachuMoves[moveIndex]);
            }
            uint32_t seed = 0x12345678U;
            pikachu.setPID(Legality::RNG::CommonEvent3::getAntishiny(seed, TRAINER_ID));
            const uint32_t individualValues = Legality::RNG::ClassicEraRNG::getSequentialIVs(seed);
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                pikachu.setIV(statIndex, static_cast<uint8_t>((individualValues >> (statIndex * 5)) & 0x1F));
            }
            pikachu.refreshChecksum();
            const std::string pikachuName = getMatchedEncounterName(pikachu);
            context.check(pikachuName == "PCNY Gift", "PCNY match: the Evolution distribution's Pikachu", pikachuName);

            // PCNYd did not run the Evolution distribution.
            pikachu.setOTName(u"PCNYd");
            pikachu.refreshChecksum();
            const std::string wrongMachineName = getMatchedEncounterName(pikachu);
            context.check(wrongMachineName != "PCNY Gift", "PCNY: a machine that did not run the distribution does not match", wrongMachineName);
        }

    }

    // ---- Verifiers that read the encounter engine's template: Mystery Gift cards, Pokemon GO and
    // the GO date and ball rules. Each plants the template as the match, as the encounter search
    // would, and runs one verifier against a control and a defective Pokemon.
    namespace
    {
        Legality::Report runTemplateVerifier(VerifierFunction verifier, const Pokemon::Pokemon &pokemon, Enums::GameVersion group, const Legality::Encounters::EncounterReference &encounter)
        {
            Legality::Report legalityReport;
            const Legality::AnalysisOptions analysisOptions;
            Legality::Encounters::LegalityInfo legalityInfo(pokemon, analysisOptions);
            legalityInfo.setEncounterMatch(encounter);
            Legality::AnalysisContext analysisContext{pokemon, group, getGroupGeneration(group), analysisOptions, legalityReport, Legality::EncounterInfo{}};
            analysisContext.legalityInfo = &legalityInfo;
            verifier(analysisContext);
            return legalityReport;
        }

        void expectTemplateVerifierFlags(Context &context, VerifierFunction verifier, const Pokemon::Pokemon &control, const Pokemon::Pokemon &defective, const Legality::Encounters::EncounterReference &encounter, const VerifierExpectation &expectation)
        {
            const std::string defectLabel = std::string("planted template defect PKHeX ") + Legality::getCheckIdentifierSourceName(expectation.identifier) + "/" + Legality::getResultCodeSourceName(expectation.code) + " (" + encounter->longName() + ") on a blank " + Enums::getGameVersionName(context.group) + " entity";
            context.check(!reportsCode(runTemplateVerifier(verifier, control, context.group, encounter), expectation.severity, expectation.identifier, expectation.code), "legality verifier flags a planted template defect on the control Pokemon that does not have it", defectLabel);
            context.check(reportsCode(runTemplateVerifier(verifier, defective, context.group, encounter), expectation.severity, expectation.identifier, expectation.code), "legality verifier did not flag a planted template defect", defectLabel);
        }

        /// PKHeX's GO tables, dates and ball rules, which need no entity of a particular game.
        void pogoTableVectors(Context &context)
        {
            namespace Encounters = Legality::Encounters;
            context.check(Encounters::getAreasGoLetsGo().size() == 170 && Encounters::getAreasGoHome().size() == 1173, "the GO Park and GO-to-HOME tables hold PKHeX's 170 and 1173 species-form groups");
            // GO launched 2016-07-06; PKHeX's day 0 is the day before, for time zones.
            context.check(Encounters::getDayNumber(2016, 7, 6) == 736150, "DateOnly.DayNumber of GO's launch day is 736150");
            const Encounters::PogoCalendarDate launch = Encounters::getPogoCalendarDate(1);
            context.check(launch.year == 2016 && launch.month == 7 && launch.day == 6, "GO day 1 is 2016-07-06");
            if (context.check(!Encounters::getAreasGoLetsGo().empty() && Encounters::getAreasGoLetsGo().front().slotCount > 0, "the GO Park table has a first slot"))
            {
                // PKHeX's first GO Park slot: Bulbasaur, wild, open-ended, local start day.
                const Encounters::EncounterSlot7GO &first = Encounters::getAreasGoLetsGo().front().slots[0];
                context.check(first.longName() == "GO Encounter (GO) (Wild): 2016.07.06-X", "the first GO Park slot is named as PKHeX names it", first.longName());
            }

            // A window 10-20 (relative days): day 15 inside, day 21 outside; an open end runs to today.
            const Encounters::EncounterSlot8GO windowed(10, 20, 25, 0, 1, 0, Legality::RNG::Shiny::Random, Encounters::POGO_GENDER_RANDOM, Encounters::PogoType::Wild, Encounters::PogoBallRestriction::None, Encounters::PogoImportFormat::PK8, 0);
            const Encounters::PogoCalendarDate inside = Encounters::getPogoCalendarDate(15);
            const Encounters::PogoCalendarDate outside = Encounters::getPogoCalendarDate(21);
            context.check(windowed.isWithinStartEnd(inside.year, inside.month, inside.day) && !windowed.isWithinStartEnd(outside.year, outside.month, outside.day), "a GO window holds its own days only");
            context.check(windowed.longName() == "GO Encounter (GO) (Wild): 2016.07.15-2016.07.25", "a dated GO slot names both ends", windowed.longName());

            // PogoBallRestriction.IsValidBall and the fixed balls it implies.
            using Enums::Ball;
            context.check(Encounters::isPogoBallValid(Encounters::PogoBallRestriction::Standard, Ball::Ultra, Encounters::PogoType::Wild, 0) && !Encounters::isPogoBallValid(Encounters::PogoBallRestriction::Standard, Ball::Master, Encounters::PogoType::Wild, 0), "a Standard GO restriction allows Ultra but not Master Balls");
            context.check(Encounters::isPogoBallValid(Encounters::PogoBallRestriction::StandardMaster, Ball::Master, Encounters::PogoType::Wild, 0), "a StandardMaster GO restriction allows the Master Ball");
            context.check(!Encounters::isPogoBallValid(Encounters::PogoBallRestriction::None, Ball::Strange, Encounters::PogoType::Raid, 0) && Encounters::isPogoBallValid(Encounters::PogoBallRestriction::None, Ball::Strange, Encounters::PogoType::Raid, Encounters::POGO_FLAG_FEATURED_WILD_AREA), "a GO raid's Safari Ball needs the Wild Area flag");
            context.check(Encounters::getPogoFixedBall(Encounters::PogoBallRestriction::OnlyPremier) == Ball::Premier && Encounters::getPogoFixedBall(Encounters::PogoBallRestriction::Standard) == Ball::None, "only a single-ball GO restriction fixes the ball");

            // IFixedOTFriendship: Timburr is 70, Hoopa Unbound 50, and HOME turns a base 70 into 50.
            const auto friendship = [](uint16_t speciesId, uint8_t formId) { return Encounters::EncounterSlot8GO(0, 0, speciesId, formId, 1, 0, Legality::RNG::Shiny::Random, Encounters::POGO_GENDER_RANDOM, Encounters::PogoType::Wild, Encounters::PogoBallRestriction::None, Encounters::PogoImportFormat::PK8, 0).originalTrainerFriendship(); };
            context.check(friendship(532, 0) == 70 && friendship(720, 1) == 50 && friendship(25, 0) == 50, "GO's original-trainer friendship: Timburr 70, Hoopa Unbound 50, Pikachu 50");
            // The import format picks the context: a PK8 import is Sword/Shield, a PK9 Scarlet/Violet.
            const Encounters::EncounterSlot8GO scarletViolet(0, 0, 25, 0, 1, 0, Legality::RNG::Shiny::Random, Encounters::POGO_GENDER_RANDOM, Encounters::PogoType::Wild, Encounters::PogoBallRestriction::None, Encounters::PogoImportFormat::PK9, 0);
            context.check(friendship(25, 0) == 50 && scarletViolet.contextGroup() == Enums::GameVersion::SV && windowed.contextGroup() == Enums::GameVersion::SWSH, "a GO import's format sets its context");
        }

        /// A Sword/Shield Pokemon transferred from GO: IVs, the Master Ball date and the fixed
        /// original-trainer friendship, against the engine's EncounterSlot8GO.
        void pogoEntityVectors(Context &context, const Trainer::Trainer &trainer)
        {
            namespace Encounters = Legality::Encounters;
            if (context.group != Enums::GameVersion::SWSH)
            {
                return;
            }
            constexpr uint16_t PIKACHU = 25;
            constexpr uint8_t LEVEL = 20;
            auto control = createPokemon(trainer, PIKACHU, LEVEL);
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing"))
            {
                return;
            }
            const auto slot = Encounters::makeEncounter<Encounters::EncounterSlot8GO>(0, 0, PIKACHU, 0, 1, 0, Legality::RNG::Shiny::Random, Encounters::POGO_GENDER_RANDOM, Encounters::PogoType::Wild, Encounters::PogoBallRestriction::StandardMaster, Encounters::PogoImportFormat::PK8, 0);
            const auto &pogo = static_cast<const Encounters::EncounterSlot8GO &>(*slot);
            control->setOriginGame(static_cast<uint8_t>(Encounters::VERSION_POKEMON_GO));
            control->setMetLocation(Encounters::LOCATION_GO8);
            control->setMetLevel(LEVEL);
            control->setMetYear(22);
            control->setMetMonth(1);
            control->setMetDay(1);
            control->setBall(static_cast<uint8_t>(Enums::Ball::Poke));
            control->setOTFriendship(pogo.originalTrainerFriendship());
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                control->setIV(statIndex, 31); // GO's 15 doubled plus one
            }
            control->refreshChecksum();

            // GetIVsValid: attack and special attack are one GO stat, so they must agree.
            auto unevenIVs = control->clone();
            unevenIVs->setIV(1, 30);
            unevenIVs->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyIndividualValues, *control, *unevenIVs, slot, {Legality::Severity::Invalid, Legality::CheckIdentifier::IndividualValues, Legality::ResultCode::IVNotCorrect});

            // IsBallValid: no Master Ball before 2023-05-21; any of a StandardMaster slot's balls after.
            auto masterBall = control->clone();
            masterBall->setBall(static_cast<uint8_t>(Enums::Ball::Master));
            masterBall->refreshChecksum();
            context.check(!pogo.isBallValid(Enums::Ball::Master, PIKACHU, *masterBall), "a GO Master Ball met before 2023-05-21 is refused");
            masterBall->setMetYear(24);
            masterBall->refreshChecksum();
            context.check(pogo.isBallValid(Enums::Ball::Master, PIKACHU, *masterBall) && !pogo.isBallValid(Enums::Ball::Strange, PIKACHU, *masterBall), "a GO Master Ball met in 2024 is allowed; a Strange Ball outside a Wild Area weekend is not");
            context.check(!pogo.isBallValid(Enums::Ball::Ultra, 292, *control), "a Shedinja that evolved after GO keeps only the Poke Ball");

            // HistoryVerifier: GO never lets its original trainer handle it, so the friendship is fixed.
            auto changedFriendship = control->clone();
            changedFriendship->setOTFriendship(static_cast<uint8_t>(pogo.originalTrainerFriendship() + 1));
            changedFriendship->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyHistory, *control, *changedFriendship, slot, {Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryStatFriendshipOTBaseEvent});
        }

        /// Black/White Mystery Gift cards (PGF): ability, met level, nickname and language.
        void mysteryGiftVectors(Context &context, const Trainer::Trainer &trainer)
        {
            namespace Encounters = Legality::Encounters;
            if (context.group != Enums::GameVersion::BW && context.group != Enums::GameVersion::B2W2)
            {
                return;
            }
            constexpr uint16_t PIKACHU = 25;
            constexpr uint8_t LEVEL = 10;
            constexpr uint16_t STATIC_ABILITY = 9;
            constexpr uint16_t LIGHTNING_ROD_ABILITY = 31;
            constexpr uint8_t ENGLISH = 2;
            Encounters::MysteryGiftPGF::Properties card;
            card.speciesId = PIKACHU;
            card.level = LEVEL;
            card.metLevel = LEVEL;
            card.abilityType = 0; // first ability only
            card.PIDType = 1;
            card.restrictLanguage = ENGLISH;
            const auto gift = Encounters::makeEncounter<Encounters::MysteryGiftPGF>(card);

            auto control = createPokemon(trainer, PIKACHU, LEVEL);
            if (!context.check(control != nullptr, "createBlankPokemon() returned nothing"))
            {
                return;
            }
            control->setMetLevel(LEVEL);
            control->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::B)); // PKHeX HasOriginalMetLevel reads the origin
            control->setFatefulEncounter(true);
            control->setLanguage(ENGLISH);
            control->setAbility(STATIC_ABILITY);
            control->setAbilityNumber(1);
            control->refreshChecksum();

            // LevelVerifier: a card fixes the level it is met at.
            auto wrongMetLevel = control->clone();
            wrongMetLevel->setMetLevel(LEVEL + 2);
            wrongMetLevel->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyLevel, *control, *wrongMetLevel, gift, {Legality::Severity::Invalid, Legality::CheckIdentifier::Level, Legality::ResultCode::LevelMetGift});

            // AbilityVerifier: a first-ability card cannot give the hidden ability.
            auto hiddenAbility = control->clone();
            hiddenAbility->setAbility(LIGHTNING_ROD_ABILITY);
            hiddenAbility->setAbilityNumber(4);
            hiddenAbility->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyAbility, *control, *hiddenAbility, gift, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ability, Legality::ResultCode::AbilityHiddenFail});

            // NicknameVerifier: a card that names nothing leaves the species name.
            auto nicknamed = control->clone();
            nicknamed->setNickname(u"Sparky");
            nicknamed->setIsNicknamed(true);
            nicknamed->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyNickname, *control, *nicknamed, gift, {Legality::Severity::Invalid, Legality::CheckIdentifier::Nickname, Legality::ResultCode::EncounterGiftNicknamed});

            // MiscFatefulEncounterVerifier: a card restricted to English was not given in Japanese.
            auto wrongLanguage = control->clone();
            wrongLanguage->setLanguage(1);
            wrongLanguage->refreshChecksum();
            expectTemplateVerifierFlags(context, Legality::verifyMisc, *control, *wrongLanguage, gift, {Legality::Severity::Invalid, Legality::CheckIdentifier::Language, Legality::ResultCode::EncounterGiftLanguageNotDistributed});
        }

        Legality::Encounters::RibbonIndexSet makeRibbonIndexSet(Legality::Encounters::RibbonIndex ribbon)
        {
            Legality::Encounters::RibbonIndexSet ribbons;
            const size_t ribbonValue = static_cast<size_t>(ribbon);
            ribbons.bits[ribbonValue >> 3] = static_cast<uint8_t>(ribbons.bits[ribbonValue >> 3] | (1u << (ribbonValue & 7)));
            return ribbons;
        }

        /// Sets one ribbon on the record, by its sprite stem; false when the format has no such ribbon.
        bool setTemplateRibbon(Pokemon::Pokemon &pokemon, Enums::GameVersion group, std::string_view spriteStem)
        {
            for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(group))
            {
                if (spriteStem == ribbon.spriteStem)
                {
                    Names::setRibbonValue(reinterpret_cast<uint8_t *>(pokemon.getData().data()), ribbon, 1);
                    pokemon.refreshChecksum();
                    return true;
                }
            }
            return false;
        }

        /// RibbonVerifier against a card's own ribbons (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7,
        /// IRibbonPartner): a card ribbon must be kept, and a ribbon no card or game gives is refused.
        void templateRibbonVectors(Context &context, const Trainer::Trainer &trainer)
        {
            namespace Encounters = Legality::Encounters;
            constexpr uint16_t PIKACHU = 25;
            constexpr uint8_t LEVEL = 10;
            const VerifierExpectation missing{Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsMissing};
            const VerifierExpectation invalid{Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsInvalid};
            auto blank = createPokemon(trainer, PIKACHU, LEVEL);
            if (!context.check(blank != nullptr, "createBlankPokemon() returned nothing"))
            {
                return;
            }
            blank->setMetLevel(LEVEL);
            blank->setFatefulEncounter(true);
            blank->refreshChecksum();
            auto withRibbon = [&](std::string_view spriteStem)
            {
                auto pokemon = blank->clone();
                context.check(setTemplateRibbon(*pokemon, context.group, spriteStem), "the format has the ribbon the test plants", std::string(spriteStem));
                return pokemon;
            };

            if (context.group == Enums::GameVersion::BW || context.group == Enums::GameVersion::B2W2)
            {
                Encounters::MysteryGiftPGF::Properties card;
                card.speciesId = PIKACHU;
                card.level = LEVEL;
                card.metLevel = LEVEL;
                const auto plainGift = Encounters::makeEncounter<Encounters::MysteryGiftPGF>(card);
                card.ribbons = makeRibbonIndexSet(Encounters::RibbonIndex::Classic);
                const auto classicGift = Encounters::makeEncounter<Encounters::MysteryGiftPGF>(card);
                // RibbonVerifierEvent4.ParseEvent: the card's Classic Ribbon must be there.
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *withRibbon("ribbonclassic"), *blank, classicGift, missing);
                // ... and a Wishing Ribbon the card does not give is refused.
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *blank, *withRibbon("ribbonwishing"), plainGift, invalid);
                // RibbonVerifierEvent3: a Gen 5 card's Country Ribbon is the card's alone.
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *blank, *withRibbon("ribboncountry"), plainGift, invalid);
            }
            if (context.group == Enums::GameVersion::SWSH)
            {
                // RibbonRules.IsRibbonValidAlolaChamp: a card that gives the Alola Champion Ribbon keeps it.
                Encounters::MysteryGiftWC8::Properties card;
                card.speciesId = PIKACHU;
                card.level = LEVEL;
                card.metLevel = LEVEL;
                card.ribbons = makeRibbonIndexSet(Encounters::RibbonIndex::ChampionAlola);
                const auto gift = Encounters::makeEncounter<Encounters::MysteryGiftWC8>(card);
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *withRibbon("ribbonchampionalola"), *blank, gift, missing);
                context.check(!Encounters::isEncounterDateRestricted(*gift), "a Sword/Shield card below 9000 is not a HOME gift, so its date is not server-set");
            }
            if (context.group == Enums::GameVersion::SV)
            {
                // RibbonVerifierCommon9: the Partner Ribbon is exactly the template's.
                Encounters::MysteryGiftWC9::Properties card;
                card.speciesId = PIKACHU;
                card.level = LEVEL;
                card.metLevel = LEVEL;
                const auto plainGift = Encounters::makeEncounter<Encounters::MysteryGiftWC9>(card);
                card.ribbons = makeRibbonIndexSet(Encounters::RibbonIndex::Partner);
                const auto partnerGift = Encounters::makeEncounter<Encounters::MysteryGiftWC9>(card);
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *withRibbon("ribbonpartner"), *blank, partnerGift, invalid);
                expectTemplateVerifierFlags(context, Legality::verifyRibbons, *blank, *withRibbon("ribbonpartner"), plainGift, invalid);
                context.check(Encounters::isEncounterDateRestricted(*plainGift), "every Scarlet/Violet card's met date is server-set");
            }
        }

        /// MemoryVerifier.VerifyOTMemory: a card or in-game trade fixes its original trainer's memory.
        void templateMemoryVectors(Context &context, const Trainer::Trainer &trainer)
        {
            namespace Encounters = Legality::Encounters;
            constexpr uint16_t PIKACHU = 25;
            constexpr uint8_t LEVEL = 10;
            const auto reportsFixedMemoryMismatch = [&](const Pokemon::Pokemon &pokemon, const Encounters::EncounterReference &encounter)
            {
                return reportsCode(runTemplateVerifier(Legality::verifyMemories, pokemon, context.group, encounter), Legality::Severity::Invalid, Legality::CheckIdentifier::Memory, Legality::ResultCode::MemoryIndexId);
            };
            auto blank = createPokemon(trainer, PIKACHU, LEVEL);
            if (blank != nullptr)
            {
                blank->setMetLevel(LEVEL);
                blank->refreshChecksum();
            }
            if (context.group == Enums::GameVersion::SWSH && context.check(blank != nullptr, "createBlankPokemon() returned nothing"))
            {
                Encounters::MysteryGiftWC8::Properties card;
                card.speciesId = PIKACHU;
                card.level = LEVEL;
                card.metLevel = LEVEL;
                const auto emptyMemoryCard = Encounters::makeEncounter<Encounters::MysteryGiftWC8>(card);
                card.trainerMemory = 31;
                card.trainerMemoryIntensity = 3;
                const auto memoryCard = Encounters::makeEncounter<Encounters::MysteryGiftWC8>(card);
                card.trainerGender = 3;
                const auto recipientCard = Encounters::makeEncounter<Encounters::MysteryGiftWC8>(card);
                context.check(!reportsFixedMemoryMismatch(*blank, emptyMemoryCard), "a card with no memory accepts a Pokemon with none");
                context.check(reportsFixedMemoryMismatch(*blank, memoryCard), "a Pokemon without its card's fixed original-trainer memory is flagged");
                context.check(!reportsFixedMemoryMismatch(*blank, recipientCard), "a card the recipient is the original trainer of fixes no memory");
            }
            if (context.group == Enums::GameVersion::SM && context.check(blank != nullptr, "createBlankPokemon() returned nothing"))
            {
                Encounters::EncounterTrade7::Properties trade;
                trade.speciesId = PIKACHU;
                trade.level = LEVEL;
                const auto tradeEncounter = Encounters::makeEncounter<Encounters::EncounterTrade7>(trade);
                context.check(reportsFixedMemoryMismatch(*blank, tradeEncounter), "a Gen 7 in-game trade without its fixed original-trainer memory is flagged");
            }
        }

        /// IndividualValueVerifier.VerifyIVsMystery and VerifyIVsRanch4, MarkRules.IsMarkPresentTitan,
        /// FormVerifier's Scarlet/Violet Vivillon and IAlphaReadOnly, against the engine's template.
        void templateEncounterVectors(Context &context, const Trainer::Trainer &trainer)
        {
            namespace Encounters = Legality::Encounters;
            constexpr uint16_t PIKACHU = 25;
            constexpr uint16_t VIVILLON = 666;
            constexpr uint8_t VIVILLON_POKE_BALL_FORM = 19;
            constexpr uint8_t LEVEL = 10;
            const auto makeBlank = [&](uint16_t speciesId) -> std::unique_ptr<Pokemon::Pokemon>
            {
                auto pokemon = createPokemon(trainer, speciesId, LEVEL);
                if (pokemon != nullptr)
                {
                    pokemon->setMetLevel(LEVEL);
                    pokemon->refreshChecksum();
                }
                return pokemon;
            };
            auto blank = makeBlank(PIKACHU);
            if (!context.check(blank != nullptr, "createBlankPokemon() returned nothing"))
            {
                return;
            }

            if (context.group == Enums::GameVersion::BW || context.group == Enums::GameVersion::B2W2)
            {
                // A card's fixed IVs must be the Pokemon's.
                Encounters::MysteryGiftPGF::Properties card;
                card.speciesId = PIKACHU;
                card.level = LEVEL;
                card.metLevel = LEVEL;
                card.individualValues = {31, 31, 31, 31, 31, 31};
                const auto gift = Encounters::makeEncounter<Encounters::MysteryGiftPGF>(card);
                auto control = blank->clone();
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    control->setIV(statIndex, 31);
                }
                control->refreshChecksum();
                auto lowerHitPoints = control->clone();
                lowerHitPoints->setIV(0, 30);
                lowerHitPoints->refreshChecksum();
                expectTemplateVerifierFlags(context, Legality::verifyIndividualValues, *control, *lowerHitPoints, gift, {Legality::Severity::Invalid, Legality::CheckIdentifier::IndividualValues, Legality::ResultCode::EncounterGiftIVMismatch});
            }
            if (context.group == Enums::GameVersion::DP)
            {
                // The Ranch gift's IVs come from three rand() calls: HP 7, Atk 11, Def 26, Spe 0,
                // SpA 2, SpD 2 has two seeds; HP 7 with every other IV 0 has none.
                Encounters::EncounterTrade4RanchGift::Properties trade;
                trade.speciesId = PIKACHU;
                trade.level = LEVEL;
                trade.metLevel = LEVEL;
                const auto ranchGift = Encounters::makeEncounter<Encounters::EncounterTrade4RanchGift>(trade);
                auto seeded = blank->clone();
                const uint8_t seededValues[6] = {7, 11, 26, 0, 2, 2}; // Pokemon4DP's index order: HP Atk Def Spe SpA SpD
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    seeded->setIV(statIndex, seededValues[statIndex]);
                }
                seeded->refreshChecksum();
                auto unseeded = blank->clone();
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    unseeded->setIV(statIndex, statIndex == 0 ? 7 : 0);
                }
                unseeded->refreshChecksum();
                if (context.check(seeded->ivSPE() == 0 && seeded->ivSPA() == 2 && seeded->ivDEF() == 26, "the Ranch vector's IVs land in their stats"))
                {
                    expectTemplateVerifierFlags(context, Legality::verifyIndividualValues, *seeded, *unseeded, ranchGift, {Legality::Severity::Invalid, Legality::CheckIdentifier::IndividualValues, Legality::ResultCode::PIDTypeMismatch});
                }
            }
            if (context.group == Enums::GameVersion::SV)
            {
                // Exactly the Titans carry the Titan Mark.
                Encounters::EncounterStatic9::Properties titan;
                titan.speciesId = PIKACHU;
                titan.level = LEVEL;
                titan.isTitan = true;
                const auto titanEncounter = Encounters::makeEncounter<Encounters::EncounterStatic9>(titan);
                auto marked = blank->clone();
                if (context.check(setTemplateRibbon(*marked, context.group, "ribbonmarktitan"), "Scarlet/Violet has the Titan Mark"))
                {
                    expectTemplateVerifierFlags(context, Legality::verifyRibbons, *marked, *blank, titanEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsMissing});
                    titan.isTitan = false;
                    const auto plainEncounter = Encounters::makeEncounter<Encounters::EncounterStatic9>(titan);
                    expectTemplateVerifierFlags(context, Legality::verifyRibbons, *blank, *marked, plainEncounter, {Legality::Severity::Invalid, Legality::CheckIdentifier::Ribbon, Legality::ResultCode::RibbonsInvalid});
                }

                // A Poke Ball Vivillon is its card's own form.
                auto vivillon = makeBlank(VIVILLON);
                if (context.check(vivillon != nullptr, "createBlankPokemon() returned nothing"))
                {
                    vivillon->setForm(VIVILLON_POKE_BALL_FORM);
                    vivillon->refreshChecksum();
                    Encounters::MysteryGiftWC9::Properties card;
                    card.speciesId = VIVILLON;
                    card.level = LEVEL;
                    card.metLevel = LEVEL;
                    card.formId = VIVILLON_POKE_BALL_FORM;
                    const auto pokeBallCard = Encounters::makeEncounter<Encounters::MysteryGiftWC9>(card);
                    card.formId = 18; // Fancy
                    const auto fancyCard = Encounters::makeEncounter<Encounters::MysteryGiftWC9>(card);
                    const auto reportsEventPre = [&](const Encounters::EncounterReference &encounter)
                    {
                        return reportsCode(runTemplateVerifier(Legality::verifyForm, *vivillon, context.group, encounter), Legality::Severity::Invalid, Legality::CheckIdentifier::Form, Legality::ResultCode::FormVivillonEventPre);
                    };
                    context.check(!reportsEventPre(pokeBallCard), "a Poke Ball Vivillon from a Poke Ball card is its card's form");
                    context.check(reportsEventPre(fancyCard), "a Poke Ball Vivillon from a Fancy card is flagged");
                }
            }
            if (context.group == Enums::GameVersion::ZA)
            {
                // IAlphaReadOnly: the template says whether it was an Alpha.
                Encounters::EncounterStatic9a::Properties fixedEncounter;
                fixedEncounter.speciesId = PIKACHU;
                fixedEncounter.level = LEVEL;
                fixedEncounter.isAlpha = true;
                const auto alpha = Encounters::makeEncounter<Encounters::EncounterStatic9a>(fixedEncounter);
                fixedEncounter.isAlpha = false;
                const auto notAlpha = Encounters::makeEncounter<Encounters::EncounterStatic9a>(fixedEncounter);
                const auto isAlphaFor = [&](const Encounters::EncounterReference &encounter)
                {
                    Legality::Report legalityReport;
                    const Legality::AnalysisOptions analysisOptions;
                    Encounters::LegalityInfo legalityInfo(*blank, analysisOptions);
                    legalityInfo.setEncounterMatch(encounter);
                    Legality::AnalysisContext analysisContext{*blank, context.group, getGroupGeneration(context.group), analysisOptions, legalityReport, Legality::EncounterInfo{}};
                    analysisContext.legalityInfo = &legalityInfo;
                    return Legality::isEncounterAlpha(analysisContext);
                };
                context.check(isAlphaFor(alpha) == std::optional<bool>(true) && isAlphaFor(notAlpha) == std::optional<bool>(false), "isEncounterAlpha reads the engine's template");
            }
        }

        void templateVerifierVectors(Context &context, const Trainer::Trainer &trainer)
        {
            templateEncounterVectors(context, trainer);
            templateMemoryVectors(context, trainer);
            pogoTableVectors(context);
            pogoEntityVectors(context, trainer);
            mysteryGiftVectors(context, trainer);
            templateRibbonVectors(context, trainer);
        }
    }

    void legalityRun(Context &context, int chunk)
    {
        Trainer::Trainer *trainer = context.trainer;
        if (!trainer) return;

        if (chunk == PARTY_CHUNK)
        {
            // The documented contract for an empty slot: species 0 analyses to an EMPTY report, not
            // to a pile of complaints about a Pokemon that is not there. The storage view runs the
            // analyzer over whatever the cursor is on, empty slots included.
            plantedDefects(context, *trainer);
            plantedVerifierDefects(context, *trainer);
            plantedRecordDefects(context, *trainer);
            plantedTransferDefects(context, *trainer);
            plantedRibbonDefects(context, *trainer);
            plantedAbilityBallFormDefects(context, *trainer);
            plantedMiscDefects(context, *trainer);
            plantedHistoryMemoryDefects(context, *trainer);
            plantedEraDefects(context, *trainer);
            plantedMoveDefects(context, *trainer);
            breedVectors(context);
            chainBreedVectors(context);
            learnVerifierVectors(context);
            encounter4Vectors(context, *trainer);
            encounter5Vectors(context, *trainer);
            encounter12Vectors(context, *trainer);
            encounter3GCVectors(context);
            encounterGift3DistributionVectors(context);
            encounter67Vectors(context, *trainer);
            encounter8Vectors(context);
            encounter9Vectors(context);
            encounter9aVectors(context);
            encounter8bVectors(context);
            encounter8aVectors(context);
            masteryVectors8a(context);
            templateVerifierVectors(context, *trainer);

            if (auto blankPokemon = trainer->createBlankPokemon())
            {
                Legality::Report legalityReport = Legality::analyze(*blankPokemon, context.group);
                context.check(legalityReport.empty(), "legality analyzer returns findings for an EMPTY slot (species 0)", "issues=" + std::to_string(legalityReport.issues.size()) + (legalityReport.issues.empty() ? "" : " first=\"" + legalityReport.issues[0].text + "\""));
            }

            bool checkDeterminism = true;
            for (size_t partySlotIndex = 0; partySlotIndex < trainer->party.size(); ++partySlotIndex)
            {
                if (isEmptySlot(trainer->party[partySlotIndex].get()))
                {
                    continue;
                }
                analyzeOne(context, *trainer->party[partySlotIndex], partySlot((int)partySlotIndex), checkDeterminism);
                checkDeterminism = false; // the determinism re-run costs a full analysis; once per chunk is enough
            }
            return;
        }

        const size_t boxIndex = (size_t)(chunk - 1);
        if (boxIndex >= trainer->boxes.size()) return;

        const size_t slotsPerBox = trainer->getSlotsPerBox();
        bool checkDeterminism = true;
        int analyzedCount = 0;
        for (size_t slotIndex = 0; slotIndex < slotsPerBox && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
        {
            if (isEmptySlot(trainer->boxes[boxIndex][slotIndex].get()))
            {
                continue;
            }
            analyzeOne(context, *trainer->boxes[boxIndex][slotIndex], slot((int)boxIndex, (int)slotIndex), checkDeterminism);
            checkDeterminism = false;
            ++analyzedCount;
        }
        if (analyzedCount > 0)
        {
            context.note("Pokemon put through the legality analyzer", "box=" + std::to_string(boxIndex + 1) + " count=" + std::to_string(analyzedCount));
        }
    }
}
