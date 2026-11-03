/**
 * The Pokewalker: its PIDs are a formula of the trainer ids, nature and gender (never shiny), and
 * its IVs come from an initial seed of the stroll time or the date.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/PokewalkerRNG.cs. A course is PKHeX's
 * PokewalkerCourse4 index (0 Refreshing Field ... 26 Amity Meadow).
 */
#ifndef LEGALITY_RNG_POKEWALKERRNG_H
#define LEGALITY_RNG_POKEWALKERRNG_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        enum class PokewalkerSeedType : uint8_t
        {
            None = 0,
            Stroll = 1,
            NoStroll = 2,
            MicTestStroll = 3,
            MicTestNoStroll = 4,
        };

        struct PokewalkerSeedResult
        {
            uint32_t seed = 0;
            /// How many Pokemon were generated before this one (24 bits in PKHeX's layout).
            uint32_t priorPokemonCount = 0;
            PokewalkerSeedType type = PokewalkerSeedType::None;
        };

        struct PokewalkerRNG
        {
            static constexpr int SLOTS_PER_COURSE = 6;
            static constexpr int GROUPS_PER_COURSE = 3;
            static constexpr int SLOTS_PER_GROUP = 2;
            static constexpr int COURSE_COUNT = 27;
            /// A stroll seed's prior-Pokemon count stops at the PC's capacity, less one.
            static constexpr uint32_t MAX_PRIOR_POKEMON = (18 * 30) - 1;

            static constexpr uint32_t getSeedStroll(uint32_t hour, uint32_t minute, uint32_t second) noexcept { return (3600 * hour) + (60 * minute) + second; }

            static constexpr uint32_t getSeedNoStroll(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second) noexcept { return ((((month * day) + minute + second) & 0xFF) << 24) | (hour << 16) | year; }

            /// A stroll seed is seconds into the day.
            static constexpr bool isSeedFormatStroll(uint32_t seed) noexcept { return seed < 60 * 60 * 24; }

            /// A no-stroll seed's top byte can be anything; the hour and year must be in range.
            static constexpr bool isSeedFormatNoStroll(uint32_t seed) noexcept { return static_cast<uint16_t>(seed) < 100 && static_cast<uint8_t>(seed >> 16) < 24; }

            /// The initial seed needing the fewest prior Pokemon for an IV spread (halves << 16).
            static PokewalkerSeedResult getLeastEffortSeed(uint32_t first, uint32_t second) noexcept;

            static PokewalkerSeedResult getLeastEffortSeed(uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;

            /// True when one of the stroll's three slot calls from `seed` picks `speciesId`.
            static bool isValidSeedStrollSlots(uint32_t seed, uint16_t speciesId, uint8_t courseIndex) noexcept;

            struct StrollSlots
            {
                int slotA;
                int slotB;
                int slotC;
            };
            static StrollSlots getSlotsStroll(uint32_t &seed) noexcept;

            /// The six species of a course; empty for a course index out of range.
            static std::span<const uint16_t> getSpecies(uint8_t courseIndex) noexcept;

            /// 0 for an out-of-range group or rarity.
            static uint16_t getSpecies(uint8_t courseIndex, int groupIndex, int rareIndex) noexcept;

            static uint32_t getPID(uint32_t ID32, uint32_t natureId, uint8_t gender, uint8_t genderRatio) noexcept;

            static uint32_t getPID(uint16_t TID16, uint16_t SID16, uint32_t natureId, uint8_t gender, uint8_t genderRatio) noexcept;
        };
    }
}

#endif
