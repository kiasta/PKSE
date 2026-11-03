#include "Legality/Evolutions/EvolutionGroup.h"

#include <array>

#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Legality/Evolutions/EvolutionUtil.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/PersonalRecord.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            using Enums::GameVersion;

            constexpr uint16_t PIKACHU_SPECIES = 25;
            constexpr uint16_t RAICHU_SPECIES = 26;
            constexpr uint16_t MEOWTH_SPECIES = 52;
            constexpr uint16_t EXEGGUTOR_SPECIES = 103;
            constexpr uint16_t MAROWAK_SPECIES = 105;
            constexpr uint16_t EEVEE_SPECIES = 133;
            constexpr uint16_t NINCADA_SPECIES = 290;
            constexpr uint16_t SPINDA_SPECIES = 327;
            constexpr uint16_t DIALGA_SPECIES = 483;
            constexpr uint16_t PALKIA_SPECIES = 484;
            constexpr uint16_t ARCEUS_SPECIES = 493;
            constexpr uint16_t AEGISLASH_SPECIES = 681;
            constexpr uint16_t ZYGARDE_SPECIES = 718;
            constexpr uint16_t SILVALLY_SPECIES = 773;
            constexpr uint16_t GIMMIGHOUL_SPECIES = 999;

            // PKHeX ParseSettings defaults: Gen 1 tradeback allowed, and a HOME transfer with no
            // tracker ignored (HOMETransferTrackerNotPresent = Invalid).
            constexpr bool ALLOW_GEN1_TRADEBACK = true;
            constexpr bool IGNORE_TRANSFER_IF_NO_TRACKER = true;

            constexpr EvolutionRuleTweak TWEAK_DEFAULT{false};
            constexpr EvolutionRuleTweak TWEAK_LEVEL_100{true};

            using RevertFormsFunction = void (*)(EvolutionCriteria &stage);
            using EvolutionBanFunction = bool (*)(const EvolutionEntity &entity, SpeciesForm head, SpeciesForm destination);

            SpeciesForm toSpeciesForm(const EvolutionCriteria &stage)
            {
                return SpeciesForm{stage.speciesId, stage.formId};
            }

            const EvolutionTree &treeFor(GameVersion contextGroup)
            {
                return *getEvolutionTree(contextGroup);
            }

            /// PKHeX IEvolutionEnvironment: one game's tree with its rule tweak and evolution ban.
            struct EvolutionEnvironment
            {
                GameVersion contextGroup;
                EvolutionRuleTweak tweak;
                EvolutionBanFunction isEvolutionBanned;

                bool tryDevolve(SpeciesForm head, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, EvolutionCriteria &result) const
                {
                    return treeFor(contextGroup).tryDevolve(head, entity, currentMaxLevel, levelMin, skipChecks, tweak, result);
                }

                bool tryEvolve(SpeciesForm head, SpeciesForm next, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, EvolutionCriteria &result) const
                {
                    const bool didEvolve = treeFor(contextGroup).tryEvolve(head, next, entity, currentMaxLevel, levelMin, skipChecks, tweak, result);
                    return didEvolve && !(isEvolutionBanned && isEvolutionBanned(entity, head, toSpeciesForm(result)));
                }
            };

            // PKHeX EvolutionGroup7.IsEvolutionBanned: the Kanto forms need Ultra Sun/Ultra Moon.
            bool isKantoEvolutionBanned(const EvolutionEntity &entity, SpeciesForm, SpeciesForm destination)
            {
                const bool isPK7 = entity.formatGroup == GameVersion::SM || entity.formatGroup == GameVersion::USUM;
                if (!(isPK7 && isVersionSunMoon(entity) && entity.isUntraded))
                {
                    return false;
                }
                return destination.formId == 0 && (destination.speciesId == RAICHU_SPECIES || destination.speciesId == MAROWAK_SPECIES || destination.speciesId == EXEGGUTOR_SPECIES);
            }

            // PKHeX EvolutionEnvironment8.IsEvolutionBanned: Gigantamax Pikachu, Meowth and Eevee.
            bool isGigantamaxEvolutionBanned(const EvolutionEntity &entity, SpeciesForm head, SpeciesForm)
            {
                const bool isGigantamaxSpecies = head.speciesId == PIKACHU_SPECIES || head.speciesId == MEOWTH_SPECIES || head.speciesId == EEVEE_SPECIES;
                return isGigantamaxSpecies && entity.formatGroup == GameVersion::SWSH && entity.canGigantamax;
            }

            constexpr EvolutionEnvironment ENVIRONMENT_RBY{GameVersion::RBY, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_GSC{GameVersion::GSC, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_RSE{GameVersion::RSE, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_DP{GameVersion::DP, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_BW{GameVersion::BW, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_XY{GameVersion::XY, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_SM{GameVersion::SM, TWEAK_DEFAULT, isKantoEvolutionBanned};
            constexpr EvolutionEnvironment ENVIRONMENT_GG{GameVersion::GG, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_SWSH{GameVersion::SWSH, TWEAK_LEVEL_100, isGigantamaxEvolutionBanned};
            constexpr EvolutionEnvironment ENVIRONMENT_PLA{GameVersion::PLA, TWEAK_DEFAULT, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_BDSP{GameVersion::BDSP, TWEAK_LEVEL_100, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_SV{GameVersion::SV, TWEAK_LEVEL_100, nullptr};
            constexpr EvolutionEnvironment ENVIRONMENT_ZA{GameVersion::ZA, TWEAK_LEVEL_100, nullptr};

            /// The devolve loop every PKHeX group shares.
            int devolveWith(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, const EvolutionEnvironment &environment, RevertFormsFunction revertMutatedForms)
            {
                int presentCount = 1;
                for (size_t stageIndex = 1; stageIndex < result.size(); stageIndex++)
                {
                    EvolutionCriteria &previous = result[stageIndex - 1];
                    if (revertMutatedForms)
                    {
                        revertMutatedForms(previous);
                    }
                    EvolutionCriteria devolved;
                    if (!environment.tryDevolve(toSpeciesForm(previous), entity, previous.levelMax, origin.levelMin, origin.shouldSkipChecks(), devolved))
                    {
                        continue;
                    }
                    if (devolved.isBetterDevolution(result[stageIndex]))
                    {
                        result[stageIndex] = devolved;
                    }
                    presentCount++;
                }
                return presentCount;
            }

            /// The evolve loop every PKHeX group shares.
            int evolveWith(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, const EvolutionEnvironment &environment)
            {
                int presentCount = 1;
                for (int stageIndex = static_cast<int>(result.size()) - 1; stageIndex >= 1; stageIndex--)
                {
                    EvolutionCriteria &destination = result[stageIndex - 1];
                    const EvolutionCriteria devolved = result[stageIndex];
                    EvolutionCriteria evolved;
                    if (!environment.tryEvolve(toSpeciesForm(devolved), toSpeciesForm(destination), entity, origin.levelMax, devolved.levelMin, origin.shouldSkipChecks(), evolved))
                    {
                        if (destination.method == EvolutionType::Invalid)
                        {
                            break;
                        }
                        continue;
                    }
                    if (evolved.isBetterEvolution(destination))
                    {
                        destination = evolved;
                    }
                    presentCount++;
                }
                return presentCount;
            }

            /// The loops of PKHeX EvolutionGroupHOME/HOME2's Multi paths: every adjacent game at once.
            int devolveAcross(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, std::span<const EvolutionEnvironment *const> environments, RevertFormsFunction revertMutatedForms)
            {
                int presentCount = 1;
                for (size_t stageIndex = 1; stageIndex < result.size(); stageIndex++)
                {
                    EvolutionCriteria &previous = result[stageIndex - 1];
                    revertMutatedForms(previous);
                    EvolutionCriteria &reference = result[stageIndex];
                    bool didDevolveAny = false;
                    for (const EvolutionEnvironment *environment : environments)
                    {
                        EvolutionCriteria devolved;
                        if (!environment->tryDevolve(toSpeciesForm(previous), entity, previous.levelMax, origin.levelMin, origin.shouldSkipChecks(), devolved))
                        {
                            continue;
                        }
                        if (devolved.isBetterDevolution(reference))
                        {
                            reference = devolved;
                        }
                        didDevolveAny = true;
                    }
                    if (didDevolveAny)
                    {
                        presentCount++;
                    }
                }
                return presentCount;
            }

            int evolveAcross(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, std::span<const EvolutionEnvironment *const> environments)
            {
                int presentCount = 1;
                for (int stageIndex = static_cast<int>(result.size()) - 1; stageIndex >= 1; stageIndex--)
                {
                    EvolutionCriteria &destination = result[stageIndex - 1];
                    const EvolutionCriteria devolved = result[stageIndex];
                    bool didEvolveAny = false;
                    for (const EvolutionEnvironment *environment : environments)
                    {
                        EvolutionCriteria evolved;
                        if (!environment->tryEvolve(toSpeciesForm(devolved), toSpeciesForm(destination), entity, origin.levelMax, devolved.levelMin, origin.shouldSkipChecks(), evolved))
                        {
                            continue;
                        }
                        if (evolved.isBetterEvolution(destination))
                        {
                            destination = evolved;
                        }
                        didEvolveAny = true;
                    }
                    if (didEvolveAny)
                    {
                        presentCount++;
                    }
                    else if (destination.method == EvolutionType::Invalid)
                    {
                        break;
                    }
                }
                return presentCount;
            }

            /// Discard by the Game Boy table of the origin generation (PKHeX PersonalTable.RB or C).
            void discardGameBoy(std::span<EvolutionCriteria> result, const EvolutionOrigin &origin)
            {
                discard(result, treeFor(origin.generation == 1 ? GameVersion::RBY : GameVersion::GSC));
            }

            // PKHeX EvolutionGroup6.RevertMutatedForms: Fairy Arceus does not exist before Gen 6.
            void revertMutatedForms6(EvolutionCriteria &stage)
            {
                if (stage.speciesId == ARCEUS_SPECIES && stage.formId == 17)
                {
                    stage.formId = 0;
                }
            }

            // PKHeX EvolutionGroup7.RevertMutatedForms: Zygarde's cube forms, Silvally's memories.
            void revertMutatedForms7(EvolutionCriteria &stage)
            {
                constexpr uint8_t zygarde50PercentLevel = 63;
                if (stage.speciesId == ZYGARDE_SPECIES && stage.formId != 0 && stage.formId != 1)
                {
                    stage.formId = stage.levelMax == zygarde50PercentLevel ? 1 : 0;
                }
                else if (stage.speciesId == SILVALLY_SPECIES && stage.formId != 0)
                {
                    stage.formId = 0;
                }
            }

            // PKHeX EvolutionGroupHOME.RevertMutatedForms
            void revertMutatedFormsHome(EvolutionCriteria &stage)
            {
                const bool revertsToNormal = stage.speciesId == DIALGA_SPECIES || stage.speciesId == PALKIA_SPECIES || stage.speciesId == ARCEUS_SPECIES || stage.speciesId == SILVALLY_SPECIES;
                if (revertsToNormal && stage.formId != 0)
                {
                    stage.formId = 0;
                }
            }

            // PKHeX FormInfo.GetOutOfBattleForm at the latest generation.
            uint8_t getOutOfBattleForm(uint16_t speciesId, uint8_t formId)
            {
                constexpr uint16_t darmanitanSpecies = 555;
                constexpr uint16_t greninjaSpecies = 658;
                constexpr uint16_t floetteSpecies = 670;
                constexpr uint16_t meowsticSpecies = 678;
                constexpr uint16_t miniorSpecies = 774;
                constexpr uint16_t mimikyuSpecies = 778;
                constexpr uint16_t magearnaSpecies = 801;
                constexpr uint16_t tatsugiriSpecies = 978;
                constexpr uint16_t ogerponSpecies = 1017;
                constexpr uint8_t floetteEternalForm = 5;
                constexpr uint8_t zygardeCompleteBaseForm = 3;
                switch (speciesId)
                {
                case darmanitanSpecies:
                case mimikyuSpecies:
                    return static_cast<uint8_t>(formId & 2);
                case greninjaSpecies:
                    return formId == 2 ? 1 : 0;
                case ZYGARDE_SPECIES:
                    return zygardeCompleteBaseForm;
                case miniorSpecies:
                    return static_cast<uint8_t>(formId + 7);
                case ogerponSpecies:
                    return static_cast<uint8_t>(formId & 3);
                case floetteSpecies:
                    return floetteEternalForm;
                case tatsugiriSpecies:
                    return static_cast<uint8_t>(formId - 3);
                case magearnaSpecies:
                case meowsticSpecies:
                    return static_cast<uint8_t>(formId - 2);
                default:
                    return 0;
                }
            }

            // PKHeX EvolutionGroupHOME2.RevertMutatedForms: battle-only forms revert out of battle.
            void revertMutatedFormsHome2(EvolutionCriteria &stage)
            {
                if (stage.formId == 0)
                {
                    return;
                }
                const bool isBattleOnly = Pokemon::isMegaForm(stage.speciesId, stage.formId) || Pokemon::isBattleForm(stage.speciesId, stage.formId);
                if (isBattleOnly)
                {
                    stage.formId = getOutOfBattleForm(stage.speciesId, stage.formId);
                }
            }

            const EvolutionGroup &group1();
            const EvolutionGroup &group2();
            const EvolutionGroup &group3();
            const EvolutionGroup &group4();
            const EvolutionGroup &group5();
            const EvolutionGroup &group6();
            const EvolutionGroup &group7();
            const EvolutionGroup &group7b();
            const EvolutionGroup &groupHome();
            const EvolutionGroup &groupHome2();

            // PKHeX EvolutionGroup1
            class EvolutionGroup1 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { return &group2(); }

                const EvolutionGroup *getPrevious(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override
                {
                    return entity.formatGeneration == 1 && ALLOW_GEN1_TRADEBACK ? &group2() : nullptr;
                }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override
                {
                    if (!ALLOW_GEN1_TRADEBACK)
                    {
                        return;
                    }
                    discardGameBoy(result, origin);
                }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (entity.formatGeneration >= 7 && !origin.shouldSkipChecks())
                    {
                        origin.levelMin = 2;
                        origin.levelMax = entity.metLevel;
                    }
                    return devolveWith(result, entity, origin, ENVIRONMENT_RBY, nullptr);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (entity.formatGeneration > 2)
                    {
                        origin.levelMax = entity.metLevel;
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_RBY);
                    history.setChain(GameVersion::RBY, setHistory(result, treeFor(GameVersion::RBY)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup2
            class EvolutionGroup2 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 2 ? &group7() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration != 1 ? &group1() : nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override { discardGameBoy(result, origin); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (entity.formatGeneration > 2 && !origin.shouldSkipChecks())
                    {
                        updateCeiling(result, entity.metLevel);
                        origin.levelMin = 2;
                        origin.levelMax = entity.metLevel;
                    }
                    return devolveWith(result, entity, origin, ENVIRONMENT_GSC, nullptr);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (entity.formatGeneration > 2)
                    {
                        origin.levelMax = entity.metLevel;
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_GSC);
                    history.setChain(GameVersion::GSC, setHistory(result, treeFor(GameVersion::GSC)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup3
            class EvolutionGroup3 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override
                {
                    if (isVersionVirtualConsoleGen3(entity))
                    {
                        return &groupHome();
                    }
                    return entity.formatGeneration > 3 ? &group4() : nullptr;
                }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { return nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::RSE)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (entity.formatGeneration == 4 && !origin.shouldSkipChecks())
                    {
                        updateCeiling(result, entity.metLevel);
                        origin.levelMin = 1;
                        origin.levelMax = entity.metLevel;
                    }
                    return devolveWith(result, entity, origin, ENVIRONMENT_RSE, nullptr);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (entity.formatGeneration > 3)
                    {
                        origin.levelMax = entity.metLevel;
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_RSE);
                    history.setChain(GameVersion::RSE, setHistory(result, treeFor(GameVersion::RSE)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup4
            class EvolutionGroup4 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 4 ? &group5() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override { return origin.generation == 3 ? &group3() : nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::DP)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (entity.formatGeneration > 4 && !origin.shouldSkipChecks())
                    {
                        if (origin.speciesId == ARCEUS_SPECIES)
                        {
                            result[0].formId = 0;
                        }
                        updateCeiling(result, entity.metLevel);
                        origin.levelMin = 1;
                        origin.levelMax = entity.metLevel;
                    }
                    return devolveWith(result, entity, origin, ENVIRONMENT_DP, nullptr);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (entity.formatGeneration > 4)
                    {
                        origin.levelMax = entity.metLevel;
                    }
                    else if (origin.generation < 4)
                    {
                        updateFloor(result, entity.metLevel, origin.levelMax);
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_DP);
                    history.setChain(GameVersion::DP, setHistory(result, treeFor(GameVersion::DP)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup5
            class EvolutionGroup5 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 5 ? &group6() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override { return origin.generation < 5 ? &group4() : nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::BW)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override { return devolveWith(result, entity, origin, ENVIRONMENT_BW, nullptr); }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (origin.generation < 5)
                    {
                        updateFloor(result, entity.metLevel, origin.levelMax);
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_BW);
                    history.setChain(GameVersion::BW, setHistory(result, treeFor(GameVersion::BW)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup6
            class EvolutionGroup6 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 6 ? &group7() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override { return origin.generation < 6 ? &group5() : nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::XY)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override { return devolveWith(result, entity, origin, ENVIRONMENT_XY, revertMutatedForms6); }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_XY);
                    history.setChain(GameVersion::XY, setHistory(result, treeFor(GameVersion::XY)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup7
            class EvolutionGroup7 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 7 ? &groupHome() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override
                {
                    if (origin.generation == 1 || origin.generation == 2)
                    {
                        return &group2();
                    }
                    if (origin.generation < 7)
                    {
                        return &group6();
                    }
                    return nullptr;
                }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::SM)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override { return devolveWith(result, entity, origin, ENVIRONMENT_SM, revertMutatedForms7); }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (origin.generation <= 2)
                    {
                        updateFloor(result, entity.metLevel, origin.levelMax);
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_SM);
                    history.setChain(GameVersion::SM, setHistory(result, treeFor(GameVersion::SM)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroup7b
            class EvolutionGroup7b final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override { return entity.formatGeneration > 7 ? &groupHome() : nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { return nullptr; }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { discard(result, treeFor(GameVersion::GG)); }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override { return devolveWith(result, entity, origin, ENVIRONMENT_GG, nullptr); }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_GG);
                    history.setChain(GameVersion::GG, setHistory(result, treeFor(GameVersion::GG)));
                    return presentCount;
                }
            };

            // PKHeX EvolutionGroupHOME.IsUnavailableEvoChain: the split Alolan evolutions need Gen 7.
            bool isUnavailableEvolutionChain(uint16_t speciesId, uint8_t formId)
            {
                return formId == 1 && (speciesId == RAICHU_SPECIES || speciesId == EXEGGUTOR_SPECIES || speciesId == MAROWAK_SPECIES);
            }

            constexpr std::array<const EvolutionEnvironment *, 4> HOME_ENVIRONMENTS = {&ENVIRONMENT_SWSH, &ENVIRONMENT_PLA, &ENVIRONMENT_BDSP, &ENVIRONMENT_SV};
            constexpr std::array<const EvolutionEnvironment *, 1> HOME2_ENVIRONMENTS = {&ENVIRONMENT_ZA};

            // PKHeX EvolutionGroupHOME
            class EvolutionGroupHome final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override
                {
                    if (entity.formatGeneration <= 9 && getEntityContextGroup(entity) != GameVersion::ZA)
                    {
                        return nullptr;
                    }
                    return &groupHome2();
                }

                const EvolutionGroup *getPrevious(const EvolutionEntity &entity, const EvolutionOrigin &origin) const noexcept override
                {
                    if (origin.generation >= 8)
                    {
                        return nullptr;
                    }
                    if (origin.contextGroup == GameVersion::GG)
                    {
                        return &group7b();
                    }
                    if (isVersionVirtualConsoleGen3(entity))
                    {
                        return &group3();
                    }
                    return &group7();
                }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &) const noexcept override
                {
                    if (isVersionScarletViolet(entity))
                    {
                        discard(result, treeFor(GameVersion::SV));
                    }
                    else if (isVersionLegendsArceus(entity))
                    {
                        discard(result, treeFor(GameVersion::PLA));
                    }
                    else if (isVersionBrilliantDiamondShiningPearl(entity))
                    {
                        discard(result, treeFor(GameVersion::BDSP));
                    }
                    else if (isVersionSwordShield(entity))
                    {
                        discard(result, treeFor(GameVersion::SWSH));
                    }
                    else if (isVersionPokemonGo(entity) && result.size() >= 2 && result[1].speciesId == GIMMIGHOUL_SPECIES)
                    {
                        result[1].formId = 1;
                    }
                }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (shouldCheckAllAdjacent(entity, origin))
                    {
                        return devolveAcross(result, entity, origin, HOME_ENVIRONMENTS, revertMutatedFormsHome);
                    }
                    return devolveWith(result, entity, origin, getSingleEnvironment(entity), revertMutatedFormsHome);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (isUnavailableEvolutionChain(entity.speciesId, entity.formId))
                    {
                        result = result.first(1);
                    }
                    if (!shouldCheckAllAdjacent(entity, origin))
                    {
                        const EvolutionEnvironment &environment = getSingleEnvironment(entity);
                        const int presentCount = evolveWith(result, entity, origin, environment);
                        const GameVersion recordContext = getSingleRecordContext(entity);
                        if (recordContext != GameVersion::Invalid)
                        {
                            history.setChain(recordContext, setHistory(result, treeFor(recordContext)));
                        }
                        return presentCount;
                    }

                    const int presentCount = evolveAcross(result, entity, origin, HOME_ENVIRONMENTS);
                    const bool isBdspOnlySpecies = entity.speciesId == NINCADA_SPECIES || entity.speciesId == SPINDA_SPECIES;
                    if (isBdspOnlySpecies && isVersionBrilliantDiamondShiningPearl(entity))
                    {
                        history.setChain(GameVersion::BDSP, setHistory(result, treeFor(GameVersion::BDSP)));
                        return presentCount;
                    }
                    history.setChain(GameVersion::SV, setHistory(result, treeFor(GameVersion::SV)));
                    history.setChain(GameVersion::SWSH, setHistory(result, treeFor(GameVersion::SWSH)));
                    if (entity.speciesId == RAICHU_SPECIES && entity.formId == 1 && history.hasVisited(GameVersion::SM))
                    {
                        return presentCount;
                    }
                    history.setChain(GameVersion::PLA, setHistory(result, treeFor(GameVersion::PLA)));
                    if (isBdspOnlySpecies)
                    {
                        return presentCount;
                    }
                    history.setChain(GameVersion::BDSP, setHistory(result, treeFor(GameVersion::BDSP)));
                    return presentCount;
                }

            private:
                static bool shouldCheckAllAdjacent(const EvolutionEntity &entity, const EvolutionOrigin &origin)
                {
                    if (origin.shouldSkipChecks())
                    {
                        return true;
                    }
                    const GameVersion entityContext = getEntityContextGroup(entity);
                    const bool isOutsideContext = !(entityContext == GameVersion::SWSH || entityContext == GameVersion::PLA || entityContext == GameVersion::BDSP || entityContext == GameVersion::SV);
                    if (isOutsideContext)
                    {
                        return true;
                    }
                    return (entity.hasHomeTrackerField && entity.hasHomeTracker) || !IGNORE_TRANSFER_IF_NO_TRACKER;
                }

                // PKHeX EvolutionGroupHOME.GetSingleEnv
                static const EvolutionEnvironment &getSingleEnvironment(const EvolutionEntity &entity)
                {
                    switch (entity.formatGroup)
                    {
                    case GameVersion::SWSH:
                        return ENVIRONMENT_SWSH;
                    case GameVersion::PLA:
                        return ENVIRONMENT_PLA;
                    case GameVersion::BDSP:
                        return ENVIRONMENT_BDSP;
                    case GameVersion::SV:
                        return ENVIRONMENT_SV;
                    default:
                        break;
                    }
                    if (isVersionLegendsArceus(entity))
                    {
                        return ENVIRONMENT_PLA;
                    }
                    if (isVersionBrilliantDiamondShiningPearl(entity))
                    {
                        return ENVIRONMENT_BDSP;
                    }
                    if (isVersionScarletViolet(entity))
                    {
                        return ENVIRONMENT_SV;
                    }
                    return ENVIRONMENT_SWSH;
                }

                static GameVersion getSingleRecordContext(const EvolutionEntity &entity)
                {
                    switch (entity.formatGroup)
                    {
                    case GameVersion::SWSH:
                    case GameVersion::PLA:
                    case GameVersion::BDSP:
                    case GameVersion::SV:
                        return entity.formatGroup;
                    default:
                        return GameVersion::Invalid;
                    }
                }
            };

            // PKHeX EvolutionGroupHOME2
            class EvolutionGroupHome2 final : public EvolutionGroup
            {
            public:
                const EvolutionGroup *getNext(const EvolutionEntity &, const EvolutionOrigin &) const noexcept override { return nullptr; }

                const EvolutionGroup *getPrevious(const EvolutionEntity &, const EvolutionOrigin &origin) const noexcept override
                {
                    if (origin.generation > 9 || origin.contextGroup == GameVersion::ZA)
                    {
                        return nullptr;
                    }
                    return &groupHome();
                }

                void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin) const noexcept override
                {
                    if (!isVersionLegendsZA(entity))
                    {
                        const EvolutionGroup *previous = getPrevious(entity, origin);
                        if (previous)
                        {
                            previous->discardForOrigin(result, entity, origin);
                        }
                        return;
                    }
                    const EvolutionTree &tree = treeFor(GameVersion::ZA);
                    if (origin.shouldSkipChecks())
                    {
                        discard(result, tree);
                        return;
                    }
                    // A form change can realign the ability, and HOME realigns it when it tracks the
                    // Pokemon; otherwise a stage whose ability slot disagrees cannot be the origin.
                    const int abilityIndex = entity.abilityNumber >> 1;
                    if ((abilityIndex == 0 || abilityIndex == 1) && !(entity.hasHomeTrackerField && entity.hasHomeTracker))
                    {
                        bool didRealignAbility = false;
                        const uint16_t speciesId = origin.speciesId;
                        if (Pokemon::hasMegaForm(speciesId) || speciesId == AEGISLASH_SPECIES)
                        {
                            const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(GameVersion::ZA, speciesId, entity.formId);
                            const uint16_t slotAbility = abilityIndex == 0 ? record.ability1 : record.ability2;
                            didRealignAbility = slotAbility == entity.abilityId;
                        }
                        if (!didRealignAbility)
                        {
                            discardByAbility(result, tree, GameVersion::ZA, entity.abilityId, abilityIndex);
                            return;
                        }
                    }
                    discard(result, tree);
                }

                int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept override
                {
                    if (shouldCheckAllAdjacent(entity, origin))
                    {
                        return devolveAcross(result, entity, origin, HOME2_ENVIRONMENTS, revertMutatedFormsHome2);
                    }
                    return devolveWith(result, entity, origin, ENVIRONMENT_ZA, revertMutatedFormsHome2);
                }

                int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept override
                {
                    if (isUnavailableEvolutionChain(entity.speciesId, entity.formId))
                    {
                        result = result.first(1);
                    }
                    if (shouldCheckAllAdjacent(entity, origin))
                    {
                        const int presentCount = evolveAcross(result, entity, origin, HOME2_ENVIRONMENTS);
                        history.setChain(GameVersion::ZA, setHistory(result, treeFor(GameVersion::ZA)));
                        return presentCount;
                    }
                    const int presentCount = evolveWith(result, entity, origin, ENVIRONMENT_ZA);
                    if (entity.formatGroup == GameVersion::ZA)
                    {
                        history.setChain(GameVersion::ZA, setHistory(result, treeFor(GameVersion::ZA)));
                    }
                    return presentCount;
                }

            private:
                static bool shouldCheckAllAdjacent(const EvolutionEntity &entity, const EvolutionOrigin &origin)
                {
                    return origin.shouldSkipChecks() || (entity.hasHomeTrackerField && entity.hasHomeTracker) || !IGNORE_TRANSFER_IF_NO_TRACKER;
                }
            };

            const EvolutionGroup1 GROUP_1;
            const EvolutionGroup2 GROUP_2;
            const EvolutionGroup3 GROUP_3;
            const EvolutionGroup4 GROUP_4;
            const EvolutionGroup5 GROUP_5;
            const EvolutionGroup6 GROUP_6;
            const EvolutionGroup7 GROUP_7;
            const EvolutionGroup7b GROUP_7B;
            const EvolutionGroupHome GROUP_HOME;
            const EvolutionGroupHome2 GROUP_HOME2;

            const EvolutionGroup &group1() { return GROUP_1; }
            const EvolutionGroup &group2() { return GROUP_2; }
            const EvolutionGroup &group3() { return GROUP_3; }
            const EvolutionGroup &group4() { return GROUP_4; }
            const EvolutionGroup &group5() { return GROUP_5; }
            const EvolutionGroup &group6() { return GROUP_6; }
            const EvolutionGroup &group7() { return GROUP_7; }
            const EvolutionGroup &group7b() { return GROUP_7B; }
            const EvolutionGroup &groupHome() { return GROUP_HOME; }
            const EvolutionGroup &groupHome2() { return GROUP_HOME2; }
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/EvolutionGroup/EvolutionGroupUtil.cs
        const EvolutionGroup *getEvolutionGroup(Enums::GameVersion contextGroup) noexcept
        {
            switch (getEvolutionContextGroup(contextGroup))
            {
            case GameVersion::RBY:
                return &GROUP_1;
            case GameVersion::GSC:
                return &GROUP_2;
            case GameVersion::RSE:
                return &GROUP_3;
            case GameVersion::DP:
                return &GROUP_4;
            case GameVersion::BW:
                return &GROUP_5;
            case GameVersion::XY:
                return &GROUP_6;
            case GameVersion::SM:
                return &GROUP_7;
            case GameVersion::GG:
                return &GROUP_7B;
            case GameVersion::SWSH:
            case GameVersion::PLA:
            case GameVersion::BDSP:
            case GameVersion::SV:
                return &GROUP_HOME;
            case GameVersion::ZA:
                return &GROUP_HOME2;
            default:
                return nullptr;
            }
        }
    }
}
