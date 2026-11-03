/**
 * The encounter properties the Gen 8 and Gen 9 seed checks regenerate a Pokemon from.
 *
 * Ported from PKHeX: Shiny (Legality/Encounters/Templates/Enums/Shiny.cs), AbilityPermission
 * (AbilityPermission.cs), SizeType9 (Templates/Gen9/SizeType9.cs), IndividualValueSet
 * (Templates/Shared/IndividualValueSet.cs), GemType (Templates/Gen9/GemType.cs) and
 * Legality/RNG/Methods GenerateParam8, GenerateParam9 and OverworldParam8a. The values are PKHeX's.
 */
#ifndef LEGALITY_RNG_GENERATEPARAMETERS_H
#define LEGALITY_RNG_GENERATEPARAMETERS_H

#include <cstdint>
#include <span>

#include "Legality/RNG/Xoroshiro128Plus.h"

namespace Legality
{
    namespace RNG
    {
        enum class Shiny : uint8_t
        {
            Random = 0,
            Never,
            Always,
            AlwaysStar,
            AlwaysSquare,
            FixedValue,
        };

        enum class AbilityPermission : int8_t
        {
            Any12H = -1,
            Any12 = 0,
            OnlyFirst = 1,
            OnlySecond = 2,
            OnlyHidden = 4,
        };

        /// The ability index (0, 1 or 2) of a single-ability permission; -1 for Any12/Any12H, where PKHeX throws.
        constexpr int getSingleValue(AbilityPermission permission) noexcept
        {
            switch (permission)
            {
            case AbilityPermission::OnlyFirst:
                return 0;
            case AbilityPermission::OnlySecond:
                return 1;
            case AbilityPermission::OnlyHidden:
                return 2;
            default:
                return -1;
            }
        }

        enum class SizeType9 : uint8_t
        {
            RANDOM = 0,
            XS = 1,
            S = 2,
            M = 3,
            L = 4,
            XL = 5,
            VALUE = 6,
        };

        /// PKHeX SizeType9Extensions.GetSizeValue; an unknown type gives 0, where PKHeX throws.
        constexpr uint8_t getSizeValue(SizeType9 type, uint8_t value, Xoroshiro128Plus &random) noexcept
        {
            switch (type)
            {
            case SizeType9::RANDOM:
            {
                const uint64_t first = random.nextInt(0x81);
                return static_cast<uint8_t>(first + random.nextInt(0x80));
            }
            case SizeType9::XS:
                return static_cast<uint8_t>(random.nextInt(0x10));
            case SizeType9::S:
                return static_cast<uint8_t>(random.nextInt(0x20) + 0x10);
            case SizeType9::M:
                return static_cast<uint8_t>(random.nextInt(0xA0) + 0x30);
            case SizeType9::L:
                return static_cast<uint8_t>(random.nextInt(0x20) + 0xD0);
            case SizeType9::XL:
                return static_cast<uint8_t>(random.nextInt(0x10) + 0xF0);
            case SizeType9::VALUE:
                return value;
            default:
                return 0;
            }
        }

        /// Fixed IVs in PKHeX's HP/Atk/Def/Spe/SpA/SpD order; unspecified when `isSpecified` is false.
        struct IndividualValueSet
        {
            int8_t HP = 0;
            int8_t ATK = 0;
            int8_t DEF = 0;
            int8_t SPE = 0;
            int8_t SPA = 0;
            int8_t SPD = 0;
            bool isSpecified = false;

            /// Writes HP, Atk, Def, SpA, SpD, Spe: the order the games roll IVs in.
            constexpr void copyToSpeedLast(std::span<int> individualValues) const noexcept
            {
                individualValues[5] = SPE;
                individualValues[4] = SPD;
                individualValues[3] = SPA;
                individualValues[2] = DEF;
                individualValues[1] = ATK;
                individualValues[0] = HP;
            }
        };

        enum class GemType : uint8_t
        {
            Default = 0,
            Random = 1,
            Normal = 2,
            Fighting = 3,
            Flying = 4,
            Poison = 5,
            Ground = 6,
            Rock = 7,
            Bug = 8,
            Ghost = 9,
            Steel = 10,
            Fire = 11,
            Water = 12,
            Grass = 13,
            Electric = 14,
            Psychic = 15,
            Ice = 16,
            Dragon = 17,
            Dark = 18,
            Fairy = 19,
            Stellar = 101,
        };

        /// A fixed Tera type, as a type id (gem value minus 2).
        constexpr bool isSpecified(GemType type, uint8_t &expect) noexcept
        {
            if (type == GemType::Default || type == GemType::Random)
            {
                expect = 0;
                return false;
            }
            expect = static_cast<uint8_t>(static_cast<uint8_t>(type) - 2);
            return true;
        }

        constexpr uint8_t RANDOM_NATURE = 25;

        struct GenerateParam8
        {
            uint16_t species = 0;
            uint8_t genderRatio = 0;
            uint8_t flawlessIVs = 0;
            AbilityPermission ability = AbilityPermission::Any12;
            Shiny shiny = Shiny::Random;
            /// A nature id, or RANDOM_NATURE.
            uint8_t nature = RANDOM_NATURE;
            IndividualValueSet individualValues{};
        };

        struct GenerateParam9
        {
            uint16_t species = 0;
            uint8_t genderRatio = 0;
            uint8_t flawlessIVs = 0;
            uint8_t rollCount = 0;
            uint8_t heightScalar = 0;
            uint8_t weightScalar = 0;
            SizeType9 scaleType = SizeType9::RANDOM;
            uint8_t scale = 0;
            AbilityPermission ability = AbilityPermission::Any12;
            Shiny shiny = Shiny::Random;
            uint8_t nature = RANDOM_NATURE;
            IndividualValueSet individualValues{};
        };

        struct OverworldParam8a
        {
            bool isAlpha = false;
            uint8_t genderRatio = 0;
            uint8_t flawlessIVs = 0;
            uint8_t rollCount = 0;
            Shiny shiny = Shiny::Random;
        };
    }
}

#endif
