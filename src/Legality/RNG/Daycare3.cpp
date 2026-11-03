#include "Legality/RNG/Daycare3.h"

#include "Enums/GameVersion.h"
#include "Legality/RNG/LCRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint8_t VERSION_EMERALD = static_cast<uint8_t>(Enums::GameVersion::EM);
            constexpr int STAT_COUNT = 6;
            constexpr int INHERITED = -1;

            void applyInheritance(uint32_t seed, int *individualValues) noexcept
            {
                int statIndexes[STAT_COUNT] = {0, 1, 2, 3, 4, 5};
                for (int i = 0; i < 3; i++)
                {
                    const int pickIndex = static_cast<int>(LCRNG::next16(seed)) % (STAT_COUNT - i);
                    const int inherit = statIndexes[pickIndex];
                    for (int j = pickIndex + 1; j < STAT_COUNT; j++)
                    {
                        statIndexes[j - 1] = statIndexes[j];
                    }
                    individualValues[inherit] = INHERITED;
                }
            }

            // Emerald's bug: it removes position i (HP, then Defense, then position 2) instead of the stat just picked.
            void applyInheritanceEmerald(uint32_t seed, int *individualValues) noexcept
            {
                int statIndexes[STAT_COUNT] = {0, 1, 2, 3, 4, 5};
                for (int i = 0; i < 3; i++)
                {
                    const int pickIndex = static_cast<int>(LCRNG::next16(seed)) % (STAT_COUNT - i);
                    const int inherit = statIndexes[pickIndex];
                    for (int j = i + 1; j < STAT_COUNT; j++)
                    {
                        statIndexes[j - 1] = statIndexes[j];
                    }
                    individualValues[inherit] = INHERITED;
                }
            }

            void fill(int *individualValues, uint32_t firstIVs, uint32_t secondIVs) noexcept
            {
                individualValues[0] = static_cast<int>(firstIVs & 0x1F);
                individualValues[1] = static_cast<int>((firstIVs >> 5) & 0x1F);
                individualValues[2] = static_cast<int>((firstIVs >> 10) & 0x1F);
                individualValues[3] = static_cast<int>(secondIVs & 0x1F);
                individualValues[4] = static_cast<int>((secondIVs >> 5) & 0x1F);
                individualValues[5] = static_cast<int>((secondIVs >> 10) & 0x1F);
            }

            int getCountMatch(std::span<const int> actual, const int *generated) noexcept
            {
                int matchCount = 0;
                for (size_t statIndex = 0; statIndex < actual.size(); statIndex++)
                {
                    if (actual[statIndex] == generated[statIndex])
                    {
                        matchCount++;
                    }
                }
                return matchCount;
            }

            int getCountMatchInherit(std::span<const int> actual, const int *generated) noexcept
            {
                int matchCount = 0;
                for (size_t statIndex = 0; statIndex < actual.size(); statIndex++)
                {
                    const int individualValue = generated[statIndex];
                    if (individualValue == INHERITED || actual[statIndex] == individualValue)
                    {
                        matchCount++;
                    }
                }
                return matchCount;
            }

            void updateIfBetter(Daycare3Origin &best, uint32_t origin) noexcept
            {
                // Step back past frames no player can hit, then to the 16-bit initial seed.
                uint32_t seed = LCRNG::previous9(origin);
                while (seed >> 16 != 0)
                {
                    seed = LCRNG::previous(seed);
                }
                const uint32_t advances = LCRNG::getDistance(seed, origin);
                if (best.advances >= advances || best.pattern == Daycare3Correlation::None)
                {
                    best = Daycare3Origin{origin, advances, static_cast<uint16_t>(seed), Daycare3Correlation::Regular};
                }
            }
        }

        // PKHeX Daycare3.IsValidProcPID
        bool Daycare3::isValidProcPID(uint32_t PID, uint8_t version) noexcept
        {
            // Ruby, Sapphire, FireRed and LeafGreen store only the low half, as (rand() & 0xFFFE) + 1: never 0.
            if (version != VERSION_EMERALD)
            {
                return (PID & 0xFFFF) != 0;
            }
            // Emerald stores the full 32 bits; vBlank and Everstone retries make any nonzero PID reachable.
            return PID != 0;
        }

        bool Daycare3::tryGetOriginSeed(const Pokemon::Pokemon &pokemon, Daycare3Origin &origin) noexcept
        {
            const int individualValues[STAT_COUNT] = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD()};
            if (pokemon.originGame() == VERSION_EMERALD)
            {
                return tryGetOriginSeedEmerald(individualValues, origin);
            }
            return tryGetOriginSeed(individualValues, pokemon.encryptionConstant(), origin);
        }

        bool Daycare3::tryGetOriginSeed(std::span<const int> individualValues, uint32_t PID, uint8_t version, Daycare3Origin &origin) noexcept
        {
            if (version == VERSION_EMERALD)
            {
                return tryGetOriginSeedEmerald(individualValues, origin);
            }
            return tryGetOriginSeed(individualValues, PID, origin);
        }

        // PKHeX Daycare3.TryGetOriginSeedEmerald
        bool Daycare3::tryGetOriginSeedEmerald(std::span<const int> individualValues, Daycare3Origin &origin) noexcept
        {
            // Frames: IVs low, IVs high (both overwritten by inheritance later), vBlank, inheritance.
            int generated[STAT_COUNT] = {};

            // Searches forward from 60 frames after Emerald's initial 0 seed, so the first result is the earliest.
            uint32_t frame = 0xBE34A09C;
            while (true)
            {
                frame = LCRNG::next(frame);
                uint32_t seed = frame;
                const uint32_t firstIVs = LCRNG::next16(seed);
                const uint32_t secondIVs = LCRNG::next16(seed);
                fill(generated, firstIVs, secondIVs);
                if (getCountMatch(individualValues, generated) < 3)
                {
                    continue;
                }
                // vBlank: setting IVs is slow.
                LCRNG::next16(seed);
                applyInheritanceEmerald(seed, generated);
                if (getCountMatchInherit(individualValues, generated) != STAT_COUNT)
                {
                    continue;
                }

                // Unroll once, and account for the interaction lag (vBlank).
                frame = LCRNG::previous4(frame);
                const uint32_t advances = LCRNG::getDistance(0, frame);
                origin = Daycare3Origin{frame, advances, 0, Daycare3Correlation::Regular};
                return true;
            }
        }

        // PKHeX Daycare3.TryGetOriginSeed
        bool Daycare3::tryGetOriginSeed(std::span<const int> individualValues, uint32_t PID, Daycare3Origin &origin) noexcept
        {
            // Frames: PID high, vBlank, IVs low, IVs high (both overwritten by inheritance later), vBlank, inheritance.
            int generated[STAT_COUNT] = {};
            origin = Daycare3Origin{};

            // The PID is the first call, and the seed's low half is unknown: try all of them and keep the fewest advances.
            const uint32_t high = PID & 0xFFFF0000;
            for (uint32_t lowHalf = 0; lowHalf < 0x10000; lowHalf++)
            {
                uint32_t seed = high | lowHalf;
                // vBlank: the PID and the other properties are set before the IVs.
                LCRNG::next16(seed);
                const uint32_t firstIVs = LCRNG::next16(seed);
                const uint32_t secondIVs = LCRNG::next16(seed);
                fill(generated, firstIVs, secondIVs);
                if (getCountMatch(individualValues, generated) < 3)
                {
                    continue;
                }
                // vBlank: setting IVs is slow.
                LCRNG::next16(seed);
                applyInheritance(seed, generated);
                if (getCountMatchInherit(individualValues, generated) != STAT_COUNT)
                {
                    continue;
                }

                const uint32_t generate = LCRNG::previous4(high | lowHalf);
                updateIfBetter(origin, generate);
            }
            return origin.pattern != Daycare3Correlation::None;
        }
    }
}
