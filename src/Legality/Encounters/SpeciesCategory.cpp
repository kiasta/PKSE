#include "Legality/Encounters/SpeciesCategory.h"

#include <algorithm>
#include <array>

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // clang-format off
            constexpr std::array<uint16_t, 23> MYTHICAL_SPECIES = {151, 251, 385, 386, 489, 490, 491, 492, 493, 494, 647, 648, 649, 719, 720, 721, 801, 802, 807, 808, 809, 893, 1025};
            constexpr std::array<uint16_t, 27> LEGENDARY_SPECIES = {150, 249, 250, 382, 383, 384, 483, 484, 487, 643, 644, 646, 716, 717, 718, 789, 790, 791, 792, 800, 888, 889, 890, 898, 1007, 1008, 1024};
            constexpr std::array<uint16_t, 44> SUB_LEGENDARY_SPECIES = {144, 145, 146, 243, 244, 245, 377, 378, 379, 380, 381, 480, 481, 482, 485, 486, 488, 638, 639, 640, 641, 642, 645, 772, 773, 785, 786, 787, 788, 891, 892, 894, 895, 896, 897, 905, 1001, 1002, 1003, 1004, 1014, 1015, 1016, 1017};
            // clang-format on
            constexpr uint16_t NIHILEGO_SPECIES = 793;
            constexpr uint16_t GUZZLORD_SPECIES = 799;
            constexpr uint16_t POIPOLE_SPECIES = 803;
            constexpr uint16_t BLACEPHALON_SPECIES = 806;
            constexpr uint16_t GREAT_TUSK_SPECIES = 984;
            constexpr uint16_t IRON_THORNS_SPECIES = 995;
            constexpr uint16_t ROARING_MOON_SPECIES = 1005;
            constexpr uint16_t IRON_VALIANT_SPECIES = 1006;
            constexpr uint16_t WALKING_WAKE_SPECIES = 1009;
            constexpr uint16_t IRON_LEAVES_SPECIES = 1010;
            constexpr uint16_t GOUGING_FIRE_SPECIES = 1020;
            constexpr uint16_t IRON_CROWN_SPECIES = 1023;

            template <size_t SpeciesCount>
            bool containsSpecies(const std::array<uint16_t, SpeciesCount> &speciesList, uint16_t speciesId) noexcept
            {
                return std::find(speciesList.begin(), speciesList.end(), speciesId) != speciesList.end();
            }
        }

        bool SpeciesCategory::isMythical(uint16_t speciesId) noexcept
        {
            return containsSpecies(MYTHICAL_SPECIES, speciesId);
        }

        bool SpeciesCategory::isLegendary(uint16_t speciesId) noexcept
        {
            return containsSpecies(LEGENDARY_SPECIES, speciesId);
        }

        bool SpeciesCategory::isSubLegendary(uint16_t speciesId) noexcept
        {
            return containsSpecies(SUB_LEGENDARY_SPECIES, speciesId);
        }

        bool SpeciesCategory::isUltraBeast(uint16_t speciesId) noexcept
        {
            return (speciesId >= NIHILEGO_SPECIES && speciesId <= GUZZLORD_SPECIES) || (speciesId >= POIPOLE_SPECIES && speciesId <= BLACEPHALON_SPECIES);
        }

        bool SpeciesCategory::isParadox(uint16_t speciesId) noexcept
        {
            // Gouging Fire through Iron Crown are consecutive: Gouging Fire, Raging Bolt, Iron Boulder, Iron Crown.
            return (speciesId >= GREAT_TUSK_SPECIES && speciesId <= IRON_THORNS_SPECIES) || speciesId == ROARING_MOON_SPECIES || speciesId == IRON_VALIANT_SPECIES || speciesId == WALKING_WAKE_SPECIES || speciesId == IRON_LEAVES_SPECIES || (speciesId >= GOUGING_FIRE_SPECIES && speciesId <= IRON_CROWN_SPECIES);
        }

        bool SpeciesCategory::isSpecialPokemon(uint16_t speciesId) noexcept
        {
            return isLegendary(speciesId) || isSubLegendary(speciesId) || isMythical(speciesId) || isUltraBeast(speciesId) || isParadox(speciesId);
        }
    }
}
