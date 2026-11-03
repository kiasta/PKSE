#include "Legality/RNG/PokewalkerRNG.h"

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t BOX_CAPACITY = 18 * 30;

            // clang-format off
            constexpr uint16_t COURSE_SPECIES[PokewalkerRNG::COURSE_COUNT * PokewalkerRNG::SLOTS_PER_COURSE] = {
                115, 84, 29, 32, 16, 161,     // Refreshing Field
                202, 69, 48, 46, 43, 21,      // Noisy Forest
                240, 95, 66, 77, 163, 74,     // Rugged Road
                54, 120, 79, 60, 191, 194,    // Beautiful Beach
                239, 81, 81, 198, 163, 19,    // Suburban Area
                238, 92, 92, 95, 41, 66,      // Dim Cave
                147, 60, 98, 90, 118, 72,     // Blue Lake
                63, 100, 109, 88, 19, 162,    // Town Outskirts
                300, 264, 314, 313, 263, 265, // Hoenn Field
                320, 298, 116, 318, 118, 129, // Warm Beach
                218, 307, 228, 111, 77, 74,   // Volcano Path
                352, 351, 203, 234, 44, 70,   // Treehouse
                105, 128, 42, 177, 66, 92,    // Scary Cave
                439, 415, 403, 406, 399, 401, // Sinnoh Field
                459, 361, 215, 436, 220, 179, // Icy Mountain Road
                357, 438, 114, 400, 179, 102, // Big Forest
                433, 200, 93, 418, 223, 170,  // White Lake
                456, 422, 129, 86, 54, 90,    // Stormy Beach
                417, 25, 39, 35, 183, 187,    // Resort
                442, 446, 433, 349, 164, 42,  // Quiet Cave
                120, 224, 116, 222, 223, 170, // Beyond The Sea
                35, 39, 41, 163, 74, 95,      // Night Sky's Edge
                25, 25, 25, 25, 25, 25,       // Yellow Forest
                441, 302, 25, 453, 427, 417,  // Rally
                255, 133, 279, 61, 52, 25,    // Sightseeing
                446, 374, 116, 355, 129, 436, // Winners Path
                239, 240, 238, 440, 174, 173, // Amity Meadow
            };
            // clang-format on

            // A mic test can advance from any no-stroll initial seed to any other initial seed.
            PokewalkerSeedResult getMicTest(std::span<uint32_t> seeds) noexcept
            {
                uint32_t stepCount = 0;
                while (true)
                {
                    for (uint32_t &seed : seeds)
                    {
                        if (PokewalkerRNG::isSeedFormatNoStroll(seed))
                        {
                            return PokewalkerSeedResult{seed, stepCount, PokewalkerSeedType::MicTestNoStroll};
                        }
                        if (stepCount != 0 && PokewalkerRNG::isSeedFormatStroll(seed))
                        {
                            return PokewalkerSeedResult{seed, stepCount - 1, PokewalkerSeedType::MicTestStroll};
                        }
                        seed = LCRNG::previous(seed);
                    }
                    stepCount++;
                }
            }
        }

        PokewalkerSeedResult PokewalkerRNG::getLeastEffortSeed(uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t first = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            const uint32_t second = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            return getLeastEffortSeed(first, second);
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/PokewalkerRNG.cs GetLeastEffortSeed: a
        // stroll spends 3 calls on its slots before each Pokemon's 2 IV calls, a no-stroll none, so
        // a stroll seed sits an odd number of calls back and a no-stroll seed an even number.
        PokewalkerSeedResult PokewalkerRNG::getLeastEffortSeed(uint32_t first, uint32_t second) noexcept
        {
            uint32_t seedBuffer[LCRNG::MAX_IV_SEED_COUNT];
            const int seedCount = LCRNGReversal::getSeedsIVs(seedBuffer, first, second);
            if (seedCount == 0)
            {
                return PokewalkerSeedResult{};
            }

            const std::span<uint32_t> seeds(seedBuffer, static_cast<size_t>(seedCount));
            for (uint32_t priorPokemon = 0; priorPokemon < BOX_CAPACITY; priorPokemon++)
            {
                for (uint32_t &seed : seeds)
                {
                    uint32_t candidate = seed;
                    if (isSeedFormatNoStroll(candidate))
                    {
                        return PokewalkerSeedResult{candidate, priorPokemon, PokewalkerSeedType::NoStroll};
                    }
                    candidate = LCRNG::previous(candidate);
                    if (priorPokemon != 0 && isSeedFormatStroll(candidate))
                    {
                        return PokewalkerSeedResult{candidate, priorPokemon - 1, PokewalkerSeedType::Stroll};
                    }
                    seed = LCRNG::previous(candidate);
                }
            }

            for (const uint32_t seed : seeds)
            {
                const uint32_t candidate = LCRNG::previous(seed);
                if (isSeedFormatStroll(candidate))
                {
                    return PokewalkerSeedResult{candidate, MAX_PRIOR_POKEMON, PokewalkerSeedType::Stroll};
                }
            }

            LCRNGReversal::getSeedsIVs(seedBuffer, first, second);
            return getMicTest(seeds);
        }

        // The slot taken is the one whose call came out 0, hence the inversion.
        bool PokewalkerRNG::isValidSeedStrollSlots(uint32_t seed, uint16_t speciesId, uint8_t courseIndex) noexcept
        {
            const std::span<const uint16_t> courseSpecies = getSpecies(courseIndex);
            if (courseSpecies.empty())
            {
                return false;
            }
            const int slotA = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            if (courseSpecies[slotA] == speciesId)
            {
                return true;
            }
            const int slotB = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            if (courseSpecies[slotB + 2] == speciesId)
            {
                return true;
            }
            const int slotC = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            return courseSpecies[slotC + 4] == speciesId;
        }

        PokewalkerRNG::StrollSlots PokewalkerRNG::getSlotsStroll(uint32_t &seed) noexcept
        {
            const int slotA = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            const int slotB = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            const int slotC = static_cast<int>(LCRNG::next16(seed) & 1) ^ 1;
            return StrollSlots{slotA, slotB, slotC};
        }

        std::span<const uint16_t> PokewalkerRNG::getSpecies(uint8_t courseIndex) noexcept
        {
            if (courseIndex >= COURSE_COUNT)
            {
                return {};
            }
            return std::span<const uint16_t>(COURSE_SPECIES + (courseIndex * SLOTS_PER_COURSE), SLOTS_PER_COURSE);
        }

        uint16_t PokewalkerRNG::getSpecies(uint8_t courseIndex, int groupIndex, int rareIndex) noexcept
        {
            const std::span<const uint16_t> courseSpecies = getSpecies(courseIndex);
            if (courseSpecies.empty() || groupIndex < 0 || groupIndex >= GROUPS_PER_COURSE || rareIndex < 0 || rareIndex >= SLOTS_PER_GROUP)
            {
                return 0;
            }
            return courseSpecies[(groupIndex * 2) + rareIndex];
        }

        uint32_t PokewalkerRNG::getPID(uint32_t ID32, uint32_t natureId, uint8_t gender, uint8_t genderRatio) noexcept
        {
            return getPID(static_cast<uint16_t>(ID32), static_cast<uint16_t>(ID32 >> 16), natureId, gender, genderRatio);
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/PokewalkerRNG.cs GetPID: the top byte keeps
        // the PID from ever being shiny; the nature and gender are then adjusted in steps of 25.
        uint32_t PokewalkerRNG::getPID(uint16_t TID16, uint16_t SID16, uint32_t natureId, uint8_t gender, uint8_t genderRatio) noexcept
        {
            if (natureId >= 24)
            {
                natureId = 0;
            }
            uint32_t PID = ((((static_cast<uint32_t>(TID16) ^ SID16) >> 8) ^ 0xFF) << 24);
            PID += natureId - (PID % 25);

            if (genderRatio == 0 || genderRatio >= 0xFE)
            {
                return PID;
            }

            const uint32_t pidGender = (PID & 0xFF) < genderRatio ? 1 : 0;
            if (gender == pidGender)
            {
                return PID;
            }

            if (gender == 0)
            {
                PID += (((genderRatio - (PID & 0xFF)) / 25) + 1) * 25;
                if ((natureId & 1) != (PID & 1))
                {
                    PID += 25;
                }
            }
            else
            {
                PID -= ((((PID & 0xFF) - genderRatio) / 25) + 1) * 25;
                if ((natureId & 1) != (PID & 1))
                {
                    PID -= 25;
                }
            }
            return PID;
        }
    }
}
