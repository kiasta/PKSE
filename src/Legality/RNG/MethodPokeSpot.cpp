#include "Legality/RNG/MethodPokeSpot.h"

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/RNG/XDRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        // PKHeX MethodPokeSpot.IsValidActivation. Forward order: origin, rand(3) == 0 activation, rand(100)
        // below 10/30/40 when Munchlax or Bonsly can spawn (skipped otherwise), rand(100) slot, two PID calls.
        PokeSpotSetup MethodPokeSpot::isValidActivation(uint8_t slot, uint32_t seed, uint32_t &origin) noexcept
        {
            origin = 0;
            const uint32_t encounterSlotValue = (seed >> 16) % 100;
            if (!isSlotValid(slot, encounterSlotValue))
            {
                return PokeSpotSetup::Invalid;
            }

            uint32_t preSlot = XDRNG::previous16(seed);
            if (preSlot % 3 == 0)
            {
                origin = XDRNG::previous(seed);
                return PokeSpotSetup::Neither;
            }

            if (preSlot % 100 < 10)
            {
                return PokeSpotSetup::Invalid;
            }
            preSlot = XDRNG::previous16(seed);
            if (preSlot % 3 == 0)
            {
                origin = XDRNG::previous(seed);
                return PokeSpotSetup::Munchlax;
            }
            return PokeSpotSetup::Invalid;
        }

        bool MethodPokeSpot::tryGetOriginSeeds(const Pokemon::Pokemon &pokemon, uint8_t slotNumber, uint8_t levelMin, uint8_t levelMax, uint32_t &PIDOrigin, uint32_t &IVOrigin) noexcept
        {
            IVOrigin = 0;
            if (!tryGetOriginSeedPID(pokemon.pid(), slotNumber, PIDOrigin))
            {
                return false;
            }
            return tryGetOriginSeedIVs(pokemon, levelMin, levelMax, IVOrigin);
        }

        bool MethodPokeSpot::tryGetOriginSeedIVs(const Pokemon::Pokemon &pokemon, uint8_t levelMin, uint8_t levelMax, uint32_t &origin) noexcept
        {
            const uint32_t firstIVs = static_cast<uint32_t>(pokemon.ivHP()) | (static_cast<uint32_t>(pokemon.ivATK()) << 5) | (static_cast<uint32_t>(pokemon.ivDEF()) << 10);
            const uint32_t secondIVs = static_cast<uint32_t>(pokemon.ivSPE()) | (static_cast<uint32_t>(pokemon.ivSPA()) << 5) | (static_cast<uint32_t>(pokemon.ivSPD()) << 10);
            const bool isGeneration3Format = Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup())) == 3;
            return tryGetOriginSeedIVs(firstIVs << 16, secondIVs << 16, levelMin, levelMax, pokemon.metLevel(), isGeneration3Format, origin);
        }

        bool MethodPokeSpot::tryGetOriginSeedPID(uint32_t PID, uint8_t slot, uint32_t &origin) noexcept
        {
            uint32_t seeds[XDRNG::MAX_PID_SEED_COUNT];
            const int seedCount = XDRNG::getSeeds(seeds, PID);
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                if (isValidActivation(slot, seeds[seedIndex], origin) == PokeSpotSetup::Invalid)
                {
                    continue;
                }
                return true;
            }
            origin = 0;
            return false;
        }

        // Frames: origin, two fake trainer id calls, the animation, the level roll, two fake PID calls,
        // then the IV words and the ability.
        bool MethodPokeSpot::tryGetOriginSeedIVs(uint32_t firstIVs, uint32_t secondIVs, uint8_t levelMin, uint8_t levelMax, uint8_t metLevel, bool hasOriginalMetLevel, uint32_t &origin) noexcept
        {
            uint32_t seeds[XDRNG::MAX_IV_SEED_COUNT];
            const int seedCount = XDRNG::getSeedsIVs(seeds, firstIVs, secondIVs);
            const uint32_t levelDelta = 1u + levelMax - levelMin;
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                const uint32_t preIV = seeds[seedIndex];
                origin = XDRNG::previous6(preIV);
                uint32_t animation = 0;
                if (!isValidAnimation(origin, origin, animation))
                {
                    continue;
                }

                const uint32_t levelRandom = XDRNG::previous2(preIV) >> 16;
                const uint8_t level = static_cast<uint8_t>(levelMin + (levelRandom % levelDelta));
                if (hasOriginalMetLevel ? (metLevel != level) : (metLevel < level))
                {
                    continue;
                }
                // The ability bit can be reset on evolution, so it is not checked.
                return true;
            }
            origin = 0;
            return false;
        }

        // PKHeX MethodPokeSpot.IsValidAnimation. The animation is rand(10) until not 3; then 8 takes 5
        // more calls, 5 and up 2, and 4 and below 1. Walked backwards from the call before `seed`.
        bool MethodPokeSpot::isValidAnimation(uint32_t seed, uint32_t &origin, uint32_t &animation) noexcept
        {
            uint32_t previousRandom = XDRNG::previous16(seed);
            animation = previousRandom % 10;
            if (animation < 5 && animation != 3)
            {
                origin = XDRNG::previous(seed);
                return true;
            }

            previousRandom = XDRNG::previous16(seed);
            animation = previousRandom % 10;
            if (animation >= 5 && animation != 8)
            {
                origin = XDRNG::previous(seed);
                return true;
            }

            seed = XDRNG::previous3(seed);
            animation = (seed >> 16) % 10;
            if (animation == 8)
            {
                origin = XDRNG::previous(seed);
                return true;
            }
            origin = 0;
            return false;
        }
    }
}
