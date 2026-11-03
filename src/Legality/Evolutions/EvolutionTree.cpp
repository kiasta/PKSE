#include "Legality/Evolutions/EvolutionTree.h"

#include <algorithm>

#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionTreeTable.h"
#include "Legality/RNG/ToxtricityUtil.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            constexpr uint8_t MAX_LEVEL = 100;

            // PKHeX EvolutionRestrictions.IsEvolvedSpeciesFormRare
            bool isEvolvedSpeciesFormRare(uint32_t encryptionConstant)
            {
                return encryptionConstant % 100 == 0;
            }

            EvolutionCheckResult skippable(bool skipChecks, EvolutionCheckResult failure)
            {
                return skipChecks ? EvolutionCheckResult::Valid : failure;
            }

            // PKHeX EvolutionReversal.GetLevelUp / EvolutionForwardPersonal.GetLevelUp
            uint8_t getLevelUp(uint8_t levelUpFlag, uint8_t currentMaxLevel, const EvolutionRuleTweak &tweak)
            {
                if (levelUpFlag == 0)
                {
                    return 0;
                }
                if (currentMaxLevel == MAX_LEVEL && tweak.allowLevelUpEvolution100)
                {
                    return 0;
                }
                return levelUpFlag;
            }

            template <typename EntryType>
            const EntryType *findEntry(std::span<const EntryType> entries, uint16_t speciesId, uint8_t formId)
            {
                const auto position = std::lower_bound(entries.begin(), entries.end(), speciesId, [formId](const EntryType &entry, uint16_t searchedSpeciesId)
                {
                    return entry.speciesId < searchedSpeciesId || (entry.speciesId == searchedSpeciesId && entry.formId < formId);
                });
                if (position == entries.end() || position->speciesId != speciesId || position->formId != formId)
                {
                    return nullptr;
                }
                return &*position;
            }

            int appendSpeciesForm(std::span<SpeciesForm> result, int resultCount, SpeciesForm speciesForm)
            {
                if (resultCount < static_cast<int>(result.size()))
                {
                    result[resultCount] = speciesForm;
                }
                return resultCount + 1;
            }

            const EvolutionTree TREE_RBY(EVOLUTION_TREE_RBY);
            const EvolutionTree TREE_GSC(EVOLUTION_TREE_GSC);
            const EvolutionTree TREE_RSE(EVOLUTION_TREE_RSE);
            const EvolutionTree TREE_DP(EVOLUTION_TREE_DP);
            const EvolutionTree TREE_BW(EVOLUTION_TREE_BW);
            const EvolutionTree TREE_XY(EVOLUTION_TREE_XY);
            const EvolutionTree TREE_SM(EVOLUTION_TREE_SM);
            const EvolutionTree TREE_SWSH(EVOLUTION_TREE_SWSH);
            const EvolutionTree TREE_SV(EVOLUTION_TREE_SV);
            const EvolutionTree TREE_GG(EVOLUTION_TREE_GG);
            const EvolutionTree TREE_PLA(EVOLUTION_TREE_PLA);
            const EvolutionTree TREE_BDSP(EVOLUTION_TREE_BDSP);
            const EvolutionTree TREE_ZA(EVOLUTION_TREE_ZA);

            constexpr std::array<const EvolutionTree *, EVOLUTION_CONTEXT_COUNT> TREES = {&TREE_RBY, &TREE_GSC, &TREE_RSE, &TREE_DP, &TREE_BW, &TREE_XY, &TREE_SM, &TREE_SWSH, &TREE_SV, &TREE_GG, &TREE_PLA, &TREE_BDSP, &TREE_ZA};
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/Methods/EvolutionMethod.cs
        EvolutionCheckResult EvolutionMethod::check(const EvolutionEntity &entity, uint8_t currentLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak) const noexcept
        {
            if (!isLevelUpRequired(method))
            {
                switch (method)
                {
                case EvolutionType::UseItemMale:
                case EvolutionType::LevelUpRecoilDamageMale:
                    return entity.gender == 0 ? EvolutionCheckResult::Valid : EvolutionCheckResult::BadGender;
                case EvolutionType::UseItemFemale:
                case EvolutionType::LevelUpRecoilDamageFemale:
                    return entity.gender == 1 ? EvolutionCheckResult::Valid : EvolutionCheckResult::BadGender;
                case EvolutionType::Trade:
                case EvolutionType::TradeHeldItem:
                case EvolutionType::TradeShelmetKarrablast:
                    return !entity.isUntraded || skipChecks ? EvolutionCheckResult::Valid : EvolutionCheckResult::Untraded;
                default:
                    return EvolutionCheckResult::Valid;
                }
            }

            EvolutionCheckResult secondaryResult = EvolutionCheckResult::Valid;
            switch (method)
            {
            case EvolutionType::LevelUpMale:
                if (entity.gender != 0)
                {
                    secondaryResult = EvolutionCheckResult::BadGender;
                }
                break;
            case EvolutionType::LevelUpFemale:
                if (entity.gender != 1)
                {
                    secondaryResult = EvolutionCheckResult::BadGender;
                }
                break;
            case EvolutionType::LevelUpFormFemale1:
                if (entity.gender != 1)
                {
                    secondaryResult = EvolutionCheckResult::BadGender;
                }
                else if (entity.formId != 1)
                {
                    secondaryResult = EvolutionCheckResult::BadForm;
                }
                break;
            case EvolutionType::LevelUpBeauty:
                if (entity.hasContestStats && entity.contestBeauty < argument)
                {
                    secondaryResult = skippable(skipChecks, EvolutionCheckResult::LowContestStat);
                }
                break;
            case EvolutionType::LevelUpNatureAmped:
            case EvolutionType::LevelUpNatureLowKey:
                if (RNG::ToxtricityUtil::getAmpLowKeyResult(entity.natureId) != entity.formId)
                {
                    secondaryResult = skippable(skipChecks, EvolutionCheckResult::BadForm);
                }
                break;
            case EvolutionType::LevelUpVersion:
            case EvolutionType::LevelUpVersionDay:
            case EvolutionType::LevelUpVersionNight:
                if ((entity.originVersion & 1) != (argument & 1) && entity.isUntraded)
                {
                    secondaryResult = skippable(skipChecks, EvolutionCheckResult::VisitVersion);
                }
                break;
            case EvolutionType::LevelUpKnowMoveEC100:
            case EvolutionType::LevelUpInBattleEC100:
                if (!isEvolvedSpeciesFormRare(entity.encryptionConstant))
                {
                    secondaryResult = skippable(skipChecks, EvolutionCheckResult::WrongEC);
                }
                break;
            case EvolutionType::LevelUpKnowMoveECElse:
            case EvolutionType::LevelUpInBattleECElse:
                if (isEvolvedSpeciesFormRare(entity.encryptionConstant))
                {
                    secondaryResult = skippable(skipChecks, EvolutionCheckResult::WrongEC);
                }
                break;
            default:
                break;
            }
            if (secondaryResult != EvolutionCheckResult::Valid)
            {
                return secondaryResult;
            }

            if (currentLevel < level)
            {
                return EvolutionCheckResult::InsufficientLevel;
            }
            if (levelUp == 0)
            {
                return EvolutionCheckResult::Valid;
            }
            if (level == 0 && currentLevel < 2)
            {
                return EvolutionCheckResult::InsufficientLevel;
            }
            if (currentLevel < levelMin + levelUp && !skipChecks)
            {
                if (currentLevel == MAX_LEVEL && tweak.allowLevelUpEvolution100)
                {
                    return EvolutionCheckResult::Valid;
                }
                return EvolutionCheckResult::InsufficientLevel;
            }
            return EvolutionCheckResult::Valid;
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/Reversal/EvolutionReversal.cs TryDevolve
        bool EvolutionNode::tryDevolve(const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept
        {
            for (const EvolutionLink *link : {&first, &second})
            {
                if (link->isEmpty())
                {
                    return false;
                }
                if (link->method->check(entity, currentMaxLevel, levelMin, skipChecks, tweak) == EvolutionCheckResult::Valid)
                {
                    result = EvolutionCriteria{link->sourceSpeciesId, link->sourceFormId, getLevelUp(link->method->levelUp, currentMaxLevel, tweak), currentMaxLevel, link->method->level, link->method->method};
                    return true;
                }
            }
            return false;
        }

        // PKHeX Forward/EvolutionForwardSpecies.cs and EvolutionForwardPersonal.cs GetForward
        std::span<const EvolutionMethod> EvolutionTree::getForward(uint16_t speciesId, uint8_t formId) const noexcept
        {
            const EvolutionForwardEntry *entry = nullptr;
            if (treeData.isSpeciesIndexed)
            {
                entry = findEntry(treeData.forwardEntries, speciesId, 0);
            }
            else if (speciesId <= treeData.maxSpeciesId)
            {
                entry = findEntry(treeData.forwardEntries, speciesId, formId);
                if (!entry && formId != 0)
                {
                    entry = findEntry(treeData.forwardEntries, speciesId, 0);
                }
            }
            if (!entry) return {};
            return treeData.methods.subspan(entry->methodIndex, entry->methodCount);
        }

        // PKHeX Reversal/EvolutionReverseLookup.cs indexer
        EvolutionNode EvolutionTree::getReverse(uint16_t speciesId, uint8_t formId) const noexcept
        {
            EvolutionNode node;
            if (speciesId > treeData.maxSpeciesId) return node;
            const EvolutionReverseEntry *entry = findEntry(treeData.reverseEntries, speciesId, formId);
            if (!entry) return node;
            node.first = EvolutionLink{&treeData.methods[entry->firstMethodIndex], entry->firstSourceSpeciesId, entry->firstSourceFormId};
            if (entry->secondSourceSpeciesId != 0)
            {
                node.second = EvolutionLink{&treeData.methods[entry->secondMethodIndex], entry->secondSourceSpeciesId, entry->secondSourceFormId};
            }
            return node;
        }

        // PKHeX EvolutionReverseSpecies / EvolutionReversePersonal.GetPreEvolutions
        int EvolutionTree::getPreEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept
        {
            SpeciesForm reversedChain[MAX_EVOLUTION_STAGES + 1];
            int chainLength = 0;
            SpeciesForm current{speciesId, formId};
            while (chainLength <= MAX_EVOLUTION_STAGES)
            {
                const EvolutionNode node = getReverse(current.speciesId, current.formId);
                if (node.first.sourceSpeciesId == 0)
                {
                    break;
                }
                current = SpeciesForm{node.first.sourceSpeciesId, node.first.sourceFormId};
                reversedChain[chainLength++] = current;
            }
            int resultCount = 0;
            for (int chainIndex = chainLength - 1; chainIndex >= 0; chainIndex--)
            {
                resultCount = appendSpeciesForm(result, resultCount, reversedChain[chainIndex]);
            }
            return std::min(resultCount, static_cast<int>(result.size()));
        }

        // PKHeX EvolutionForwardSpecies / EvolutionForwardPersonal.GetEvolutions
        int EvolutionTree::getEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept
        {
            int resultCount = 0;
            for (const EvolutionMethod &method : getForward(speciesId, formId))
            {
                if (method.destinationSpeciesId == 0)
                {
                    continue;
                }
                const SpeciesForm evolution{method.destinationSpeciesId, method.getDestinationForm(formId)};
                resultCount = appendSpeciesForm(result, resultCount, evolution);
                if (resultCount >= static_cast<int>(result.size()))
                {
                    break;
                }
                resultCount += getEvolutions(evolution.speciesId, evolution.formId, result.subspan(resultCount));
            }
            return std::min(resultCount, static_cast<int>(result.size()));
        }

        // PKHeX IEvolutionNetwork.cs EvolutionNetwork.GetEvolutionsAndPreEvolutions
        int EvolutionTree::getEvolutionsAndPreEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept
        {
            int resultCount = getPreEvolutions(speciesId, formId, result);
            resultCount = std::min(appendSpeciesForm(result, resultCount, SpeciesForm{speciesId, formId}), static_cast<int>(result.size()));
            resultCount += getEvolutions(speciesId, formId, result.subspan(resultCount));
            return resultCount;
        }

        bool EvolutionTree::isSpeciesDerivedFrom(uint16_t speciesId, uint8_t formId, uint16_t otherSpeciesId, uint8_t otherFormId, bool ignoreForm) const noexcept
        {
            SpeciesForm relatives[32];
            const int relativeCount = getEvolutionsAndPreEvolutions(speciesId, formId, relatives);
            for (int relativeIndex = 0; relativeIndex < relativeCount; relativeIndex++)
            {
                if (relatives[relativeIndex].speciesId != otherSpeciesId)
                {
                    continue;
                }
                return ignoreForm || relatives[relativeIndex].formId == otherFormId;
            }
            return false;
        }

        SpeciesForm EvolutionTree::getBaseSpeciesForm(uint16_t speciesId, uint8_t formId) const noexcept
        {
            SpeciesForm preEvolutions[MAX_EVOLUTION_STAGES];
            if (getPreEvolutions(speciesId, formId, preEvolutions) > 0)
            {
                return preEvolutions[0];
            }
            return SpeciesForm{speciesId, formId};
        }

        // PKHeX Forward/EvolutionForwardPersonal.cs and EvolutionForwardSpecies.cs TryEvolve
        bool EvolutionTree::tryEvolve(SpeciesForm head, SpeciesForm next, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept
        {
            for (const EvolutionMethod &method : getForward(head.speciesId, head.formId))
            {
                if (method.destinationSpeciesId != next.speciesId)
                {
                    continue;
                }
                if (next.formId != method.getDestinationForm(head.formId))
                {
                    continue;
                }
                if (method.check(entity, currentMaxLevel, levelMin, skipChecks, tweak) != EvolutionCheckResult::Valid)
                {
                    continue;
                }
                const uint8_t levelUpRequired = treeData.isSpeciesIndexed ? method.levelUp : getLevelUp(method.levelUp, currentMaxLevel, tweak);
                const uint8_t stageLevelMin = std::max(static_cast<uint8_t>(levelMin + method.levelUp), method.level);
                result = EvolutionCriteria{next.speciesId, next.formId, levelUpRequired, currentMaxLevel, stageLevelMin, method.method};
                return true;
            }
            return false;
        }

        bool EvolutionTree::tryDevolve(SpeciesForm head, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept
        {
            return getReverse(head.speciesId, head.formId).tryDevolve(entity, currentMaxLevel, levelMin, skipChecks, tweak, result);
        }

        bool EvolutionTree::isPresentInGame(uint16_t speciesId, uint8_t formId) const noexcept
        {
            if (speciesId >= treeData.presenceMasks.size() || formId >= 32)
            {
                return false;
            }
            return ((treeData.presenceMasks[speciesId] >> formId) & 1u) != 0;
        }

        const EvolutionTree *getEvolutionTree(Enums::GameVersion contextGroup) noexcept
        {
            const int contextIndex = getEvolutionContextIndex(contextGroup);
            return contextIndex < 0 ? nullptr : TREES[contextIndex];
        }
    }
}
