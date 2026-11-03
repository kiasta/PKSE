#include "Legality/RNG/LockFinder.h"

#include "Legality/RNG/ShinyUtil.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        // PKHeX PKHeX.Core/Legality/RNG/Frame/FrameCache.cs
        FrameCache::FrameCache(uint32_t origin)
        {
            seeds.reserve(32);
            values.reserve(32);
            add(origin);
        }

        void FrameCache::add(uint32_t seed)
        {
            last = seed;
            seeds.push_back(seed);
            values.push_back(seed >> 16);
        }

        uint32_t FrameCache::operator[](int frameIndex)
        {
            while (static_cast<size_t>(frameIndex) >= seeds.size())
            {
                add(XDRNG::previous(last));
            }
            return values[static_cast<size_t>(frameIndex)];
        }

        uint32_t FrameCache::getSeed(int frameIndex)
        {
            while (static_cast<size_t>(frameIndex) >= seeds.size())
            {
                add(XDRNG::previous(last));
            }
            return seeds[static_cast<size_t>(frameIndex)];
        }

        /// PKHeX TeamLockResult.GetPossibleLocks (GetSingleLock and GetAllLocks), a lazy C# iterator: each
        /// frame is produced only when asked for, because the shared forced CPU shiny value it reads and
        /// writes is also changed by the deeper searches run between frames.
        class TeamLockFrameIterator
        {
        public:
            TeamLockFrameIterator(TeamLockResult &result, int frame, const NPCLock &currentLock, const NPCLock *priorLock) noexcept : owner(result), current(currentLock), counter(frame), start(frame)
            {
                if (priorLock == nullptr || priorLock->isShadow())
                {
                    stage = Stage::SingleFirst;
                }
                else
                {
                    prior = *priorLock;
                    stage = Stage::AllLocks;
                }
            }

            bool moveNext(SeedFrame &result)
            {
                switch (stage)
                {
                case Stage::SingleFirst:
                    return moveNextSingleFirst(result);
                case Stage::SingleRerolls:
                    return moveNextSingleReroll(result);
                case Stage::AllLocks:
                    return moveNextAllLocks(result);
                default:
                    return false;
                }
            }

        private:
            enum class Stage
            {
                SingleFirst,
                SingleRerolls,
                AllLocks,
                Done,
            };

            bool moveNextSingleFirst(SeedFrame &result)
            {
                PID = (owner.cache[counter + 1] << 16) | owner.cache[counter];
                if (!current.matchesLock(PID))
                {
                    stage = Stage::Done;
                    return false;
                }
                result = SeedFrame{PID, counter + current.framesConsumed()};
                // The single lock may not be enough: the frame before it may have been an anti-shiny reroll.
                stage = Stage::SingleRerolls;
                forcedTrainer = false;
                start = 2;
                return true;
            }

            bool moveNextSingleReroll(SeedFrame &result)
            {
                const uint32_t upper = owner.cache[start + 1];
                const uint32_t lower = owner.cache[start];
                const uint32_t shinyValue = (upper ^ lower) >> 3;
                // XD's shiny check covers every opponent Pokemon, shadow or not: matching the trainer is a reroll.
                if (shinyValue != owner.trainerShinyValue && owner.requiredCPUShinyValue != TeamLockResult::NOT_FORCED)
                {
                    if (shinyValue == owner.requiredCPUShinyValue)
                    {
                        // PKHeX loops here forever on the same frame; ending the frames is the only finite reading.
                        owner.requiredCPUShinyValue = shinyValue;
                        forcedTrainer = true;
                        stage = Stage::Done;
                        return false;
                    }
                    if (forcedTrainer)
                    {
                        owner.requiredCPUShinyValue = TeamLockResult::NOT_FORCED;
                    }
                    stage = Stage::Done;
                    return false;
                }
                // The rerolled PID stands in for the bad anti-shiny one.
                result = SeedFrame{PID, start + current.framesConsumed()};
                start += 2;
                return true;
            }

            bool moveNextAllLocks(SeedFrame &result)
            {
                // The next lock is generated 7 + 2n frames after, so reverse until a sequential generation fits.
                while (true)
                {
                    const int interruptFrame = counter - 7;
                    if (interruptFrame > start)
                    {
                        const uint32_t upper = owner.cache[interruptFrame + 1];
                        const uint32_t lower = owner.cache[interruptFrame];
                        const uint32_t CPUPID = (upper << 16) | lower;
                        const uint32_t shinyValue = (upper ^ lower) >> 3;
                        // A shiny interrupt against the trainer is ignored.
                        if (shinyValue != owner.trainerShinyValue && prior.matchesLock(CPUPID))
                        {
                            if (owner.requiredCPUShinyValue != TeamLockResult::NOT_FORCED)
                            {
                                if (shinyValue != owner.requiredCPUShinyValue)
                                {
                                    if (forcedTrainer)
                                    {
                                        owner.requiredCPUShinyValue = TeamLockResult::NOT_FORCED;
                                    }
                                    stage = Stage::Done;
                                    return false;
                                }
                            }
                            else
                            {
                                // Skip this interrupt by requiring the CPU trainer to have this shiny value.
                                owner.requiredCPUShinyValue = shinyValue;
                                forcedTrainer = true;
                            }
                        }
                    }

                    const uint32_t candidatePID = (owner.cache[counter + 1] << 16) | owner.cache[counter];
                    const int frame = counter;
                    counter += 2;
                    if (current.matchesLock(candidatePID))
                    {
                        result = SeedFrame{candidatePID, frame + current.framesConsumed()};
                        return true;
                    }
                }
            }

            TeamLockResult &owner;
            NPCLock current;
            NPCLock prior{};
            Stage stage = Stage::Done;
            int counter;
            int start;
            bool forcedTrainer = false;
            uint32_t PID = 0;
        };

        TeamLockResult::TeamLockResult(const TeamLock &teamSpecification, uint32_t seed, uint32_t shinyValue) : specifications(teamSpecification), trainerShinyValue(shinyValue), locks(teamSpecification.locks.begin(), teamSpecification.locks.end()), cache(XDRNG::previous2(seed))
        {
            team.reserve(locks.size());
            isValid = findLockSeed(0, nullptr);
            if (isValid)
            {
                originSeed = cache.getSeed(originFrame);
            }
        }

        // PKHeX TeamLockResult.FindLockSeed: locks are a stack whose top is the last team member.
        bool TeamLockResult::findLockSeed(int frame, const NPCLock *prior)
        {
            if (locks.empty())
            {
                return verifyNPC(frame);
            }

            const NPCLock current = locks.back();
            locks.pop_back();
            TeamLockFrameIterator frames(*this, frame, current, prior);
            SeedFrame candidate{};
            while (frames.moveNext(candidate))
            {
                team.push_back(candidate);
                if (findLockSeed(candidate.frameId, &current))
                {
                    return true;
                }
                team.pop_back();
            }
            locks.push_back(current);
            return false;
        }

        bool TeamLockResult::verifyNPC(int frame)
        {
            const uint32_t CPUTID16 = cache[frame + 1];
            const uint32_t CPUSID16 = cache[frame];
            const uint32_t CPUShinyValue = (CPUTID16 ^ CPUSID16) >> 3;
            if (requiredCPUShinyValue != NOT_FORCED && requiredCPUShinyValue != CPUShinyValue)
            {
                return false;
            }

            // The team is walked from the top of PKHeX's stack (the last pushed) down, against the locks from the end.
            int position = static_cast<int>(team.size()) - 1;
            for (auto member = team.rbegin(); member != team.rend(); ++member)
            {
                const uint32_t memberPID = member->PID;
                const uint32_t PIDShinyValue = ((memberPID & 0xFFFF) ^ (memberPID >> 16)) >> 3;
                // Shininess against the trainer was already handled while regenerating the team.
                if (PIDShinyValue == CPUShinyValue)
                {
                    if (!specifications.locks[static_cast<size_t>(position)].isShadow())
                    {
                        return false;
                    }
                    if (trainerShinyValue != NOT_FORCED)
                    {
                        return false;
                    }
                }
                position--;
            }
            originFrame = frame + 2;
            return true;
        }

        // PKHeX LockFinder.IsAllShadowLockValid
        bool LockFinder::isAllShadowLockValid(bool isXD, std::span<const TeamLock> teams, uint32_t seed, const Pokemon::Pokemon &pokemon)
        {
            // XD has no shiny shadow Pokemon.
            if (isXD && ShinyUtil::isShiny(pokemon))
            {
                return false;
            }
            if (teams.empty())
            {
                return true;
            }
            const uint32_t trainerShinyValue = isXD ? static_cast<uint32_t>(pokemon.tid16() ^ pokemon.sid16()) >> 3 : 0xFFFFFFFF;
            return isAllShadowLockValid(teams, seed, trainerShinyValue);
        }

        bool LockFinder::isAllShadowLockValid(std::span<const TeamLock> teams, uint32_t seed, uint32_t trainerShinyValue)
        {
            for (const TeamLock &team : teams)
            {
                const TeamLockResult result(team, seed, trainerShinyValue);
                if (result.isValid)
                {
                    return true;
                }
            }
            return false;
        }
    }
}
