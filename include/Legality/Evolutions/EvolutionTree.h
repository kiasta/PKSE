/**
 * One context's evolution data, looked up forward (what a species-form evolves into) and in reverse
 * (what it evolved from), with each method's conditions checked against a Pokemon. Ported from
 * PKHeX PKHeX.Core/Legality/Evolutions: EvolutionTree.cs, IEvolutionNetwork.cs, EvolutionRuleTweak.cs,
 * Methods/EvolutionMethod.cs, Forward/ and Reversal/.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONTREE_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONTREE_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Evolutions/EvolutionType.h"

namespace Legality
{
    namespace Evolutions
    {
        /// PKHeX EvolutionMethod.AnyForm: the evolution keeps the form it had.
        constexpr uint8_t EVOLUTION_ANY_FORM = 0xFF;

        /// PKHeX EvolutionCheckResult.
        enum class EvolutionCheckResult : uint8_t
        {
            Valid,
            InsufficientLevel,
            BadGender,
            BadForm,
            WrongEC,
            VisitVersion,
            LowContestStat,
            Untraded,
        };

        /// PKHeX EvolutionRuleTweak.
        struct EvolutionRuleTweak
        {
            /// Sword/Shield onward: a level 100 Pokemon can still trigger a level-up evolution.
            bool allowLevelUpEvolution100 = false;
        };

        /// One way to evolve, as the evolution binary stores it (PKHeX EvolutionMethod).
        struct EvolutionMethod
        {
            uint16_t destinationSpeciesId = 0;
            uint16_t argument = 0;
            /// EVOLUTION_ANY_FORM when the form carries over.
            uint8_t destinationFormId = 0;
            EvolutionType method = EvolutionType::None;
            uint8_t level = 0;
            /// Levels gained to trigger it: 0 when not a level-up method, or in a game where it
            /// triggers on demand (Legends: Arceus, Legends: Z-A).
            uint8_t levelUp = 0;

            uint8_t getDestinationForm(uint8_t formId) const noexcept { return destinationFormId == EVOLUTION_ANY_FORM ? formId : destinationFormId; }

            /// PKHeX EvolutionMethod.Check.
            EvolutionCheckResult check(const EvolutionEntity &entity, uint8_t level, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak) const noexcept;
        };

        /// A reverse edge: `method` evolves (sourceSpeciesId, sourceFormId) into the node's species.
        struct EvolutionLink
        {
            const EvolutionMethod *method = nullptr;
            uint16_t sourceSpeciesId = 0;
            uint8_t sourceFormId = 0;

            bool isEmpty() const noexcept { return sourceSpeciesId == 0; }
        };

        /// PKHeX EvolutionNode: at most two ways into one species-form, the less restrictive first.
        struct EvolutionNode
        {
            EvolutionLink first;
            EvolutionLink second;

            /// PKHeX EvolutionReversal.TryDevolve.
            bool tryDevolve(const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept;
        };

        struct SpeciesForm
        {
            uint16_t speciesId = 0;
            uint8_t formId = 0;

            bool operator==(const SpeciesForm &other) const noexcept = default;
        };

        struct EvolutionTreeData;

        class EvolutionTree
        {
        public:
            explicit EvolutionTree(const EvolutionTreeData &treeData) noexcept : treeData(treeData) {}

            /// PKHeX IEvolutionForward.GetForward.
            std::span<const EvolutionMethod> getForward(uint16_t speciesId, uint8_t formId) const noexcept;

            /// PKHeX IEvolutionReverse.GetReverse; an empty node when nothing evolves into it.
            EvolutionNode getReverse(uint16_t speciesId, uint8_t formId) const noexcept;

            /// PKHeX IEvolutionReverse.GetPreEvolutions: the lowest stage first. Returns the count
            /// written to `result`.
            int getPreEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept;

            /// PKHeX IEvolutionForward.GetEvolutions, depth first in data order. Returns the count
            /// written to `result`.
            int getEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept;

            /// PKHeX EvolutionNetwork.GetEvolutionsAndPreEvolutions.
            int getEvolutionsAndPreEvolutions(uint16_t speciesId, uint8_t formId, std::span<SpeciesForm> result) const noexcept;

            /// PKHeX EvolutionNetwork.IsSpeciesDerivedFrom.
            bool isSpeciesDerivedFrom(uint16_t speciesId, uint8_t formId, uint16_t otherSpeciesId, uint8_t otherFormId, bool ignoreForm = true) const noexcept;

            /// PKHeX EvolutionNetwork.GetBaseSpeciesForm.
            SpeciesForm getBaseSpeciesForm(uint16_t speciesId, uint8_t formId) const noexcept;

            /// PKHeX IEvolutionForward.TryEvolve.
            bool tryEvolve(SpeciesForm head, SpeciesForm next, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept;

            /// PKHeX IEvolutionReverse.TryDevolve.
            bool tryDevolve(SpeciesForm head, const EvolutionEntity &entity, uint8_t currentMaxLevel, uint8_t levelMin, bool skipChecks, const EvolutionRuleTweak &tweak, EvolutionCriteria &result) const noexcept;

            /// PKHeX IPersonalTable.IsPresentInGame for the table this context's history is
            /// filtered by (Gen 1: Red/Blue, Gen 3: Emerald, Gen 4: HeartGold/SoulSilver, Gen 5:
            /// Black 2/White 2, Gen 6: Omega Ruby/Alpha Sapphire, Gen 7: Ultra Sun/Ultra Moon).
            bool isPresentInGame(uint16_t speciesId, uint8_t formId) const noexcept;

        private:
            const EvolutionTreeData &treeData;
        };

        /// PKHeX EvolutionTree.GetEvolutionTree; null when `contextGroup` names no context.
        const EvolutionTree *getEvolutionTree(Enums::GameVersion contextGroup) noexcept;
    }
}

#endif
