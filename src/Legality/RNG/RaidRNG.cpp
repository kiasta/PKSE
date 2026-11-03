#include "Legality/RNG/RaidRNG.h"

#include <algorithm>

#include "Enums/GameVersion.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/ToxtricityUtil.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr int UNSET = -1;
            constexpr int MAXIMUM_IV = 31;
            constexpr uint16_t TOXTRICITY_SPECIES = 849;
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;

            bool isGeneration8Context(Enums::GameVersion group) noexcept
            {
                return group == Enums::GameVersion::SWSH || group == Enums::GameVersion::PLA || group == Enums::GameVersion::BDSP;
            }
        }

        // PKHeX RaidRNG.Verify
        bool RaidRNG::verify(const Pokemon::Pokemon &pokemon, uint64_t seed, std::span<int> individualValues, const GenerateParam8 &parameters, bool forceNoShiny) noexcept
        {
            Xoroshiro128Plus random(seed);
            const uint32_t encryptionConstant = static_cast<uint32_t>(random.nextInt());
            if (encryptionConstant != pokemon.encryptionConstant())
            {
                return false;
            }

            uint32_t PID = 0;
            bool isShiny = false;
            {
                const uint32_t fakeTrainerID = static_cast<uint32_t>(random.nextInt());
                PID = static_cast<uint32_t>(random.nextInt());
                const uint32_t shinyXor = ShinyUtil::getShinyXor(PID, fakeTrainerID);
                isShiny = shinyXor < 16;
                if (isShiny && forceNoShiny)
                {
                    ShinyUtil::forceShinyState(false, PID, fakeTrainerID, 0);
                    isShiny = false;
                }
            }
            ShinyUtil::forceShinyState(isShiny, PID, pokemon.id32(), 0);
            if (pokemon.pid() != PID)
            {
                return false;
            }

            if (parameters.individualValues.isSpecified)
            {
                parameters.individualValues.copyToSpeedLast(individualValues);
            }
            else
            {
                std::fill(individualValues.begin(), individualValues.end(), UNSET);
            }

            for (int i = static_cast<int>(std::count(individualValues.begin(), individualValues.end(), MAXIMUM_IV)); i < parameters.flawlessIVs; i++)
            {
                int statIndex = static_cast<int>(random.nextInt(6));
                while (individualValues[statIndex] != UNSET)
                {
                    statIndex = static_cast<int>(random.nextInt(6));
                }
                individualValues[statIndex] = MAXIMUM_IV;
            }
            for (int i = 0; i < 6; i++)
            {
                if (individualValues[i] == UNSET)
                {
                    individualValues[i] = static_cast<int>(random.nextInt(32));
                }
            }

            if (pokemon.ivHP() != individualValues[0] || pokemon.ivATK() != individualValues[1] || pokemon.ivDEF() != individualValues[2] || pokemon.ivSPA() != individualValues[3] || pokemon.ivSPD() != individualValues[4] || pokemon.ivSPE() != individualValues[5])
            {
                return false;
            }

            int ability = 0;
            if (parameters.ability == AbilityPermission::Any12H)
            {
                ability = static_cast<int>(random.nextInt(3));
            }
            else if (parameters.ability == AbilityPermission::Any12)
            {
                ability = static_cast<int>(random.nextInt(2));
            }
            else
            {
                ability = getSingleValue(parameters.ability);
            }
            ability <<= 1;
            // A Hidden Ability any other way is the ability checks' business (Ability Patch).
            if (ability == 4)
            {
                if (pokemon.abilityNumber() != 4 && pokemon.getGameGroup() == Enums::GameVersion::SWSH)
                {
                    return false;
                }
            }

            switch (parameters.genderRatio)
            {
            case RATIO_MAGIC_GENDERLESS:
                if (pokemon.gender() != 2)
                {
                    return false;
                }
                break;
            case RATIO_MAGIC_FEMALE:
                if (pokemon.gender() != 1)
                {
                    return false;
                }
                break;
            case RATIO_MAGIC_MALE:
                if (pokemon.gender() != 0)
                {
                    return false;
                }
                break;
            default:
            {
                const int gender = static_cast<int>(random.nextInt(253)) + 1 < parameters.genderRatio ? 1 : 0;
                // Nincada (0/1) evolving into Shedinja (2) is allowed.
                if (pokemon.gender() != gender && pokemon.gender() != 2)
                {
                    return false;
                }
                break;
            }
            }

            uint8_t nature = parameters.nature;
            if (nature >= RANDOM_NATURE)
            {
                nature = parameters.species == TOXTRICITY_SPECIES ? ToxtricityUtil::getRandomNature(random, pokemon.form()) : static_cast<uint8_t>(random.nextInt(25));
            }
            if (pokemon.nature() != nature)
            {
                return false;
            }

            if (HomeQuirks::hasSizeScalars(pokemon))
            {
                const uint64_t heightFirst = random.nextInt(0x81);
                const uint64_t heightScalar = heightFirst + random.nextInt(0x80);
                const uint64_t weightFirst = random.nextInt(0x81);
                const uint64_t weight = weightFirst + random.nextInt(0x80);
                if (heightScalar == 0 && weight == 0 && pokemon.hasHomeTracker() && pokemon.homeTracker() != 0)
                {
                    // HOME 3.0.0 rerolls a 0/0 size, so only a Gen 9 or later format shows it.
                    if (!isGeneration8Context(pokemon.getGameGroup()))
                    {
                        return false;
                    }
                }
                else
                {
                    if (pokemon.heightScalar() != heightScalar)
                    {
                        return false;
                    }
                    if (pokemon.weightScalar() != weight)
                    {
                        const bool isHomeZeroed = heightScalar == 0 && pokemon.weightScalar() == 0 && HomeQuirks::hasEnteredSetZeroScale(pokemon);
                        if (!isHomeZeroed)
                        {
                            return false;
                        }
                    }
                }
            }
            return true;
        }
    }
}
