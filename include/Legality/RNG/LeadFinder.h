/**
 * Which party lead (if any) a Gen 3 or Gen 4 wild encounter's PID/IV correlation needed, from the
 * MethodFinder result and the candidate slot.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/LeadFinder.cs and LeadEncounter.cs.
 * PKHeX's GetLevelConstraint reads its evolution chain and stays with the encounter engine, which
 * passes the resulting level range here as levelMin/levelMax.
 */
#ifndef LEGALITY_RNG_LEADFINDER_H
#define LEGALITY_RNG_LEADFINDER_H

#include <cstdint>
#include <vector>

#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/RNG/LeadSeed.h"
#include "Legality/RNG/PIDIV.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct LeadFinder
        {
            static LeadSeed getLeadInfo3(const EncounterSlot3Parameters &encounter, const PIDIV &pidiv, uint8_t levelMin, uint8_t levelMax, bool emerald, uint8_t gender, uint8_t format) noexcept;

            static LeadSeed getLeadInfo4(const Pokemon::Pokemon &pokemon, const EncounterSlot4Parameters &encounter, const PIDIV &pidiv, uint8_t levelMin, uint8_t levelMax) noexcept;

            static bool tryGetLeadInfo4(const EncounterSlot4Parameters &encounter, uint8_t levelMin, uint8_t levelMax, bool heartGoldSoulSilver, uint32_t seed, uint8_t format, LeadSeed &result) noexcept;
        };

        template <typename Encounter>
        struct LeadEncounter
        {
            LeadSeed lead;
            Encounter encounter;

            bool isBetterThan(const LeadEncounter &other) const noexcept { return lead.isBetterThan(other.lead); }
        };

        /// Candidates kept best lead first; an equal lead keeps its arrival order.
        template <typename Encounter>
        struct LeadEncounterQueue
        {
            std::vector<LeadEncounter<Encounter>> list;

            void insert(const LeadSeed &lead, const Encounter &encounter)
            {
                const LeadEncounter<Encounter> toInsert{lead, encounter};
                for (size_t listIndex = 0; listIndex < list.size(); listIndex++)
                {
                    if (!toInsert.isBetterThan(list[listIndex]))
                    {
                        continue;
                    }
                    list.insert(list.begin() + static_cast<std::ptrdiff_t>(listIndex), toInsert);
                    return;
                }
                list.push_back(toInsert);
            }
        };
    }
}

#endif
