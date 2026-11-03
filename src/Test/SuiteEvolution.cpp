/**
 * The evolution engine under Legality/Evolutions, checked against PKHeX's own unit test for it
 * (PKHeX.Core.Tests: Legality/LegalityData.cs) and against evolution histories PKHeX computed for
 * real Pokemon (legalitydump evolution over the test saves).
 *
 * A tree that lists a level-up method before an item method, or a devolution that drops a stage,
 * does not crash or look wrong. It just shrinks a chain, and every verifier that asks "was this
 * Pokemon ever species X in game Y" then answers no for a legitimate Pokemon. These fixed vectors
 * are where that shows up first; tools/harness/evolution_diff.py compares the whole save sweep.
 */
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Test/TestSuite.h"

namespace Test
{

    namespace
    {
        namespace Evolutions = Legality::Evolutions;
        using Enums::GameVersion;

        enum Chunk
        {
            MethodOrderChunk = 0,
            TreeQueryChunk,
            HistoryChunk,
            CHUNK_COUNT,
        };

        /// One expected stage of a chain: species, form, minimum level, maximum level, method.
        struct ExpectedStage
        {
            uint16_t speciesId;
            uint8_t formId;
            uint8_t levelMin;
            uint8_t levelMax;
            Evolutions::EvolutionType method;
        };

        std::string describeChain(std::span<const Evolutions::EvolutionCriteria> chain)
        {
            std::string description = "[";
            for (const Evolutions::EvolutionCriteria &stage : chain)
            {
                description += " " + std::to_string(stage.speciesId) + "-" + std::to_string(stage.formId) + " " + std::to_string(stage.levelMin) + ".." + std::to_string(stage.levelMax) + " " + Evolutions::getEvolutionTypeSourceName(stage.method);
            }
            return description + " ]";
        }

        bool chainEquals(std::span<const Evolutions::EvolutionCriteria> chain, std::span<const ExpectedStage> expected)
        {
            if (chain.size() != expected.size())
            {
                return false;
            }
            for (size_t stageIndex = 0; stageIndex < chain.size(); stageIndex++)
            {
                const Evolutions::EvolutionCriteria &stage = chain[stageIndex];
                const ExpectedStage &expectedStage = expected[stageIndex];
                if (stage.speciesId != expectedStage.speciesId || stage.formId != expectedStage.formId || stage.levelMin != expectedStage.levelMin || stage.levelMax != expectedStage.levelMax || stage.method != expectedStage.method)
                {
                    return false;
                }
            }
            return true;
        }

        void checkChain(Context &context, const char *label, const char *chainName, std::span<const Evolutions::EvolutionCriteria> chain, std::span<const ExpectedStage> expected)
        {
            context.check(chainEquals(chain, expected), label, std::string(chainName) + " gave " + describeChain(chain));
        }

        // PKHeX LegalityData.EvolutionsOrdered: where a species has two ways to evolve, the one that
        // needs no level-up comes first, or the stricter level-up method shadows it.
        void checkMethodOrder(Context &context)
        {
            struct OrderVector
            {
                uint16_t speciesId;
                uint8_t formId;
                const char *speciesName;
            };
            static constexpr OrderVector ORDER_VECTORS[] = {
                {349, 0, "Feebas"},
                {739, 0, "Crabrawler"},
            };
            for (const OrderVector &vector : ORDER_VECTORS)
            {
                int contextsWithTwoMethods = 0;
                for (const GameVersion contextGroup : Evolutions::EVOLUTION_CONTEXT_GROUPS)
                {
                    const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(contextGroup);
                    context.check(tree != nullptr, "evolution tree exists", Evolutions::getEvolutionContextSourceName(contextGroup));
                    if (!tree)
                    {
                        continue;
                    }
                    const std::span<const Evolutions::EvolutionMethod> methods = tree->getForward(vector.speciesId, vector.formId);
                    if (methods.size() <= 1)
                    {
                        continue;
                    }
                    const std::string detail = std::string(vector.speciesName) + " in " + Evolutions::getEvolutionContextSourceName(contextGroup);
                    context.check(!Evolutions::isLevelUpRequired(methods[0].method), "first evolution method needs no level-up", detail);
                    context.check(Evolutions::isLevelUpRequired(methods[1].method), "second evolution method needs a level-up", detail);
                    contextsWithTwoMethods++;
                }
                context.check(contextsWithTwoMethods > 0, "species has two evolution methods somewhere", vector.speciesName);
            }
        }

        void checkTreeQueries(Context &context)
        {
            const Evolutions::EvolutionTree *treeGG = Evolutions::getEvolutionTree(GameVersion::GG);
            context.check(treeGG != nullptr, "Let's Go tree exists", "");
            if (!treeGG)
            {
                return;
            }
            const Evolutions::EvolutionNode ivysaurNode = treeGG->getReverse(2, 0);
            context.check(!ivysaurNode.first.isEmpty() && ivysaurNode.first.sourceSpeciesId == 1, "Ivysaur devolves into Bulbasaur", "Let's Go");
            context.check(ivysaurNode.second.isEmpty(), "Ivysaur has one pre-evolution", "Let's Go");
            context.check(treeGG->isPresentInGame(1, 0) && !treeGG->isPresentInGame(152, 0), "Let's Go has Kanto species only", "Bulbasaur present, Chikorita absent");
            context.check(treeGG->isSpeciesDerivedFrom(3, 0, 1, 0), "Venusaur derives from Bulbasaur", "Let's Go");

            const Evolutions::EvolutionTree *treeSWSH = Evolutions::getEvolutionTree(GameVersion::SWSH);
            context.check(treeSWSH != nullptr, "Sword/Shield tree exists", "");
            if (treeSWSH)
            {
                const std::span<const Evolutions::EvolutionMethod> kirliaMethods = treeSWSH->getForward(281, 0);
                bool hasGallade = false;
                for (const Evolutions::EvolutionMethod &method : kirliaMethods)
                {
                    hasGallade = hasGallade || (method.destinationSpeciesId == 475 && method.method == Evolutions::EvolutionType::UseItemMale);
                }
                context.check(hasGallade, "Kirlia evolves into Gallade with an item when male", "Sword/Shield");
            }
            context.check(Evolutions::getEvolutionTree(GameVersion::Invalid) == nullptr, "no tree for an invalid context", "");
        }

