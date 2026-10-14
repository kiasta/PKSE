#include "Pokemon/PIDSearch.h"
#include "Utils/HelperUtilities.h"

namespace Pokemon
{
    namespace
    {
        bool genderIsFixed(uint8_t genderRatio) { return genderRatio == 0 || genderRatio == 254 || genderRatio == 255; }

        /// The requirements the low half alone decides: the gender, and a Gen 3/4 ability bit.
        bool lowHalfFits(const PIDRequirements &requirements, uint32_t low)
        {
            if (requirements.wantGender >= 0 && !genderIsFixed(requirements.genderRatio) && genderFromPID(low, requirements.genderRatio) != requirements.wantGender)
                return false;
            if (requirements.wantAbilityBit >= 0 && requirements.abilityBitIndex >= 0 && requirements.abilityBitIndex < 16 && static_cast<int>((low >> requirements.abilityBitIndex) & 1) != requirements.wantAbilityBit)
                return false;
            return true;
        }

        /// The rest, on the whole PID: the nature, a Gen 5 ability bit and shininess.
        bool wholeFits(const PIDRequirements &requirements, uint32_t candidate)
        {
            if (requirements.wantNature >= 0 && static_cast<int>(candidate % 25) != requirements.wantNature)
                return false;
            if (requirements.wantAbilityBit >= 0 && requirements.abilityBitIndex >= 16 && static_cast<int>((candidate >> requirements.abilityBitIndex) & 1) != requirements.wantAbilityBit)
                return false;
            if (requirements.wantShiny >= 0)
            {
                const uint32_t trainerShinyValue = (requirements.trainerID32 >> 16) ^ (requirements.trainerID32 & 0xFFFF);
                const uint32_t pokemonShinyValue = (candidate >> 16) ^ (candidate & 0xFFFF);
                const bool isShiny = (trainerShinyValue ^ pokemonShinyValue) < requirements.shinyThreshold;
                if (isShiny != (requirements.wantShiny != 0))
                    return false;
            }
            return true;
        }
    }

    uint8_t genderFromPID(uint32_t pidValue, uint8_t genderRatio) noexcept
    {
        if (genderRatio == 255)
            return 2;
        if (genderRatio == 254)
            return 1;
        if (genderRatio == 0)
            return 0;
        return (pidValue & 0xFF) < genderRatio ? 1 : 0;
    }

    bool findPID(const PIDRequirements &requirements, uint32_t &pidOut) noexcept
    {
        const uint32_t trainerShinyValue = (requirements.trainerID32 >> 16) ^ (requirements.trainerID32 & 0xFFFF);
        const uint32_t lowStart = Utils::rand32() & 0xFFFF;
        const uint32_t highStart = Utils::rand32() & 0xFFFF;
        for (uint32_t lowOffset = 0; lowOffset < 0x10000; ++lowOffset)
        {
            const uint32_t low = (lowStart + lowOffset) & 0xFFFF;
            if (!lowHalfFits(requirements, low))
                continue;
            if (requirements.wantShiny == 1)
            {
                // Shiny: the high half is the low half XORed with the trainer's value and something
                // under the threshold, so only those few high halves can work.
                const uint32_t shinyStart = Utils::rand32() % requirements.shinyThreshold;
                for (uint32_t shinyOffset = 0; shinyOffset < requirements.shinyThreshold; ++shinyOffset)
                {
                    const uint32_t shinyValue = (shinyStart + shinyOffset) % requirements.shinyThreshold;
                    const uint32_t candidate = (((low ^ trainerShinyValue ^ shinyValue) & 0xFFFF) << 16) | low;
                    if (wholeFits(requirements, candidate))
                    {
                        pidOut = candidate;
                        return true;
                    }
                }
                continue;
            }
            // Anything else: nearly every high half fits, so a walk finds one within a few dozen steps.
            for (uint32_t highOffset = 0; highOffset < 0x10000; ++highOffset)
            {
                const uint32_t candidate = (((highStart + highOffset) & 0xFFFF) << 16) | low;
                if (wholeFits(requirements, candidate))
                {
                    pidOut = candidate;
                    return true;
                }
            }
        }
        return false;
    }
}
