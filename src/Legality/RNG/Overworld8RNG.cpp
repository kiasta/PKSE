#include "Legality/RNG/Overworld8RNG.h"

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
            constexpr int NO_MATCH_IVS = -1;
            constexpr int UNSET = -1;
            constexpr int MAXIMUM_IV = 31;

            bool isPIDValid(const Pokemon::Pokemon &pokemon, uint32_t PID, Shiny shiny) noexcept
            {
                if (shiny == Shiny::Random)
                {
                    return PID == pokemon.pid();
                }
                if (PID == pokemon.pid())
                {
                    return true;
                }

                // A forced shiny.
                if (ShinyUtil::isShiny(pokemon))
                {
                    if (ShinyUtil::isShiny6(pokemon.id32(), PID))
                    {
                        return false;
                    }
                    PID = ShinyUtil::getShinyPID(pokemon.tid16(), pokemon.sid16(), PID, 0);
                    return PID == pokemon.pid();
                }

                // A forced non-shiny.
                if (!ShinyUtil::isShiny6(pokemon.id32(), PID))
                {
                    return false;
                }
                PID ^= 0x10000000;
                return PID == pokemon.pid();
            }

            uint32_t getSizeRoll(Xoroshiro128Plus &random) noexcept
            {
                const uint64_t first = random.nextInt(0x81);
                return static_cast<uint32_t>(first + random.nextInt(0x80));
            }

            int getIsMatchEnd(const Pokemon::Pokemon &pokemon, const Xoroshiro128Plus &random, int start = 0, int end = 3) noexcept
            {
                const bool skipOne = start == 0 && end == 3;
                for (int flawlessCount = start; flawlessCount <= end; flawlessCount++)
                {
                    if (skipOne && flawlessCount == 1)
                    {
                        continue;
                    }

                    Xoroshiro128Plus copy = random;
                    int individualValues[6] = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
                    for (int i = 0; i < flawlessCount; i++)
                    {
                        int statIndex = 0;
                        do
                        {
                            statIndex = static_cast<int>(copy.nextInt(6));
                        } while (individualValues[statIndex] != UNSET);
                        individualValues[statIndex] = MAXIMUM_IV;
                    }
                    for (int &individualValue : individualValues)
                    {
                        if (individualValue == UNSET)
                        {
                            individualValue = static_cast<int>(copy.nextInt(MAXIMUM_IV + 1));
                        }
                    }

                    if (individualValues[0] != pokemon.ivHP() || individualValues[1] != pokemon.ivATK() || individualValues[2] != pokemon.ivDEF() || individualValues[3] != pokemon.ivSPA() || individualValues[4] != pokemon.ivSPD() || individualValues[5] != pokemon.ivSPE())
                    {
                        continue;
                    }

                    if (!HomeQuirks::hasSizeScalars(pokemon))
                    {
                        continue;
                    }
                    const uint32_t heightScalar = getSizeRoll(copy);
                    if (pokemon.heightScalar() != heightScalar)
                    {
                        continue;
                    }
                    const uint32_t weight = getSizeRoll(copy);
                    if (pokemon.weightScalar() != weight)
                    {
                        const bool isHomeZeroed = heightScalar == 0 && pokemon.weightScalar() == 0 && HomeQuirks::hasEnteredSetZeroScale(pokemon);
                        if (!isHomeZeroed)
                        {
                            continue;
                        }
                    }
                    return flawlessCount;
                }
                return NO_MATCH_IVS;
            }
        }

        bool Overworld8RNG::validateOverworldEncounter(const Pokemon::Pokemon &pokemon, Shiny shiny, int flawless) noexcept
        {
            return validateOverworldEncounter(pokemon, getOriginalSeed(pokemon), shiny, flawless);
        }

        // PKHeX Overworld8RNG.ValidateOverworldEncounter
        bool Overworld8RNG::validateOverworldEncounter(const Pokemon::Pokemon &pokemon, uint32_t seed, Shiny shiny, int flawless) noexcept
        {
            // A truncated state, not a xoroshiro seed.
            if (seed == 0xFFFFFFFF)
            {
                return false;
            }

            Xoroshiro128Plus random(seed);
            const uint32_t encryptionConstant = static_cast<uint32_t>(random.nextInt(UINT32_MAX));
            if (encryptionConstant != pokemon.encryptionConstant())
            {
                return false;
            }

            const uint32_t PID = static_cast<uint32_t>(random.nextInt(UINT32_MAX));
            if (!isPIDValid(pokemon, PID, shiny))
            {
                return false;
            }

            const int actualCount = flawless == -1 ? getIsMatchEnd(pokemon, random) : getIsMatchEnd(pokemon, random, flawless, flawless);
            return actualCount != NO_MATCH_IVS;
        }

        // PKHeX Overworld8RNG.GetOriginalSeed: the EC is the sum of the two states, and 96 of the 128 seed bits are known.
        uint32_t Overworld8RNG::getOriginalSeed(const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            constexpr uint32_t TWO_SEEDS_ENCRYPTION_CONSTANT = 0xF8572EBE;
            if (encryptionConstant != TWO_SEEDS_ENCRYPTION_CONSTANT)
            {
                return encryptionConstant - static_cast<uint32_t>(Xoroshiro128Plus::XOROSHIRO_CONSTANT);
            }

            // Two seeds give this EC (the second because the first roll is UINT32_MAX and rerolls); the PID decides.
            constexpr uint32_t FIRST_SEED = 0xD5B9C463;
            constexpr uint32_t SECOND_SEED = 0xDD6295A4;
            constexpr uint32_t FIRST_PID = 0x8E72DCF6;
            return pokemon.pid() == FIRST_PID ? FIRST_SEED : SECOND_SEED;
        }
    }
}
