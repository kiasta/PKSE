#include "Legality/RNG/Roaming8bRNG.h"

#include "Enums/GameVersion.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/Xoroshiro128Plus8b.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr int UNSET = -1;
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;
            constexpr uint8_t GENDER_MALE = 0;
            constexpr uint8_t GENDER_FEMALE = 1;
            constexpr uint8_t GENDER_GENDERLESS = 2;
            // Unity's Random.Range is [int.MinValue, int.MaxValue), so this seed is never rolled.
            constexpr uint32_t UNITY_EXCLUDED_SEED = 0x7FFFFFFF;

            enum class RareType
            {
                Never,
                Star,
                Square,
            };

            RareType getRareType(uint32_t shinyXor) noexcept
            {
                if (shinyXor == 0)
                {
                    return RareType::Square;
                }
                return shinyXor < 16 ? RareType::Star : RareType::Never;
            }

            // The fake trainer's shininess carries over to the real trainer, keeping a square a square.
            uint32_t getRevisedPID(uint32_t fakeTrainerID, uint32_t PID, const Pokemon::Pokemon &pokemon) noexcept
            {
                const uint32_t shinyXor = ShinyUtil::getShinyXor(PID, fakeTrainerID);
                const uint32_t newShinyXor = ShinyUtil::getShinyXor(PID, pokemon.id32());
                if (getRareType(shinyXor) == getRareType(newShinyXor))
                {
                    return PID;
                }
                if (shinyXor >= 16)
                {
                    return PID ^ 0x10000000;
                }
                const uint32_t low = PID & 0xFFFF;
                return (((shinyXor == 0 ? 0u : 1u) ^ pokemon.tid16() ^ pokemon.sid16() ^ low) << 16) | low;
            }

            bool isHeightWeightMatch(const Pokemon::Pokemon &pokemon, Xoroshiro128Plus8b random) noexcept
            {
                if (!HomeQuirks::hasSizeScalars(pokemon))
                {
                    return false;
                }
                const uint32_t heightFirst = random.nextUInt(0x81);
                const uint32_t heightScalar = heightFirst + random.nextUInt(0x80);
                const uint32_t weightFirst = random.nextUInt(0x81);
                const uint32_t weight = weightFirst + random.nextUInt(0x80);
                return pokemon.heightScalar() == heightScalar && pokemon.weightScalar() == weight;
            }

            bool isGenderMatch(const Pokemon::Pokemon &pokemon, Xoroshiro128Plus8b &random) noexcept
            {
                const uint8_t genderRatio = Pokemon::getPersonalRecord(Enums::GameVersion::BDSP, pokemon.speciesID(), pokemon.form()).genderRatio;
                if (genderRatio == RATIO_MAGIC_GENDERLESS)
                {
                    return pokemon.gender() == GENDER_GENDERLESS;
                }
                if (genderRatio == RATIO_MAGIC_MALE)
                {
                    return pokemon.gender() == GENDER_MALE;
                }
                if (genderRatio == RATIO_MAGIC_FEMALE)
                {
                    return pokemon.gender() == GENDER_FEMALE;
                }
                return pokemon.gender() == ((static_cast<int>(random.nextUInt(253)) + 1 < genderRatio) ? 1 : 0);
            }

            bool isMatchEnd(const Pokemon::Pokemon &pokemon, Xoroshiro128Plus8b random) noexcept
            {
                if (!isGenderMatch(pokemon, random))
                {
                    return false;
                }
                if (pokemon.nature() != random.nextUInt(25))
                {
                    return false;
                }
                return isHeightWeightMatch(pokemon, random);
            }

            // Cute Charm forced the gender, so the gender roll is not made.
            bool isMatchEndWithCuteCharm(const Pokemon::Pokemon &pokemon, Xoroshiro128Plus8b random) noexcept
            {
                if (pokemon.nature() != random.nextUInt(25))
                {
                    return false;
                }
                return isHeightWeightMatch(pokemon, random);
            }

            // Synchronize forced the nature, so the nature roll is not made.
            bool isMatchEndWithSynchronize(const Pokemon::Pokemon &pokemon, Xoroshiro128Plus8b random) noexcept
            {
                if (!isGenderMatch(pokemon, random))
                {
                    return false;
                }
                return isHeightWeightMatch(pokemon, random);
            }
        }

        // PKHeX Roaming8bRNG.ValidateRoamingEncounter
        bool Roaming8bRNG::validateRoamingEncounter(const Pokemon::Pokemon &pokemon, Shiny, int flawless) noexcept
        {
            const uint32_t seed = pokemon.encryptionConstant();
            if (seed == UNITY_EXCLUDED_SEED)
            {
                return false;
            }
            Xoroshiro128Plus8b random(seed);

            const uint32_t fakeTrainerID = random.nextUInt();
            uint32_t PID = random.nextUInt();
            PID = getRevisedPID(fakeTrainerID, PID, pokemon);
            if (pokemon.pid() != PID)
            {
                return false;
            }

            // Perfect IVs at random positions first, then the rest.
            int individualValues[6] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
            int determined = 0;
            while (determined < flawless)
            {
                const int statIndex = static_cast<int>(random.nextUInt(6));
                if (individualValues[statIndex] != UNSET)
                {
                    continue;
                }
                individualValues[statIndex] = 31;
                determined++;
            }
            for (int &individualValue : individualValues)
            {
                if (individualValue == UNSET)
                {
                    individualValue = static_cast<int>(random.nextUInt(31 + 1));
                }
            }
            if (individualValues[0] != pokemon.ivHP() || individualValues[1] != pokemon.ivATK() || individualValues[2] != pokemon.ivDEF() || individualValues[3] != pokemon.ivSPA() || individualValues[4] != pokemon.ivSPD() || individualValues[5] != pokemon.ivSPE())
            {
                return false;
            }

            // Roamers only have abilities 1 and 2.
            if (pokemon.abilityNumber() != (1 << static_cast<int>(random.nextUInt(2))))
            {
                return false;
            }

            return isMatchEnd(pokemon, random) || isMatchEndWithCuteCharm(pokemon, random) || isMatchEndWithSynchronize(pokemon, random);
        }
    }
}
