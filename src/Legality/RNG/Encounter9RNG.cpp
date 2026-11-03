#include "Legality/RNG/Encounter9RNG.h"

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
            // PKHeX EntityGender VM, MM, HH, MF, VF.
            constexpr uint8_t RATIO_VERY_MALE = 31;
            constexpr uint8_t RATIO_MOSTLY_MALE = 63;
            constexpr uint8_t RATIO_HALF = 127;
            constexpr uint8_t RATIO_MOSTLY_FEMALE = 191;
            constexpr uint8_t RATIO_VERY_FEMALE = 225;

            uint32_t getAdaptedPID(Xoroshiro128Plus &random, const Pokemon::Pokemon &pokemon, const GenerateParam9 &parameters) noexcept
            {
                const uint32_t fakeTrainerID = static_cast<uint32_t>(random.nextInt());
                uint32_t PID = static_cast<uint32_t>(random.nextInt());
                if (parameters.shiny == Shiny::Random)
                {
                    int rollIndex = 1;
                    bool isShiny = false;
                    uint32_t shinyXor = 0;
                    while (true)
                    {
                        shinyXor = ShinyUtil::getShinyXor(PID, fakeTrainerID);
                        isShiny = shinyXor < 16;
                        if (isShiny)
                        {
                            if (shinyXor != 0)
                            {
                                shinyXor = 1;
                            }
                            break;
                        }
                        if (rollIndex >= parameters.rollCount)
                        {
                            break;
                        }
                        PID = static_cast<uint32_t>(random.nextInt());
                        rollIndex++;
                    }
                    ShinyUtil::forceShinyState(isShiny, PID, pokemon.id32(), shinyXor);
                }
                else if (parameters.shiny == Shiny::Always)
                {
                    const uint16_t fakeTID16 = static_cast<uint16_t>(fakeTrainerID);
                    const uint16_t fakeSID16 = static_cast<uint16_t>(fakeTrainerID >> 16);
                    // Shiny when battled, then shiny for the trainer who caught it.
                    if (!ShinyUtil::isShiny6(fakeTrainerID, PID))
                    {
                        PID = ShinyUtil::getShinyPID(fakeTID16, fakeSID16, PID, 0);
                    }
                    if (!ShinyUtil::isShiny6(pokemon.id32(), PID))
                    {
                        PID = ShinyUtil::getShinyPID(pokemon.tid16(), pokemon.sid16(), PID, ShinyUtil::getShinyXor(PID, fakeTrainerID) == 0 ? 0u : 1u);
                    }
                }
                else
                {
                    if (ShinyUtil::isShiny6(fakeTrainerID, PID))
                    {
                        PID ^= 0x10000000;
                    }
                    if (ShinyUtil::isShiny6(pokemon.id32(), PID))
                    {
                        PID ^= 0x10000000;
                    }
                }
                return PID;
            }
        }

        // PKHeX Encounter9RNG.IsMatch
        bool Encounter9RNG::isMatch(const Pokemon::Pokemon &pokemon, const GenerateParam9 &parameters, uint64_t seed) noexcept
        {
            Xoroshiro128Plus random(seed);
            if (pokemon.encryptionConstant() != static_cast<uint32_t>(random.nextInt(UINT32_MAX)))
            {
                return false;
            }

            const uint32_t PID = getAdaptedPID(random, pokemon, parameters);
            if (pokemon.pid() != PID)
            {
                return false;
            }

            int individualValues[6] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
            if (parameters.individualValues.isSpecified)
            {
                parameters.individualValues.copyToSpeedLast(individualValues);
            }
            else
            {
                for (int i = 0; i < parameters.flawlessIVs; i++)
                {
                    int statIndex = 0;
                    do
                    {
                        statIndex = static_cast<int>(random.nextInt(6));
                    } while (individualValues[statIndex] != UNSET);
                    individualValues[statIndex] = MAXIMUM_IV;
                }
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

            // The ability roll is consumed but not compared: an Ability Capsule/Patch can change it.
            if (parameters.ability == AbilityPermission::Any12H)
            {
                random.nextInt(3);
            }
            else if (parameters.ability == AbilityPermission::Any12)
            {
                random.nextInt(2);
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
                gender = getGender(parameters.genderRatio, random.nextInt(100));
                break;
            }
            if (pokemon.gender() != gender)
            {
                return false;
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

            if (parameters.heightScalar == 0)
            {
                const uint64_t first = random.nextInt(0x81);
                const uint8_t heightScalar = static_cast<uint8_t>(first + random.nextInt(0x80));
                if (!isHeightMatchSV(pokemon, heightScalar))
                {
                    return false;
                }
            }
            if (parameters.weightScalar == 0)
            {
                const uint64_t first = random.nextInt(0x81);
                const uint8_t weightScalar = static_cast<uint8_t>(first + random.nextInt(0x80));
                if (HomeQuirks::hasSizeScalars(pokemon))
                {
                    const uint8_t actual = pokemon.weightScalar();
                    if (actual != weightScalar)
                    {
                        const bool isHomeZeroed = actual != 0 && pokemon.heightScalar() == 0 && HomeQuirks::hasEnteredSetZeroScale(pokemon);
                        if (!isHomeZeroed)
                        {
                            return false;
                        }
                    }
                }
            }
            {
                const uint8_t scaleValue = getSizeValue(parameters.scaleType, parameters.scale, random);
                if (pokemon.hasScale())
                {
                    if (pokemon.scale() != scaleValue)
                    {
                        return false;
                    }
                }
                else if (HomeQuirks::hasSizeScalars(pokemon))
                {
                    if (pokemon.heightScalar() != scaleValue)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool Encounter9RNG::isHeightMatchSV(const Pokemon::Pokemon &pokemon, uint8_t heightScalar) noexcept
        {
            if (!HomeQuirks::hasSizeScalars(pokemon) || !pokemon.hasScale())
            {
                return true;
            }
            return HomeQuirks::isTouchedScaleCopiedOrUntouched(pokemon, heightScalar);
        }

        uint8_t Encounter9RNG::getGender(uint8_t ratio, uint64_t random100) noexcept
        {
            switch (ratio)
            {
            case RATIO_VERY_MALE:
                return random100 < 12 ? 1 : 0;
            case RATIO_MOSTLY_MALE:
                return random100 < 25 ? 1 : 0;
            case RATIO_HALF:
                return random100 < 50 ? 1 : 0;
            case RATIO_MOSTLY_FEMALE:
                return random100 < 75 ? 1 : 0;
            case RATIO_VERY_FEMALE:
                return random100 < 89 ? 1 : 0;
            default:
                return 0;
            }
        }
    }
}
