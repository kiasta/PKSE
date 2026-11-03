#include "Legality/RNG/LeadFinder.h"

#include "Enums/GameVersion.h"
#include "Legality/RNG/CuteCharm4.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"
#include "Legality/RNG/MethodFinder.h"
#include "Legality/RNG/MethodH.h"
#include "Legality/RNG/MethodJ.h"
#include "Legality/RNG/MethodK.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint8_t VERSION_HEARTGOLD = static_cast<uint8_t>(Enums::GameVersion::HG);
            constexpr uint8_t VERSION_SOULSILVER = static_cast<uint8_t>(Enums::GameVersion::SS);

            bool isHeartGoldSoulSilver(const Pokemon::Pokemon &pokemon) noexcept
            {
                const uint8_t version = pokemon.originGame();
                return version == VERSION_HEARTGOLD || version == VERSION_SOULSILVER;
            }

            uint8_t getFormatGeneration(const Pokemon::Pokemon &pokemon) noexcept
            {
                return static_cast<uint8_t>(Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup())));
            }

            int getSeedsIVs(const Pokemon::Pokemon &pokemon, std::span<uint32_t> seeds) noexcept
            {
                return LCRNGReversal::getSeedsIVs(seeds, pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPA(), pokemon.ivSPD(), pokemon.ivSPE());
            }

            // PKHeX overwrites `result` with default after its loop, so this never reports a match; kept as PKHeX has it.
            bool tryGetMatchRadarShiny(const Pokemon::Pokemon &pokemon, LeadSeed &result) noexcept
            {
                uint32_t seeds[LCRNG::MAX_IV_SEED_COUNT];
                const int seedCount = getSeedsIVs(pokemon, seeds);
                const uint32_t encryptionConstant = pokemon.encryptionConstant();
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    uint32_t origin = 0;
                    if (!MethodFinder::isChainShinyValid(pokemon.tid16(), pokemon.sid16(), encryptionConstant, seeds[seedIndex], origin))
                    {
                        continue;
                    }
                    result = LeadSeed{origin, LeadRequired::None};
                }
                result = LeadSeed{};
                return false;
            }

            bool tryGetMatchCuteCharm4(const EncounterSlot4Parameters &encounter, const Pokemon::Pokemon &pokemon, uint8_t levelMin, uint8_t levelMax, uint8_t format, LeadSeed &result) noexcept
            {
                uint32_t seeds[LCRNG::MAX_IV_SEED_COUNT];
                const int seedCount = getSeedsIVs(pokemon, seeds);
                const std::span<const uint32_t> candidates(seeds, static_cast<size_t>(seedCount));
                const uint8_t nature = static_cast<uint8_t>(pokemon.encryptionConstant() % 25);
                if (isHeartGoldSoulSilver(pokemon))
                {
                    return MethodK::tryGetMatchCuteCharm(encounter, candidates, nature, levelMin, levelMax, format, result);
                }
                return MethodJ::tryGetMatchCuteCharm(encounter, candidates, nature, levelMin, levelMax, format, result);
            }
        }

        // PKHeX LeadFinder.GetLeadInfo3
        LeadSeed LeadFinder::getLeadInfo3(const EncounterSlot3Parameters &encounter, const PIDIV &pidiv, uint8_t levelMin, uint8_t levelMax, bool emerald, uint8_t gender, uint8_t format) noexcept
        {
            if (isClassicMethod(pidiv.type))
            {
                return MethodH::getSeed(encounter, pidiv.originSeed, levelMin, levelMax, emerald, gender, format);
            }
            return LeadSeed{};
        }

        // PKHeX LeadFinder.GetLeadInfo4
        LeadSeed LeadFinder::getLeadInfo4(const Pokemon::Pokemon &pokemon, const EncounterSlot4Parameters &encounter, const PIDIV &pidiv, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            const uint8_t format = getFormatGeneration(pokemon);
            const bool heartGoldSoulSilver = isHeartGoldSoulSilver(pokemon);
            PIDType type = pidiv.type;
            if (type == PIDType::ChainShiny)
            {
                if (ShinyUtil::isShiny(pokemon) && !heartGoldSoulSilver && encounter.canUseRadar)
                {
                    // The chain generator only skips frames for Cute Charm and Synchronize, so the nearest frame needs no lead.
                    return LeadSeed{pidiv.originSeed, LeadRequired::None};
                }
                // A chain shiny's PID/IV can, very rarely, also be a Cute Charm one.
                if (CuteCharm4::isCuteCharm(pokemon, pokemon.encryptionConstant()))
                {
                    type = PIDType::CuteCharm;
                }
            }
            else if (type == PIDType::Method1)
            {
                LeadSeed result{};
                if (tryGetLeadInfo4(encounter, levelMin, levelMax, heartGoldSoulSilver, pidiv.originSeed, format, result))
                {
                    return result;
                }

                if (ShinyUtil::isShiny(pokemon) && !heartGoldSoulSilver && encounter.canUseRadar)
                {
                    if (tryGetMatchRadarShiny(pokemon, result))
                    {
                        return result;
                    }
                }

                // MethodFinder returns Method 1 first; the same PID/IV can, very rarely, be a Cute Charm one.
                if (CuteCharm4::isCuteCharm(pokemon, pokemon.encryptionConstant()))
                {
                    type = PIDType::CuteCharm;
                }
            }

            if (type == PIDType::CuteCharm)
            {
                LeadSeed result{};
                if (tryGetMatchCuteCharm4(encounter, pokemon, levelMin, levelMax, format, result))
                {
                    return result;
                }
            }
            return LeadSeed{};
        }

        bool LeadFinder::tryGetLeadInfo4(const EncounterSlot4Parameters &encounter, uint8_t levelMin, uint8_t levelMax, bool heartGoldSoulSilver, uint32_t seed, uint8_t format, LeadSeed &result) noexcept
        {
            result = heartGoldSoulSilver ? MethodK::getSeed(encounter, seed, levelMin, levelMax, format) : MethodJ::getSeed(encounter, seed, levelMin, levelMax, format);
            return result.isValid();
        }
    }
}
