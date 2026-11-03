#include "Legality/Encounters/EggHatchLocation.h"

#include <array>

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t MASK_RUBY_SAPPHIRE = 1 << 0;
            constexpr uint8_t MASK_EMERALD = 1 << 1;
            constexpr uint8_t MASK_FIRERED_LEAFGREEN = 1 << 2;
            constexpr uint8_t MASK_ALL3 = MASK_RUBY_SAPPHIRE | MASK_EMERALD | MASK_FIRERED_LEAFGREEN;

            // PKHeX EggHatchLocation3.LocationPermitted3. 64, 71, 75, 77 and 84 are unused or
            // unreachable Hoenn maps; 155-158 and 171-173 are unused Sevii Isles.
            // clang-format off
            constexpr std::array<uint8_t, 213> LOCATION_PERMITTED3 = {
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 0, 3, 3, 3, 3, 3,
                3, 0, 3, 3, 3, 0, 3, 0, 3, 3,
                3, 3, 3, 3, 0, 3, 3, 7, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 0, 0, 0, 0, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 0, 0, 0, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 6, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2,
            };
            // clang-format on

            bool hasLocationFlag(uint8_t mask, uint16_t location) noexcept
            {
                if (location >= LOCATION_PERMITTED3.size())
                {
                    return false;
                }
                return (LOCATION_PERMITTED3[location] & mask) != 0;
            }
        }

        bool isValidMet3(uint16_t location, uint8_t version) noexcept
        {
            switch (static_cast<Enums::GameVersion>(version))
            {
            case Enums::GameVersion::RU:
            case Enums::GameVersion::SA:
                return hasLocationFlag(MASK_RUBY_SAPPHIRE, location);
            case Enums::GameVersion::EM:
                return hasLocationFlag(MASK_EMERALD, location);
            case Enums::GameVersion::FR:
            case Enums::GameVersion::LG:
                return hasLocationFlag(MASK_FIRERED_LEAFGREEN, location);
            default:
                return false;
            }
        }

        bool isValidMet3Any(uint16_t location) noexcept
        {
            return hasLocationFlag(MASK_ALL3, location);
        }

        namespace
        {
            constexpr uint8_t MASK_DIAMOND_PEARL = 1 << 0;
            constexpr uint8_t MASK_PLATINUM = 1 << 1;
            constexpr uint8_t MASK_HEARTGOLD_SOULSILVER = 1 << 2;
            constexpr uint8_t MASK_ALL4 = MASK_DIAMOND_PEARL | MASK_PLATINUM | MASK_HEARTGOLD_SOULSILVER;

            // PKHeX EggHatchLocation4.LocationPermitted4.
            // clang-format off
            constexpr std::array<uint8_t, 235> LOCATION_PERMITTED4 = {
                0, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 2, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 2,
                7, 3, 3, 3, 3, 2, 0, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 6, 6, 6, 6, 6, 2, 6, 2,
                2, 6, 2, 2, 2, 2, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 0, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 0, 4,
            };
            // clang-format on

            bool hasLocationFlag4(uint8_t mask, uint16_t location) noexcept
            {
                if (location >= LOCATION_PERMITTED4.size())
                {
                    return false;
                }
                return (LOCATION_PERMITTED4[location] & mask) != 0;
            }
        }

        bool isValidMet4(uint16_t location, uint8_t version) noexcept
        {
            switch (static_cast<Enums::GameVersion>(version))
            {
            case Enums::GameVersion::D:
            case Enums::GameVersion::P:
                return hasLocationFlag4(MASK_DIAMOND_PEARL, location);
            case Enums::GameVersion::Pt:
                return hasLocationFlag4(MASK_PLATINUM, location);
            case Enums::GameVersion::HG:
            case Enums::GameVersion::SS:
                return hasLocationFlag4(MASK_HEARTGOLD_SOULSILVER, location);
            default:
                return false;
            }
        }

        bool isValidMet4Any(uint16_t location) noexcept
        {
            return hasLocationFlag4(MASK_ALL4, location);
        }

        bool isValidMet5(uint16_t location, uint8_t version) noexcept
        {
            // PKHeX EggHatchLocation5.LocationPermitted5: bit 0 White, 1 Black, 2 White 2, 3 Black 2.
            // White and White 2 cannot reach Black Gate (112); Black and Black 2 cannot reach White Gate (113).
            // clang-format off
            constexpr std::array<uint8_t, 154> locationPermitted5 = {
                0, 0, 0, 0, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 3, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                3, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 3,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 3, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                15, 15, 10, 5, 15, 15, 15, 12, 12, 12,
                12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                12, 12, 12, 12, 12, 12, 12, 12, 0, 12,
                12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                12, 12, 12, 12,
            };
            // clang-format on
            const unsigned shift = static_cast<unsigned>(version) - static_cast<unsigned>(Enums::GameVersion::W);
            if (shift >= 4 || location >= locationPermitted5.size())
            {
                return false;
            }
            return (locationPermitted5[location] & (1u << shift)) != 0;
        }

        bool isValidMet6XY(uint16_t location) noexcept
        {
            // Every X/Y map id is even; 80 is unused.
            constexpr uint16_t locationMin = 6;
            constexpr uint16_t locationMax = 168;
            if (location < locationMin || location > locationMax)
            {
                return false;
            }
            return location % 2 == 0 && location != 80;
        }

        bool isValidMet6AO(uint16_t location) noexcept
        {
            // Every Omega Ruby/Alpha Sapphire map id is even; 348 is unused.
            constexpr uint16_t locationMin = 170;
            constexpr uint16_t locationMax = 354;
            if (location < locationMin || location > locationMax)
            {
                return false;
            }
            return location % 2 == 0 && location != 348;
        }

        namespace
        {
            constexpr uint8_t HATCH_MASK_SUN_MOON = 1 << 0;
            constexpr uint8_t HATCH_MASK_ULTRA_SUN_MOON = 1 << 1;
            /// PKHeX Locations.Pelago7: Poke Pelago, where an egg can hatch in all four games.
            constexpr uint16_t LOCATION_POKE_PELAGO7 = 30016;

            // PKHeX EggHatchLocation7.LocationPermitted7: bit 0 Sun/Moon, bit 1 Ultra Sun/Ultra Moon.
            // clang-format off
            constexpr std::array<uint8_t, 233> LOCATION_PERMITTED7 = {
                0, 0, 0, 0, 0, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 0, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                0, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 0, 0, 0, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 0, 0, 0, 0, 2, 0,
                2, 0, 2, 0, 2, 0, 2, 0, 2, 0,
                2, 0, 2, 0, 2, 0, 2, 0, 2, 0,
                2, 0, 2, 0, 2, 0, 2, 0, 2, 0,
                2, 0, 2,
            };
            // clang-format on

            bool isValidMet7(uint8_t mask, uint16_t location) noexcept
            {
                if (location < LOCATION_PERMITTED7.size() && (LOCATION_PERMITTED7[location] & mask) != 0)
                {
                    return true;
                }
                return location == LOCATION_POKE_PELAGO7;
            }
        }

        bool isValidMet7SM(uint16_t location) noexcept
        {
            return isValidMet7(HATCH_MASK_SUN_MOON, location);
        }

        bool isValidMet7USUM(uint16_t location) noexcept
        {
            return isValidMet7(HATCH_MASK_ULTRA_SUN_MOON, location);
        }

        bool isValidMet8SWSH(uint16_t location) noexcept
        {
            // PKHeX EggHatchLocation8.LocationPermitted8, indexed by location / 2 (odd locations are never hatch spots).
            constexpr std::array<uint8_t, 124> locationPermitted8 = {
                0, 0, 0, 1, 1, 0, 1, 1, 1, 1,
                1, 1, 1, 0, 1, 1, 1, 1, 1, 0,
                1, 0, 1, 1, 1, 0, 1, 1, 1, 1,
                1, 0, 1, 1, 1, 1, 1, 0, 1, 1,
                1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
                0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1,
            };
            if (location % 2 != 0)
            {
                return false;
            }
            const uint16_t permitIndex = location >> 1;
            if (permitIndex >= locationPermitted8.size())
            {
                return false;
            }
            return locationPermitted8[permitIndex] != 0;
        }

        namespace
        {
            constexpr uint8_t HATCH_MASK_SCARLET = 1 << 0;
            constexpr uint8_t HATCH_MASK_VIOLET = 1 << 1;

            // PKHeX EggHatchLocation9.LocationPermitted9. 130 Naranja Academy exists only in Scarlet,
            // 131 Uva Academy only in Violet.
            constexpr std::array<uint8_t, 201> LOCATION_PERMITTED9 = {
                0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 0, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 0, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 0, 3, 0, 3,
                3, 0, 3, 0, 0, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 0, 3,
                0, 3, 0, 3, 0, 3, 0, 3, 0, 3,
                0, 3, 0, 3, 0, 3, 0, 3, 3, 0,
                0, 0, 0, 0, 3, 0, 0, 0, 0, 0,
                1, 2, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                0, 0, 3, 0, 0, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3, 0, 3, 0, 3, 0, 3, 0, 3, 0,
                3,
            };

            bool hasHatchLocationFlag9(uint8_t mask, uint16_t location) noexcept
            {
                if (location >= LOCATION_PERMITTED9.size())
                {
                    return false;
                }
                return (LOCATION_PERMITTED9[location] & mask) != 0;
            }
        }

        namespace
        {
            constexpr uint8_t HATCH_MASK_BRILLIANT_DIAMOND = 1 << 0;
            constexpr uint8_t HATCH_MASK_SHINING_PEARL = 1 << 1;

            // PKHeX EggHatchLocation8b.LocationPermitted8b, indexed by location: bit 0 Brilliant
            // Diamond, bit 1 Shining Pearl.
            constexpr std::array<uint8_t, 658> LOCATION_PERMITTED8B = {
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3,
                3, 3, 3, 0, 3, 3, 3, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 3, 3, 0, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 2, 1, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3,
                3, 3, 3, 0, 0, 3, 3, 3, 3, 3, 3, 0, 0, 0, 3, 0, 3, 2, 1, 3,
                3, 3, 3, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 3,
                0, 0, 0, 0, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1, 3, 3, 3, 3, 1, 2, 3,
            };

            bool hasHatchLocationFlag8b(uint8_t mask, uint16_t location) noexcept
            {
                if (location >= LOCATION_PERMITTED8B.size())
                {
                    return false;
                }
                return (LOCATION_PERMITTED8B[location] & mask) != 0;
            }
        }

        bool isValidMet8BD(uint16_t location) noexcept
        {
            return hasHatchLocationFlag8b(HATCH_MASK_BRILLIANT_DIAMOND, location);
        }

        bool isValidMet8SP(uint16_t location) noexcept
        {
            return hasHatchLocationFlag8b(HATCH_MASK_SHINING_PEARL, location);
        }

        bool isValidMet9SL(uint16_t location) noexcept
        {
            return hasHatchLocationFlag9(HATCH_MASK_SCARLET, location);
        }

        bool isValidMet9VL(uint16_t location) noexcept
        {
            return hasHatchLocationFlag9(HATCH_MASK_VIOLET, location);
        }
    }
}
