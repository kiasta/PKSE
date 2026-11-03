#include "Legality/RNG/LumioseRNG.h"

#include <bit>

#include "Legality/RNG/Encounter9RNG.h"
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
            constexpr int STAT_COUNT = 6;
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;

            // PKHeX LumioseRNG.GetAdaptedPID
            uint32_t getAdaptedPID(Xoroshiro128Plus &random, const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters) noexcept
            {
                const uint32_t fakeTrainerID = parameters.correlation == LumioseCorrelation::SkipTrainer ? pokemon.id32() : static_cast<uint32_t>(random.nextInt());
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
                    if (!ShinyUtil::isShiny6(fakeTrainerID, PID))
                    {
                        PID = ShinyUtil::getShinyPID(static_cast<uint16_t>(fakeTrainerID), static_cast<uint16_t>(fakeTrainerID >> 16), PID, 0);
                    }
                    if (!ShinyUtil::isShiny6(pokemon.id32(), PID))
                    {
                        PID = ShinyUtil::getShinyPID(pokemon.tid16(), pokemon.sid16(), PID, ShinyUtil::getShinyXor(PID, fakeTrainerID) == 0 ? 0u : 1u);
                    }
                }
                else
                {
                    // Every other shiny state is generated as never-shiny, as PKHeX does.
                    if (ShinyUtil::isShiny6(fakeTrainerID, PID))
                    {
                        PID = LumioseRNG::antiShiny(PID);
                    }
                    if (ShinyUtil::isShiny6(pokemon.id32(), PID))
                    {
                        PID = LumioseRNG::antiShiny(PID);
                    }
                }
                return PID;
            }

            // PKHeX LumioseRNG.IsMatchIVsAndFollowing(pk, enc, rand, ivs)
            bool isMatchIVsAndFollowing(const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters, Xoroshiro128Plus random, int (&individualValues)[STAT_COUNT]) noexcept
            {
                for (int &individualValue : individualValues)
                {
                    if (individualValue == UNSET)
                    {
                        individualValue = static_cast<int>(random.nextInt(32));
                    }
                }
                if (parameters.correlation != LumioseCorrelation::ReApplyIVs)
                {
                    if (pokemon.ivHP() != individualValues[0] || pokemon.ivATK() != individualValues[1] || pokemon.ivDEF() != individualValues[2] || pokemon.ivSPA() != individualValues[3] || pokemon.ivSPD() != individualValues[4] || pokemon.ivSPE() != individualValues[5])
                    {
                        return false;
                    }
                }

                // No way to change abilities: the index must match.
                int abilityNumber = static_cast<int>(parameters.ability);
                if (parameters.ability == AbilityPermission::Any12H)
                {
                    abilityNumber = 1 << static_cast<int>(random.nextInt(3));
                }
                else if (parameters.ability == AbilityPermission::Any12)
                {
                    abilityNumber = 1 << static_cast<int>(random.nextInt(2));
                }
                if (pokemon.abilityNumber() != abilityNumber)
                {
                    return false;
                }

                uint8_t gender = 0;
                if (parameters.genderRatio == RATIO_MAGIC_GENDERLESS)
                {
                    gender = 2;
                }
                else if (parameters.genderRatio == RATIO_MAGIC_FEMALE)
                {
                    gender = 1;
                }
                else if (parameters.genderRatio != RATIO_MAGIC_MALE)
                {
                    gender = Encounter9RNG::getGender(parameters.genderRatio, random.nextInt(100));
                }
                if (pokemon.gender() != gender)
                {
                    return false;
                }

                const uint8_t nature = parameters.nature < RANDOM_NATURE ? parameters.nature : static_cast<uint8_t>(random.nextInt(25));
                if (pokemon.nature() != nature)
                {
                    return false;
                }

                // A Hyperspace size boost is decided outside the seed; such encounters pass SizeType9::VALUE.
                const uint8_t scale = getSizeValue(parameters.sizeType, parameters.scale, random);
                if (pokemon.hasScale() && pokemon.scale() != scale)
                {
                    return false;
                }
                return true;
            }

            bool isMatchIVsAndFollowingFlawlessBits(const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters, const Xoroshiro128Plus &random, int flawlessBits) noexcept
            {
                int individualValues[STAT_COUNT] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
                for (int statIndex = 0; statIndex < STAT_COUNT; statIndex++)
                {
                    if ((flawlessBits & (1 << statIndex)) != 0)
                    {
                        individualValues[statIndex] = MAXIMUM_IV;
                    }
                }
                return isMatchIVsAndFollowing(pokemon, parameters, random, individualValues);
            }

            /// PKHeX GetBitmaskFlawlessIVs, in HP/Atk/Def/SpA/SpD/Spe order.
            int getFlawlessIVBits(const Pokemon::Pokemon &pokemon) noexcept
            {
                const int statValues[STAT_COUNT] = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPA(), pokemon.ivSPD(), pokemon.ivSPE()};
                int flawlessBits = 0;
                for (int statIndex = 0; statIndex < STAT_COUNT; statIndex++)
                {
                    if (statValues[statIndex] == MAXIMUM_IV)
                    {
                        flawlessBits |= 1 << statIndex;
                    }
                }
                return flawlessBits;
            }

            // PKHeX LumioseRNG.IsMatchUnknownPreFillIVs: a separate seed chose the flawless IVs, so try
            // every choice of `flawlessCount` stats the record has at 31 (Gosper's hack).
            bool isMatchUnknownPreFillIVs(const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters, const Xoroshiro128Plus &random) noexcept
            {
                const int flawlessCount = parameters.flawlessIVs;
                if (flawlessCount == 0)
                {
                    return isMatchIVsAndFollowingFlawlessBits(pokemon, parameters, random, 0);
                }
                if (flawlessCount == STAT_COUNT)
                {
                    return isMatchIVsAndFollowingFlawlessBits(pokemon, parameters, random, (1 << STAT_COUNT) - 1);
                }
                const int permittedBits = getFlawlessIVBits(pokemon);
                if (std::popcount(static_cast<unsigned int>(permittedBits)) == flawlessCount)
                {
                    return isMatchIVsAndFollowingFlawlessBits(pokemon, parameters, random, permittedBits);
                }
                constexpr int combinationLimit = 1 << STAT_COUNT;
                int combination = (1 << flawlessCount) - 1;
                while (true)
                {
                    if ((combination & permittedBits) == combination && isMatchIVsAndFollowingFlawlessBits(pokemon, parameters, random, combination))
                    {
                        return true;
                    }
                    const int lowestBit = combination & -combination;
                    const int rippled = combination + lowestBit;
                    if (rippled >= combinationLimit || rippled == 0)
                    {
                        break;
                    }
                    combination = rippled + (((rippled ^ combination) / lowestBit) >> 2);
                }
                return false;
            }
        }

        bool LumioseRNG::isMatch(const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters, uint64_t seed) noexcept
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
            if (parameters.correlation == LumioseCorrelation::PreApplyIVs)
            {
                return isMatchUnknownPreFillIVs(pokemon, parameters, random);
            }

            int individualValues[STAT_COUNT] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
            if (parameters.individualValues.isSpecified)
            {
                parameters.individualValues.copyToSpeedLast(individualValues);
            }
            for (int flawlessIndex = 0; flawlessIndex < parameters.flawlessIVs; flawlessIndex++)
            {
                int statIndex = 0;
                do
                {
                    statIndex = static_cast<int>(random.nextInt(6));
                } while (individualValues[statIndex] != UNSET);
                individualValues[statIndex] = MAXIMUM_IV;
            }
            return isMatchIVsAndFollowing(pokemon, parameters, random, individualValues);
        }
    }
}
