#include "Legality/RNG/MethodFinder.h"

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/RNG/ARNG.h"
#include "Legality/RNG/CommonEvent3.h"
#include "Legality/RNG/CuteCharm4.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"
#include "Legality/RNG/LCRNGReversalSkip.h"
#include "Legality/RNG/MethodCXD.h"
#include "Legality/RNG/MonochromeRNG.h"
#include "Legality/RNG/PokewalkerRNG.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/XDRNG.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/PIDSearch.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
            constexpr uint16_t ESPEON_SPECIES = 196;
            constexpr uint16_t UMBREON_SPECIES = 197;
            constexpr uint16_t UNOWN_SPECIES = 201;
            constexpr uint16_t MARILL_SPECIES = 183;
            constexpr uint16_t AZUMARILL_SPECIES = 184;
            constexpr uint16_t FROSLASS_SPECIES = 478;
            constexpr uint8_t ESPEON_STARTER_MINIMUM_LEVEL = 25;
            constexpr uint8_t UMBREON_STARTER_MINIMUM_LEVEL = 26;
            // PKHeX EntityGender.MF (75% male) and the Snorunt line's 50% ratio.
            // PKHeX EntityGender.MF: Azurill is 75% female.
            constexpr uint8_t AZURILL_GENDER_RATIO = 0xBF;
            constexpr uint8_t GENDER_RATIO_HALF = 0x7F;
            constexpr uint32_t QUIRKY_NATURE = 24;
            // The Ruby/Sapphire Channel shiny lock xors with this constant.
            constexpr uint32_t CHANNEL_SHINY_LOCK_CONSTANT = 40122;

            using SeedBuffer = uint32_t[LCRNG::MAX_IV_SEED_COUNT];

            int getFormatGeneration(const Pokemon::Pokemon &pokemon) noexcept
            {
                return Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup()));
            }

            // PKHeX PKM.IsShiny: the shiny window is 8 through Gen 5's formats and 16 after.
            bool isShiny(const Pokemon::Pokemon &pokemon, int formatGeneration) noexcept
            {
                const uint32_t shinyXor = ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32());
                return shinyXor < (formatGeneration <= 5 ? 8u : 16u);
            }

            bool isGen4Version(uint8_t version) noexcept
            {
                using Enums::GameVersion;
                return version == static_cast<uint8_t>(GameVersion::HG) || version == static_cast<uint8_t>(GameVersion::SS) || version == static_cast<uint8_t>(GameVersion::D) || version == static_cast<uint8_t>(GameVersion::P) || version == static_cast<uint8_t>(GameVersion::Pt);
            }

            bool getNonMatch(PIDIV &pidiv) noexcept
            {
                pidiv = PIDIV();
                return false;
            }

            bool getLCRNGMatch(std::span<uint32_t> seeds, uint32_t pidHigh, uint32_t pidLow, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                int seedCount = LCRNGReversal::getSeeds(seeds, pidLow, pidHigh);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    // The first two calls made the PID.
                    uint32_t frame = LCRNG::next2(seed);
                    if (firstIVs == LCRNG::next15(frame))
                    {
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method1, seed);
                            return true;
                        }
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method4, seed);
                            return true;
                        }
                    }
                    else
                    {
                        if (firstIVs != LCRNG::next15(frame))
                        {
                            continue;
                        }
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method2, seed);
                            return true;
                        }
                    }
                }

                seedCount = LCRNGReversalSkip::getSeeds(seeds, pidLow, pidHigh);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    // Two PID calls plus the vblank call between them.
                    uint32_t frame = LCRNG::next3(seed);
                    if (firstIVs != LCRNG::next15(frame))
                    {
                        continue;
                    }
                    if (secondIVs != LCRNG::next15(frame))
                    {
                        continue;
                    }
                    pidiv = PIDIV(PIDType::Method3, seed);
                    return true;
                }
                return getNonMatch(pidiv);
            }

            // Methods 1-4 with the PID halves swapped (FireRed/LeafGreen Unown).
            bool getLCRNGUnownMatch(std::span<uint32_t> seeds, uint32_t pidHigh, uint32_t pidLow, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                int seedCount = LCRNGReversal::getSeeds(seeds, pidHigh, pidLow);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    uint32_t frame = LCRNG::next2(seed);
                    if (firstIVs == LCRNG::next15(frame))
                    {
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method1Unown, seed);
                            return true;
                        }
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method4Unown, seed);
                            return true;
                        }
                    }
                    else
                    {
                        if (firstIVs != LCRNG::next15(frame))
                        {
                            continue;
                        }
                        if (secondIVs == LCRNG::next15(frame))
                        {
                            pidiv = PIDIV(PIDType::Method2Unown, seed);
                            return true;
                        }
                    }
                }

                seedCount = LCRNGReversalSkip::getSeeds(seeds, pidHigh, pidLow);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    uint32_t frame = LCRNG::next3(seed);
                    if (firstIVs != LCRNG::next15(frame))
                    {
                        continue;
                    }
                    if (secondIVs != LCRNG::next15(frame))
                    {
                        continue;
                    }
                    pidiv = PIDIV(PIDType::Method3Unown, seed);
                    return true;
                }
                return getNonMatch(pidiv);
            }

            // The Ruby/Sapphire roamer keeps only the low 8 bits of its IVs.
            bool getLCRNGRoamerMatch(std::span<uint32_t> seeds, uint32_t pidHigh, uint32_t pidLow, uint32_t packedIVs, PIDIV &pidiv) noexcept
            {
                if (packedIVs > 0xFF)
                {
                    return getNonMatch(pidiv);
                }
                const int seedCount = LCRNGReversal::getSeeds(seeds, pidLow, pidHigh);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    const uint32_t roamerIVs = (LCRNG::next3(seed) >> 16) & 0x00FF;
                    if (packedIVs != roamerIVs)
                    {
                        continue;
                    }
                    pidiv = PIDIV(PIDType::Method1Roamer, seed);
                    return true;
                }
                return getNonMatch(pidiv);
            }

            // PKHeX GetXDRNGMatch: a shiny PID for the player is re-rolled, so an anti-shiny match
            // walks back over every PID before it that was shiny for the same trainer.
            bool getXDRNGMatch(std::span<uint32_t> seeds, const Pokemon::Pokemon &pokemon, uint32_t pidHigh, uint32_t pidLow, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                const int seedCount = XDRNG::getSeeds(seeds, pidHigh, pidLow);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    uint32_t secondIVFrame = XDRNG::previous(seed);
                    uint32_t firstIVFrame = XDRNG::previous(secondIVFrame);

                    uint32_t firstCandidate = (firstIVFrame >> 16) & 0x7FFF;
                    uint32_t secondCandidate = (secondIVFrame >> 16) & 0x7FFF;
                    if (firstCandidate == firstIVs && secondCandidate == secondIVs)
                    {
                        pidiv = PIDIV(PIDType::ColosseumXD, XDRNG::previous(firstIVFrame));
                        return true;
                    }

                    const uint32_t trainerShinyValue = static_cast<uint32_t>(pokemon.tid16() ^ pokemon.sid16()) >> 3;
                    uint32_t pidShinyValue = (pidHigh ^ pidLow) >> (16 + 3);
                    if (pidShinyValue == trainerShinyValue)
                    {
                        continue;
                    }

                    uint32_t priorPIDLowFrame = seed;
                    uint32_t priorPIDHighFrame = secondIVFrame;
                    pidShinyValue = (priorPIDLowFrame ^ priorPIDHighFrame) >> 19;
                    if (pidShinyValue != trainerShinyValue)
                    {
                        continue;
                    }

                    do
                    {
                        secondIVFrame = XDRNG::previous(firstIVFrame);
                        firstIVFrame = XDRNG::previous(secondIVFrame);
                        firstCandidate = (firstIVFrame >> 16) & 0x7FFF;
                        secondCandidate = (secondIVFrame >> 16) & 0x7FFF;
                        if (firstCandidate == firstIVs && secondCandidate == secondIVs)
                        {
                            pidiv = PIDIV(PIDType::ColosseumXDAntishiny, XDRNG::previous(firstIVFrame));
                            return true;
                        }

                        priorPIDLowFrame = XDRNG::previous(priorPIDHighFrame);
                        priorPIDHighFrame = XDRNG::previous(priorPIDLowFrame);
                        pidShinyValue = (priorPIDLowFrame ^ priorPIDHighFrame) >> 19;
                    } while (pidShinyValue == trainerShinyValue);
                }
                return getNonMatch(pidiv);
            }

            // PKHeX GetChannelMatch. The event's shiny lock failed through operator precedence; a
            // PID it would have touched has the high half's top bit flipped back first.
            bool getChannelMatch(std::span<uint32_t> seeds, const Pokemon::Pokemon &pokemon, uint32_t pidHigh, uint32_t pidLow, uint32_t packedIVs, PIDIV &pidiv) noexcept
            {
                const uint8_t version = pokemon.originGame();
                if (version != static_cast<uint8_t>(Enums::GameVersion::RU) && version != static_cast<uint8_t>(Enums::GameVersion::SA))
                {
                    return getNonMatch(pidiv);
                }

                const uint32_t undoneHigh = (pidHigh >> 16) ^ 0x8000;
                if (static_cast<uint32_t>(undoneHigh > 7 ? 0 : 1) != ((pidLow >> 16) ^ pokemon.sid16() ^ CHANNEL_SHINY_LOCK_CONSTANT))
                {
                    pidHigh = undoneHigh << 16;
                }

                const int seedCount = XDRNG::getSeeds(seeds, pidHigh, pidLow);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t seed = seeds[seedIndex];
                    // The held item call is not checked: the item can be swapped.
                    const uint32_t heldItemFrame = XDRNG::next3(seed);
                    const uint32_t versionFrame = XDRNG::next(heldItemFrame);
                    if ((versionFrame >> 31) + 1 != version)
                    {
                        continue;
                    }
                    const uint32_t genderFrame = XDRNG::next(versionFrame);
                    if (genderFrame >> 31 != pokemon.otGender())
                    {
                        continue;
                    }
                    if (XDRNG::getSequentialIV32(genderFrame) != packedIVs)
                    {
                        continue;
                    }
                    if (seed >> 16 != pokemon.sid16())
                    {
                        continue;
                    }
                    pidiv = PIDIV(PIDType::Channel, XDRNG::previous(seed));
                    return true;
                }
                return getNonMatch(pidiv);
            }

            uint32_t getPIDShinyValue(uint32_t PID) noexcept
            {
                return ((PID >> 16) ^ (PID & 0xFFFF)) >> 3;
            }

            // PKHeX GetMG4Match: each ARNG re-roll keeps the original shiny value, at most 3 times.
            bool getMysteryGift4Match(std::span<uint32_t> seeds, uint32_t PID, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                const uint32_t currentShinyValue = getPIDShinyValue(PID);
                PID = ARNG::previous(PID);
                const uint32_t originalShinyValue = getPIDShinyValue(PID);
                if (originalShinyValue == currentShinyValue)
                {
                    return getNonMatch(pidiv);
                }

                for (int rerollIndex = 0; rerollIndex < 3; rerollIndex++)
                {
                    const int seedCount = LCRNGReversal::getSeeds(seeds, PID << 16, PID & 0xFFFF0000);
                    for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                    {
                        const uint32_t seed = seeds[seedIndex];
                        uint32_t frame = LCRNG::next2(seed);
                        if (firstIVs != LCRNG::next15(frame))
                        {
                            continue;
                        }
                        if (secondIVs != LCRNG::next15(frame))
                        {
                            continue;
                        }
                        pidiv = PIDIV(PIDType::Gen4MysteryGiftAntishiny, seed);
                        return true;
                    }

                    PID = ARNG::previous(PID);
                    if (getPIDShinyValue(PID) != originalShinyValue)
                    {
                        break;
                    }
                }
                return getNonMatch(pidiv);
            }

            bool getGen5MysteryGiftShinyMatch(const Pokemon::Pokemon &pokemon, uint32_t PID, PIDIV &pidiv) noexcept
            {
                const uint32_t lowHalf = PID & 0xFFFF;
                if (lowHalf <= 0xFF)
                {
                    const uint32_t abilityBit = (PID >> 16) & 1;
                    if (PID == MonochromeRNG::getShinyPID(lowHalf, abilityBit, pokemon.tid16(), pokemon.sid16()))
                    {
                        pidiv = PIDIV(PIDType::Gen5MysteryGiftShiny);
                        return true;
                    }
                }
                return getNonMatch(pidiv);
            }

            bool getCuteCharmMatch(const Pokemon::Pokemon &pokemon, uint32_t PID, PIDIV &pidiv) noexcept
            {
                if (!CuteCharm4::isCuteCharm(pokemon, PID))
                {
                    return getNonMatch(pidiv);
                }
                pidiv = PIDIV(PIDType::CuteCharm);
                return true;
            }

            bool getChainShinyMatch(std::span<uint32_t> seeds, const Pokemon::Pokemon &pokemon, uint32_t PID, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                const int seedCount = LCRNGReversal::getSeedsIVs(seeds, firstIVs << 16, secondIVs << 16);
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    uint32_t origin = 0;
                    if (!MethodFinder::isChainShinyValid(pokemon.tid16(), pokemon.sid16(), PID, seeds[seedIndex], origin))
                    {
                        continue;
                    }
                    pidiv = PIDIV(PIDType::ChainShiny, origin);
                    return true;
                }
                return getNonMatch(pidiv);
            }

            // PKHeX GetBACDMatch: the IVs come right after the event's two PID calls, so the PID is
            // checked against the calls before the IVs, then against the shiny-forcing variants.
            bool getBACDMatch(std::span<uint32_t> seeds, const Pokemon::Pokemon &pokemon, uint32_t actualPID, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                const int seedCount = LCRNGReversal::getSeedsIVs(seeds, firstIVs << 16, secondIVs << 16);
                PIDType type = PIDType::BACD;
                const uint32_t idXor = static_cast<uint32_t>(pokemon.tid16() ^ pokemon.sid16());
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    uint32_t seed = seeds[seedIndex];
                    const uint32_t secondRandom = seed >> 16;
                    const uint32_t firstRandom = LCRNG::previous16(seed);

                    const uint32_t expectedPID = CommonEvent3::getRegular(firstRandom, secondRandom);
                    if (expectedPID != actualPID)
                    {
                        const bool shiny = ShinyUtil::isShiny3(idXor, actualPID);
                        if (!shiny)
                        {
                            if (CommonEvent3::isRegularAntishiny(actualPID, expectedPID))
                            {
                                type = PIDType::BACDAntishiny;
                            }
                            else if (CommonEvent3::isForceAntishiny(actualPID, firstRandom, secondRandom, idXor))
                            {
                                type = PIDType::BACDAntishinyXor;
                            }
                            else if (CommonEvent3::isForceShinyDifferentOT(actualPID, LCRNG::previous16(seed), secondRandom, idXor))
                            {
                                type = PIDType::BACDEggShiny;
                            }
                            else
                            {
                                continue;
                            }
                        }
                        else
                        {
                            if (CommonEvent3::isRegularAntishinyDifferentOT(actualPID, expectedPID))
                            {
                                type = PIDType::BACDEggAntishiny;
                            }
                            else if (CommonEvent3::isForceAntishinyDifferentOT(actualPID, firstRandom, secondRandom, idXor))
                            {
                                type = PIDType::BACDEggAntishinyXor;
                            }
                            else if (CommonEvent3::isForceShiny(actualPID, LCRNG::previous16(seed), secondRandom, idXor))
                            {
                                type = PIDType::BACDShiny;
                            }
                            else
                            {
                                continue;
                            }
                        }
                    }

                    pidiv = PIDIV(type, LCRNG::previous(seed));
                    return true;
                }
                return getNonMatch(pidiv);
            }

            // An Azurill (75% male) evolved into Marill/Azumarill (50%): a female Pokewalker PID
            // can read as male afterwards.
            bool isAzurillEdgeCaseMale(const Pokemon::Pokemon &pokemon, uint32_t natureId, uint32_t actualPID) noexcept
            {
                const uint16_t speciesId = pokemon.speciesID();
                if (speciesId != MARILL_SPECIES && speciesId != AZUMARILL_SPECIES)
                {
                    return false;
                }
                if (Pokemon::genderFromPID(actualPID, AZURILL_GENDER_RATIO) != 1)
                {
                    return false;
                }
                return PokewalkerRNG::getPID(pokemon.tid16(), pokemon.sid16(), natureId, 1, AZURILL_GENDER_RATIO) == actualPID;
            }

            // PKHeX GetPokewalkerMatch: bits 8-23 are all zero or all one, and Quirky cannot occur.
            bool getPokewalkerMatch(const Pokemon::Pokemon &pokemon, uint32_t actualPID, PIDIV &pidiv) noexcept
            {
                constexpr uint32_t middleMask = 0x00FFFF00;
                const uint32_t middleBits = actualPID & middleMask;
                if (middleBits != 0 && middleBits != middleMask)
                {
                    return getNonMatch(pidiv);
                }

                const uint32_t natureId = actualPID % 25;
                if (natureId == QUIRKY_NATURE)
                {
                    return getNonMatch(pidiv);
                }

                // No Pokewalker species evolves into a different ratio, except Azurill and Froslass
                // (Snorunt is 50%).
                const uint8_t gender = pokemon.gender();
                uint8_t genderRatio = Pokemon::getGenderRatio(pokemon.speciesID(), pokemon.form(), pokemon.getGameGroup());
                if (pokemon.speciesID() == FROSLASS_SPECIES)
                {
                    genderRatio = GENDER_RATIO_HALF;
                }
                const uint32_t expectedPID = PokewalkerRNG::getPID(pokemon.tid16(), pokemon.sid16(), natureId, gender, genderRatio);
                if (expectedPID != actualPID)
                {
                    if (!(gender == 0 && isAzurillEdgeCaseMale(pokemon, natureId, actualPID)))
                    {
                        return getNonMatch(pidiv);
                    }
                }
                pidiv = PIDIV(PIDType::Pokewalker);
                return true;
            }

            bool getColoStarterMatch(const Pokemon::Pokemon &pokemon, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                const uint16_t speciesId = pokemon.speciesID();
                bool isStarter = false;
                if (pokemon.originGame() == VERSION_COLOSSEUM_XD)
                {
                    isStarter = (speciesId == ESPEON_SPECIES && pokemon.metLevel() >= ESPEON_STARTER_MINIMUM_LEVEL) || (speciesId == UMBREON_SPECIES && pokemon.metLevel() >= UMBREON_STARTER_MINIMUM_LEVEL);
                }
                if (!isStarter)
                {
                    return getNonMatch(pidiv);
                }

                uint32_t starterSeed = 0;
                if (!MethodCXD::tryGetSeedStarterColo(firstIVs, secondIVs, pokemon.encryptionConstant(), pokemon.tid16(), pokemon.sid16(), speciesId, starterSeed))
                {
                    return getNonMatch(pidiv);
                }
                pidiv = PIDIV(PIDType::ColosseumStarter, starterSeed);
                return true;
            }

            bool getModified8BitMatch(const Pokemon::Pokemon &pokemon, uint32_t PID, PIDIV &pidiv) noexcept
            {
                if (isGen4Version(pokemon.originGame()))
                {
                    return (PID <= 0xFF && getCuteCharmMatch(pokemon, PID, pidiv)) || getGen5MysteryGiftShinyMatch(pokemon, PID, pidiv);
                }
                return getGen5MysteryGiftShinyMatch(pokemon, PID, pidiv) || (PID <= 0xFF && getCuteCharmMatch(pokemon, PID, pidiv));
            }

            bool getModifiedPIDMatch(std::span<uint32_t> seeds, const Pokemon::Pokemon &pokemon, int formatGeneration, uint32_t PID, uint32_t firstIVs, uint32_t secondIVs, PIDIV &pidiv) noexcept
            {
                if (isShiny(pokemon, formatGeneration))
                {
                    if (getChainShinyMatch(seeds, pokemon, PID, firstIVs, secondIVs, pidiv))
                    {
                        return true;
                    }
                    if (getModified8BitMatch(pokemon, PID, pidiv))
                    {
                        return true;
                    }
                }
                else
                {
                    if (PID <= 0xFF && getCuteCharmMatch(pokemon, PID, pidiv))
                    {
                        return true;
                    }
                }
                return getPokewalkerMatch(pokemon, PID, pidiv);
            }
        }

        uint32_t MethodFinder::getPackedIVs(const Pokemon::Pokemon &pokemon) noexcept
        {
            return static_cast<uint32_t>(pokemon.ivHP()) | (static_cast<uint32_t>(pokemon.ivATK()) << 5) | (static_cast<uint32_t>(pokemon.ivDEF()) << 10) | (static_cast<uint32_t>(pokemon.ivSPE()) << 15) | (static_cast<uint32_t>(pokemon.ivSPA()) << 20) | (static_cast<uint32_t>(pokemon.ivSPD()) << 25);
        }

        // PKHeX PKHeX.Core/Legality/RNG/MethodFinder.cs Analyze
        PIDIV MethodFinder::analyze(const Pokemon::Pokemon &pokemon) noexcept
        {
            const int formatGeneration = getFormatGeneration(pokemon);
            if (formatGeneration < 3)
            {
                return PIDIV();
            }

            const uint32_t PID = pokemon.encryptionConstant();
            const uint32_t pidHigh = PID & 0xFFFF0000;
            const uint32_t pidLow = PID << 16;

            const uint32_t packedIVs = getPackedIVs(pokemon);
            const uint32_t firstIVs = packedIVs & 0x7FFF;
            const uint32_t secondIVs = packedIVs >> 15;

            // LCRNG has the most results of any method, so one buffer of its size serves them all.
            SeedBuffer seedBuffer;
            const std::span<uint32_t> seeds(seedBuffer);

            PIDIV result;
            if (getLCRNGMatch(seeds, pidHigh, pidLow, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (pokemon.speciesID() == UNOWN_SPECIES && getLCRNGUnownMatch(seeds, pidHigh, pidLow, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (getColoStarterMatch(pokemon, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (getXDRNGMatch(seeds, pokemon, pidHigh, pidLow, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (getLCRNGRoamerMatch(seeds, pidHigh, pidLow, packedIVs, result))
            {
                return result;
            }
            if (getChannelMatch(seeds, pokemon, pidHigh, pidLow, packedIVs, result))
            {
                return result;
            }
            if (getMysteryGift4Match(seeds, PID, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (getBACDMatch(seeds, pokemon, PID, firstIVs, secondIVs, result))
            {
                return result;
            }
            if (getModifiedPIDMatch(seeds, pokemon, formatGeneration, PID, firstIVs, secondIVs, result))
            {
                return result;
            }
            return PIDIV();
        }

        bool MethodFinder::getLCRNGMethod1Match(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept
        {
            return getLCRNGMethod1Match(pokemon.encryptionConstant(), getPackedIVs(pokemon), result);
        }

        bool MethodFinder::getLCRNGMethod1Match(uint32_t PID, uint32_t packedIVs, uint32_t &result) noexcept
        {
            return getLCRNGMethod1Match(PID, packedIVs & 0x7FFF, packedIVs >> 15, result);
        }

        bool MethodFinder::getLCRNGMethod1Match(uint32_t PID, uint32_t firstIVs, uint32_t secondIVs, uint32_t &result) noexcept
        {
            const uint32_t pidLow = PID << 16;
            const uint32_t pidHigh = PID & 0xFFFF0000;
            SeedBuffer seeds;
            const int seedCount = LCRNGReversal::getSeeds(seeds, pidLow, pidHigh);
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                const uint32_t seed = seeds[seedIndex];
                uint32_t frame = LCRNG::next2(seed);
                if (firstIVs != LCRNG::next15(frame))
                {
                    continue;
                }
                if (secondIVs != LCRNG::next15(frame))
                {
                    continue;
                }
                result = seed;
                return true;
            }
            result = 0;
            return false;
        }

        // PKHeX PKHeX.Core/Legality/RNG/MethodFinder.cs IsChainShinyValid: PID bits 15 down to 3
        // are one call each (walked back), then the two 3-bit calls, then the forced high half.
        bool MethodFinder::isChainShinyValid(uint16_t TID16, uint16_t SID16, uint32_t PID, uint32_t seed, uint32_t &origin) noexcept
        {
            origin = seed;
            int bitIndex = 15;
            do
            {
                const uint32_t bit = (origin >> 16) & 1;
                if (bit != ((PID >> bitIndex) & 1))
                {
                    break;
                }
                origin = LCRNG::previous(origin);
            } while (--bitIndex != 2);
            if (bitIndex != 2)
            {
                return false;
            }

            const uint32_t upperFrame = origin;
            if (((upperFrame >> 16) & 7) != ((PID >> 16) & 7))
            {
                return false;
            }
            const uint32_t lowerFrame = LCRNG::previous(upperFrame);
            if (((lowerFrame >> 16) & 7) != (PID & 7))
            {
                return false;
            }

            const uint32_t expectedHigh = (((PID & 0xFFFF) ^ TID16 ^ SID16) & 0xFFF8) | ((upperFrame >> 16) & 0x7);
            if (expectedHigh != PID >> 16)
            {
                return false;
            }

            origin = LCRNG::previous2(lowerFrame);
            return true;
        }
    }
}
