#include "Legality/RNG/Overworld8aRNG.h"

#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/ShinyUtil.h"
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
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;
        }

        // PKHeX Overworld8aRNG.Verify
        bool Overworld8aRNG::verify(const Pokemon::Pokemon &pokemon, uint64_t seed, const OverworldParam8a &parameters, bool isFixedHeight, bool isFixedWeight) noexcept
        {
            Xoroshiro128Plus random(seed);
            const uint32_t encryptionConstant = static_cast<uint32_t>(random.nextInt());
            if (encryptionConstant != pokemon.encryptionConstant())
            {
                return false;
            }

            const uint32_t fakeTrainerID = static_cast<uint32_t>(random.nextInt());
            uint32_t PID = static_cast<uint32_t>(random.nextInt());
            bool isShiny = false;
            if (parameters.shiny == Shiny::Random)
            {
                for (int i = 1; i < parameters.rollCount; i++)
                {
                    isShiny = ShinyUtil::getShinyXor(PID, fakeTrainerID) < 16;
                    if (isShiny)
                    {
                        break;
                    }
                    PID = static_cast<uint32_t>(random.nextInt());
                }
            }
            else
            {
                isShiny = parameters.shiny == Shiny::Always;
            }

            ShinyUtil::forceShinyState(isShiny, PID, pokemon.id32(), 0);
            if (pokemon.pid() != PID)
            {
                return false;
            }

            int individualValues[6] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
            for (int i = 0; i < parameters.flawlessIVs; i++)
            {
                int statIndex = 0;
                do
                {
                    statIndex = static_cast<int>(random.nextInt(6));
                } while (individualValues[statIndex] != UNSET);
                individualValues[statIndex] = MAXIMUM_IV;
            }
            for (int &individualValue : individualValues)
            {
                if (individualValue == UNSET)
                {
                    individualValue = static_cast<int>(random.nextInt(32));
                }
            }
            if (pokemon.ivHP() != individualValues[0] || pokemon.ivATK() != individualValues[1] || pokemon.ivDEF() != individualValues[2] || pokemon.ivSPA() != individualValues[3] || pokemon.ivSPD() != individualValues[4] || pokemon.ivSPE() != individualValues[5])
            {
                return false;
            }

            // A Hidden Ability any other way is the ability checks' business (Ability Patch).
            const int ability = static_cast<int>(random.nextInt(2)) << 1;
            if (ability == 4 && pokemon.abilityNumber() != 4)
            {
                return false;
            }

            uint8_t gender = 0;
            switch (parameters.genderRatio)
            {
            case RATIO_MAGIC_GENDERLESS:
                gender = 2;
                break;
            case RATIO_MAGIC_FEMALE:
                gender = 1;
                break;
            case RATIO_MAGIC_MALE:
                gender = 0;
                break;
            default:
                gender = random.nextInt(252) + 1 < parameters.genderRatio ? 1 : 0;
                break;
            }
            if (pokemon.gender() != gender)
            {
                return false;
            }

            const uint8_t nature = static_cast<uint8_t>(random.nextInt(25));
            if (pokemon.nature() != nature)
            {
                return false;
            }

            uint8_t heightScalar = 0xFF;
            uint8_t weight = 0xFF;
            if (!parameters.isAlpha)
            {
                const uint64_t heightFirst = random.nextInt(0x81);
                heightScalar = static_cast<uint8_t>(heightFirst + random.nextInt(0x80));
                const uint64_t weightFirst = random.nextInt(0x81);
                weight = static_cast<uint8_t>(weightFirst + random.nextInt(0x80));
            }
            if (HomeQuirks::hasSizeScalars(pokemon))
            {
                if (!isFixedHeight && pokemon.heightScalar() != heightScalar)
                {
                    return false;
                }
                if (!isFixedWeight && pokemon.weightScalar() != weight)
                {
                    const bool isHomeZeroed = heightScalar == 0 && pokemon.weightScalar() == 0 && HomeQuirks::hasEnteredSetZeroScale(pokemon);
                    if (!isHomeZeroed)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        uint8_t Overworld8aRNG::getRandomLevel(uint64_t slotSeed, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            const uint64_t delta = 1ull + levelMax - levelMin;
            Xoroshiro128Plus random(slotSeed);
            // The slot roll, then the entity seed.
            random.next();
            random.next();
            const uint8_t amplitude = static_cast<uint8_t>(random.nextInt(delta));
            return static_cast<uint8_t>(levelMin + amplitude);
        }
    }
}
