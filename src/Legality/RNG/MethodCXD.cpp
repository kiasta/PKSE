#include "Legality/RNG/MethodCXD.h"

#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/XDRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint16_t UMBREON_SPECIES = 197;
            // PKHeX EntityGender.VM: the 87.5% male ratio.
            constexpr uint32_t GENDER_RATIO_VERY_MALE = 0x1F;
            constexpr uint16_t NAME_SCREEN_SEED_THRESHOLD = 0x1999;

            uint32_t getPIDRegular(uint32_t &seed) noexcept
            {
                const uint32_t highHalf = XDRNG::next16(seed);
                const uint32_t lowHalf = XDRNG::next16(seed);
                return (highHalf << 16) | lowHalf;
            }

            bool isMaleEevee(uint32_t PID) noexcept
            {
                return (PID & 0xFF) >= GENDER_RATIO_VERY_MALE;
            }

            uint32_t getColoStarterPID(uint32_t &seed, uint32_t ID32) noexcept
            {
                while (true)
                {
                    const uint32_t PID = getPIDRegular(seed);
                    if (isMaleEevee(PID) && !ShinyUtil::isShiny3(ID32, PID))
                    {
                        return PID;
                    }
                }
            }

            bool isMatchIVs(uint32_t firstIVs, uint32_t secondIVs, uint32_t seed) noexcept
            {
                if (firstIVs != XDRNG::next15(seed))
                {
                    return false;
                }
                return secondIVs == XDRNG::next15(seed);
            }

            uint32_t getPackedIVs(const Pokemon::Pokemon &pokemon) noexcept
            {
                return static_cast<uint32_t>(pokemon.ivHP()) | (static_cast<uint32_t>(pokemon.ivATK()) << 5) | (static_cast<uint32_t>(pokemon.ivDEF()) << 10) | (static_cast<uint32_t>(pokemon.ivSPE()) << 15) | (static_cast<uint32_t>(pokemon.ivSPA()) << 20) | (static_cast<uint32_t>(pokemon.ivSPD()) << 25);
            }
        }

        uint32_t MethodCXD::getColoStarterPID(uint32_t &seed, uint16_t TID16, uint16_t SID16) noexcept
        {
            const uint32_t ID32 = (static_cast<uint32_t>(SID16) << 16) | TID16;
            return RNG::getColoStarterPID(seed, ID32);
        }

        // PKHeX PKHeX.Core/Legality/RNG/CXD/MethodCXD.cs TryGetSeedStarterColo: TID, SID, two fake
        // PID calls, then each starter's IVs, ability and PID.
        bool MethodCXD::tryGetSeedStarterColo(uint32_t firstIVs, uint32_t secondIVs, uint32_t expectedPID, uint16_t TID16, uint16_t SID16, uint16_t speciesId, uint32_t &result) noexcept
        {
            const uint32_t ID32 = (static_cast<uint32_t>(SID16) << 16) | TID16;
            uint32_t seeds[XDRNG::MAX_PID_SEED_COUNT];
            const int seedCount = XDRNG::getSeeds(seeds, static_cast<uint32_t>(TID16) << 16, static_cast<uint32_t>(SID16) << 16);
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                const uint32_t seed = seeds[seedIndex];
                if (speciesId == UMBREON_SPECIES)
                {
                    if (!isMatchIVs(firstIVs, secondIVs, XDRNG::next4(seed)))
                    {
                        continue;
                    }
                }
                uint32_t umbreonPIDSeed = XDRNG::next7(seed);
                uint32_t PID = RNG::getColoStarterPID(umbreonPIDSeed, ID32);
                if (speciesId == UMBREON_SPECIES)
                {
                    if (PID != expectedPID)
                    {
                        continue;
                    }
                    result = seed;
                    return true;
                }

                const uint32_t afterFakePID = XDRNG::next2(umbreonPIDSeed);
                if (!isMatchIVs(firstIVs, secondIVs, afterFakePID))
                {
                    continue;
                }
                uint32_t espeonPIDSeed = XDRNG::next3(afterFakePID);
                PID = RNG::getColoStarterPID(espeonPIDSeed, ID32);
                if (PID != expectedPID)
                {
                    continue;
                }
                result = seed;
                return true;
            }
            result = 0;
            return false;
        }

        bool MethodCXD::tryGetSeedStarterXD(uint32_t firstIVs, uint32_t secondIVs, uint32_t expectedPID, uint16_t TID16, uint16_t SID16, uint32_t &result) noexcept
        {
            uint32_t seeds[XDRNG::MAX_PID_SEED_COUNT];
            const int seedCount = XDRNG::getSeeds(seeds, static_cast<uint32_t>(TID16) << 16, static_cast<uint32_t>(SID16) << 16);
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                const uint32_t seed = seeds[seedIndex];
                if (!isMatchIVs(firstIVs, secondIVs, XDRNG::next4(seed)))
                {
                    continue;
                }
                uint32_t pidSeed = XDRNG::next7(seed);
                if (getPIDRegular(pidSeed) != expectedPID)
                {
                    continue;
                }
                result = seed;
                return true;
            }
            result = 0;
            return false;
        }

        bool MethodCXD::tryGetSeedStarterColo(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept
        {
            const uint32_t packedIVs = getPackedIVs(pokemon);
            return tryGetSeedStarterColo(packedIVs & 0x7FFF, packedIVs >> 15, pokemon.encryptionConstant(), pokemon.tid16(), pokemon.sid16(), pokemon.speciesID(), result);
        }

        bool MethodCXD::tryGetSeedStarterXD(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept
        {
            const uint32_t packedIVs = getPackedIVs(pokemon);
            return tryGetSeedStarterXD(packedIVs & 0x7FFF, packedIVs >> 15, pokemon.encryptionConstant(), pokemon.tid16(), pokemon.sid16(), result);
        }

        // PKHeX PKHeX.Core/Legality/RNG/CXD/MethodCXD.cs IsValidNameScreenEndSeed
        bool MethodCXD::isValidNameScreenEndSeed(uint32_t seed, uint32_t &origin) noexcept
        {
            const bool firstQuiet = (seed >> 16) > NAME_SCREEN_SEED_THRESHOLD;
            const bool secondQuiet = XDRNG::previous16(seed) > NAME_SCREEN_SEED_THRESHOLD;
            const bool thirdQuiet = XDRNG::previous16(seed) > NAME_SCREEN_SEED_THRESHOLD;
            const bool fourthQuiet = XDRNG::previous16(seed) > NAME_SCREEN_SEED_THRESHOLD;

            if (firstQuiet && secondQuiet && thirdQuiet && fourthQuiet)
            {
                origin = seed;
                return true;
            }

            if (XDRNG::previous16(seed) <= NAME_SCREEN_SEED_THRESHOLD && isValidNameScreenEndSeed(XDRNG::previous(seed), origin))
            {
                return true;
            }
            if (XDRNG::previous16(seed) <= NAME_SCREEN_SEED_THRESHOLD && firstQuiet && isValidNameScreenEndSeed(XDRNG::previous(seed), origin))
            {
                return true;
            }
            if (XDRNG::previous16(seed) <= NAME_SCREEN_SEED_THRESHOLD && firstQuiet && secondQuiet && isValidNameScreenEndSeed(XDRNG::previous(seed), origin))
            {
                return true;
            }
            if (XDRNG::previous16(seed) <= NAME_SCREEN_SEED_THRESHOLD && firstQuiet && secondQuiet && thirdQuiet && isValidNameScreenEndSeed(XDRNG::previous(seed), origin))
            {
                return true;
            }

            origin = 0;
            return false;
        }

        bool MethodCXD::tryGetSeedTrainerID(uint16_t TID16, uint16_t SID16, uint32_t &origin) noexcept
        {
            uint32_t seeds[XDRNG::MAX_PID_SEED_COUNT];
            const int seedCount = XDRNG::getSeeds(seeds, static_cast<uint32_t>(TID16) << 16, static_cast<uint32_t>(SID16) << 16);
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                if (isValidNameScreenEndSeed(XDRNG::previous1000(seeds[seedIndex]), origin))
                {
                    return true;
                }
            }
            origin = 0;
            return false;
        }

        bool MethodCXD::tryGetShinySID(uint16_t TID16, uint16_t &SID16, uint32_t xorValue, uint32_t bits) noexcept
        {
            for (int attemptIndex = 0; attemptIndex < 8; attemptIndex++, bits++)
            {
                const uint16_t candidateSID = static_cast<uint16_t>(xorValue ^ (bits & 7));
                uint32_t origin = 0;
                if (!tryGetSeedTrainerID(TID16, candidateSID, origin))
                {
                    continue;
                }
                SID16 = candidateSID;
                return true;
            }
            SID16 = 0;
            return false;
        }
    }
}
