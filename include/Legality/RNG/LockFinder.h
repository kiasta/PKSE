/**
 * Colosseum and XD shadow Pokemon: the trainer's team before a shadow Pokemon is generated with
 * nature/gender locks, so a shadow Pokemon's seed is only reachable if the locked team could have
 * been generated before it.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/CXD/LockFinder.cs, NPCLock.cs, TeamLock.cs,
 * TeamLockResult.cs, SeedFrame.cs and Frame/FrameCache.cs. The team data itself lives with the
 * encounters, which pass it here.
 */
#ifndef LEGALITY_RNG_LOCKFINDER_H
#define LEGALITY_RNG_LOCKFINDER_H

#include <cstdint>
#include <span>
#include <vector>

#include "Legality/RNG/XDRNG.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        /// Seeds stepped backwards (XDRNG) from an origin, generated as they are first read.
        class FrameCache
        {
        public:
            explicit FrameCache(uint32_t origin);

            /// The upper 16 bits of the seed `index` steps back.
            uint32_t operator[](int frameIndex);
            uint32_t getSeed(int frameIndex);

        private:
            void add(uint32_t seed);

            std::vector<uint32_t> seeds;
            std::vector<uint32_t> values;
            uint32_t last = 0;
        };

        struct NPCLock
        {
            uint8_t nature = 0;
            uint8_t gender = 0;
            uint8_t ratio = 0;
            uint8_t state = 0;
            uint16_t species = 0;

            /// A team member with a nature/gender lock (gender 2 means any).
            static constexpr NPCLock regular(uint16_t speciesId, uint8_t natureId, uint8_t genderValue, uint8_t genderRatio) noexcept { return NPCLock{natureId, genderValue, genderRatio, 0, speciesId}; }
            /// A shadow team member, already seen or not.
            static constexpr NPCLock shadow(uint16_t speciesId, bool seen = false) noexcept { return NPCLock{0, 0, 0, static_cast<uint8_t>(seen ? 2 : 1), speciesId}; }

            constexpr int framesConsumed() const noexcept { return isSeen() ? 5 : 7; }
            constexpr bool isSeen() const noexcept { return state > 1; }
            constexpr bool isShadow() const noexcept { return state != 0; }

            constexpr bool matchesLock(uint32_t PID) const noexcept
            {
                // A shadow lock with no nature set is unlocked.
                if (isShadow() && nature == 0)
                {
                    return true;
                }
                if (gender != 2 && gender != ((PID & 0xFF) < ratio ? 1 : 0))
                {
                    return false;
                }
                return nature == PID % 25;
            }
        };

        struct TeamLock
        {
            uint16_t species = 0;
            /// PKHeX NPCLockDetail: which story point or seen Pokemon the team is for, for display only.
            uint8_t comment = 0;
            std::span<const NPCLock> locks;
        };

        struct SeedFrame
        {
            uint32_t PID;
            int frameId;
        };

        /// Reverse-generates a locked team from `originSeed`, taking reroll and trainer-shiny skips into account.
        class TeamLockResult
        {
        public:
            TeamLockResult(const TeamLock &teamSpecification, uint32_t seed, uint32_t trainerShinyValue);

            uint32_t originSeed = 0;
            bool isValid = false;

        private:
            friend class TeamLockFrameIterator;

            bool findLockSeed(int frame, const NPCLock *prior);
            bool verifyNPC(int frame);

            static constexpr uint32_t NOT_FORCED = 0xFFFFFFFF;

            const TeamLock &specifications;
            int originFrame = 0;
            uint32_t requiredCPUShinyValue = NOT_FORCED;
            uint32_t trainerShinyValue;
            std::vector<NPCLock> locks;
            FrameCache cache;
            std::vector<SeedFrame> team;
        };

        struct LockFinder
        {
            /// `isXD` is true for an XD shadow encounter; Colosseum has no player-shiny reroll.
            static bool isAllShadowLockValid(bool isXD, std::span<const TeamLock> teams, uint32_t seed, const Pokemon::Pokemon &pokemon);
            static bool isAllShadowLockValid(std::span<const TeamLock> teams, uint32_t seed, uint32_t trainerShinyValue = 0xFFFFFFFF);
        };
    }
}

#endif