        // Each vector is a Pokemon from the test saves, with the history PKHeX computed for it.
        void checkHistories(Context &context)
        {
            {
                // Let's Go Eevee, an Ivysaur caught as a level 3 Bulbasaur and met at level 7.
                Evolutions::EvolutionEntity entity;
                entity.speciesId = 2;
                entity.currentLevel = 27;
                entity.metLevel = 7;
                entity.formatGroup = GameVersion::GG;
                entity.formatGeneration = 7;
                entity.originGeneration = 7;
                entity.originVersion = 43;
                const Evolutions::EvolutionEncounter encounter{1, GameVersion::GG, 7, 3, false};
                const Evolutions::EvolutionHistory history = Evolutions::getEvolutionChainsAllGens(entity, encounter);
                static constexpr ExpectedStage EXPECTED[] = {
                    {2, 0, 16, 27, Evolutions::EvolutionType::LevelUp},
                    {1, 0, 7, 27, Evolutions::EvolutionType::None},
                };
                checkChain(context, "Let's Go Ivysaur history", "Gen7b", history.getChain(GameVersion::GG), EXPECTED);
                context.check(!history.hasVisitedExcept({GameVersion::GG}), "Let's Go Ivysaur visited only Let's Go", "");
                context.check(history.hasVisitedAs(GameVersion::GG, 1), "Let's Go Ivysaur was a Bulbasaur in Let's Go", "");
            }
            {
                // Red, a level 6 Bulbasaur from a level 5 gift.
                Evolutions::EvolutionEntity entity;
                entity.speciesId = 1;
                entity.currentLevel = 6;
                entity.formatGroup = GameVersion::RBY;
                entity.formatGeneration = 1;
                entity.originGeneration = 1;
                const Evolutions::EvolutionEncounter encounter{1, GameVersion::RBY, 1, 5, false};
                const Evolutions::EvolutionHistory history = Evolutions::getEvolutionChainsAllGens(entity, encounter);
                static constexpr ExpectedStage EXPECTED_HISTORY[] = {
                    {1, 0, 5, 6, Evolutions::EvolutionType::None},
                };
                checkChain(context, "Red Bulbasaur history", "Gen1", history.getChain(GameVersion::RBY), EXPECTED_HISTORY);
                checkChain(context, "Red Bulbasaur history", "Gen2", history.getChain(GameVersion::GSC), EXPECTED_HISTORY);
                context.check(!history.hasVisited(GameVersion::SM), "Red Bulbasaur never reached Gen 7", "");
                static constexpr ExpectedStage EXPECTED_ORIGIN[] = {
                    {1, 0, 2, 6, Evolutions::EvolutionType::None},
                };
                const Evolutions::EvolutionChain origin = Evolutions::getEncounterOriginChainGameBoy(entity, 1, GameVersion::RBY);
                checkChain(context, "Red Bulbasaur origin", "origin", origin.view(), EXPECTED_ORIGIN);
            }
            {
                // Sword, a level 100 male Gallade hatched from a Ralts egg.
                Evolutions::EvolutionEntity entity;
                entity.speciesId = 475;
                entity.currentLevel = 100;
                entity.metLevel = 1;
                entity.eggLocation = 60002;
                entity.formatGroup = GameVersion::SWSH;
                entity.formatGeneration = 8;
                entity.originGeneration = 8;
                entity.originVersion = 44;
                entity.gender = 0;
                const Evolutions::EvolutionEncounter encounter{280, GameVersion::SWSH, 8, 1, false};
                const Evolutions::EvolutionHistory history = Evolutions::getEvolutionChainsAllGens(entity, encounter);
                static constexpr ExpectedStage EXPECTED[] = {
                    {475, 0, 30, 100, Evolutions::EvolutionType::UseItemMale},
                    {281, 0, 20, 100, Evolutions::EvolutionType::LevelUp},
                    {280, 0, 1, 100, Evolutions::EvolutionType::None},
                };
                checkChain(context, "Sword Gallade history", "Gen8", history.getChain(GameVersion::SWSH), EXPECTED);
                context.check(!history.hasVisited(GameVersion::BDSP) && !history.hasVisited(GameVersion::SV), "Sword Gallade without a HOME tracker stays in Sword/Shield", "");
                const Evolutions::EvolutionChain origin = Evolutions::getEncounterOriginChain(entity, 8, GameVersion::SWSH);
                checkChain(context, "Sword Gallade origin", "origin", origin.view(), EXPECTED);
            }
        }
    }

    int evolutionPlan(Context &)
    {
        return CHUNK_COUNT;
    }

    void evolutionRun(Context &context, int chunk)
    {
        switch (chunk)
        {
        case MethodOrderChunk:
            checkMethodOrder(context);
            break;
        case TreeQueryChunk:
            checkTreeQueries(context);
            break;
        case HistoryChunk:
            checkHistories(context);
            break;
        default:
            break;
        }
    }
}
