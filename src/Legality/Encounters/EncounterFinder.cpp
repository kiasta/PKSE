#include "Legality/Encounters/EncounterFinder.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator.h"
#include "Legality/Encounters/EncounterInvalid.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterMoveSeam.h"
#include "Legality/Encounters/EncounterVerifier.h"
#include "Legality/Encounters/EvolutionVerifier.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Legality/Encounters/EncounterTransfer7.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX's Gen 3 gift-egg met location (253), compared on its low byte.
            constexpr uint8_t LOCATION_GIFT_EGG3_BYTE = 253;
            /// PKHeX Locations 2009-2014: the Gen 4 gift-egg locations.
            constexpr uint16_t LOCATION_GIFT_EGG4_FIRST = 2009;
            constexpr uint16_t LOCATION_GIFT_EGG4_LAST = 2014;

            /// PKHeX FramePatternSettings defaults: RNGFrameNotFound3 is Fishy, RNGFrameNotFound4 Invalid.
            Judgement getFramePatternJudgement(uint8_t generation) noexcept
            {
                return generation == 3 ? Judgement::Fishy : Judgement::Invalid;
            }

            bool wasGiftEgg(const Pokemon::Pokemon &pokemon, uint8_t generation, uint16_t eggLocation)
            {
                if (pokemon.isFatefulEncounter())
                {
                    return false;
                }
                switch (generation)
                {
                case 3:
                    // A gift egg cannot be told from a bred one once hatched.
                    return pokemon.isEgg() && static_cast<uint8_t>(pokemon.metLocation()) == LOCATION_GIFT_EGG3_BYTE;
                case 4:
                {
                    const bool isHeartGoldSoulSilver = isVersion(pokemon, Enums::GameVersion::HG) || isVersion(pokemon, Enums::GameVersion::SS);
                    return (eggLocation >= LOCATION_GIFT_EGG4_FIRST && eggLocation <= LOCATION_GIFT_EGG4_LAST) || (getFormatGeneration(pokemon) != 4 && eggLocation == LOCATION_FARAWAY4 && isHeartGoldSoulSilver);
                }
                case 5:
                    return eggLocation == LOCATION_BREEDER5;
                default:
                    return eggLocation == LOCATION_BREEDER6;
                }
            }

            bool wasEventEgg(const Pokemon::Pokemon &pokemon, uint8_t generation)
            {
                if (generation == 3)
                {
                    // An event egg cannot be told apart after it hatches or leaves Gen 3.
                    return getFormatGeneration(pokemon) == 3 && pokemon.isEgg() && isEventLocation3(pokemon.metLocation());
                }
                // Manaphy was the only Gen 4 event egg.
                return pokemon.isFatefulEncounter() && pokemon.eggDay() != 0;
            }

            bool wasEvent(const Pokemon::Pokemon &pokemon, uint8_t generation)
            {
                if (pokemon.isFatefulEncounter())
                {
                    return true;
                }
                if (generation == 3)
                {
                    return isEventLocation3(pokemon.metLocation()) && getFormatGeneration(pokemon) == 3;
                }
                if (generation == 4)
                {
                    return isEventLocation4(pokemon.metLocation()) && getFormatGeneration(pokemon) == 4;
                }
                if (generation >= 5)
                {
                    return isEventLocation5(pokemon.metLocation());
                }
                return false;
            }

            ResultCode getHintWhyNotFound(const Pokemon::Pokemon &pokemon, uint8_t generation)
            {
                if (wasGiftEgg(pokemon, generation, pokemon.eggLocation()))
                {
                    return ResultCode::EncounterGift;
                }
                if (wasEventEgg(pokemon, generation))
                {
                    return ResultCode::EncounterGiftEggEvent;
                }
                if (wasEvent(pokemon, generation))
                {
                    return ResultCode::EncounterGiftNotFound;
                }
                return ResultCode::EncounterInvalid;
            }

            /// PKHeX VerifySecondaryChecks. The memory checks PKHeX runs for Gen 6+ formats
            /// (MemoryPermissions.CanKnowMove) are not ported; they belong to those generations'
            /// ports and pass here.
            bool verifySecondaryChecks(const Pokemon::Pokemon &pokemon, LegalityInfo &info, PeekEnumerator &iterator)
            {
                if (getFormatGeneration(pokemon) >= 6)
                {
                    if (!verifyRelearnMoves(info, pokemon) && iterator.peekIsNext())
                    {
                        return false;
                    }
                }
                else
                {
                    fillRelearnValid(info);
                }

                if (!verifyCurrentMoves(info, pokemon) && iterator.peekIsNext())
                {
                    return false;
                }
                if (!info.areAllParseValid() && iterator.peekIsNext())
                {
                    return false;
                }
                CheckResult evolution = EvolutionVerifier::verifyEvolution(pokemon, info);
                if (!evolution.isValid() && iterator.peekIsNext())
                {
                    return false;
                }
                // A Gen 1 record that knows a Gen 2 move went through the Time Capsule, which leaves a
                // held item in the catch-rate byte. PKHeX rejects the candidate even when it is the last.
                if (pokemon.getGameGroup() == Enums::GameVersion::RBY && hasGen2Move(info))
                {
                    if (!ALLOW_GEN1_TRADEBACK)
                    {
                        return false;
                    }
                    if (!GBRestrictions::isCatchRateHeldItem(GameBoyEntity::getCatchRate(pokemon)))
                    {
                        return false;
                    }
                }
                info.parse.push_back(std::move(evolution));
                return true;
            }

            void verifyWithoutEncounter(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
            {
                info.setEncounterMatch(makeEncounter<EncounterInvalid>(EncounterInvalid(pokemon)));
                const ResultCode hint = getHintWhyNotFound(pokemon, info.encounterMatch()->generation());
                info.parse.push_back(CheckResult::get(Judgement::Invalid, CheckIdentifier::Encounter, hint));
                verifyRelearnMoves(info, pokemon);
                verifyCurrentMoves(info, pokemon);
            }
        }

        // PKHeX LegalityAnalysis.UpdateVCTransferInfo
        void EncounterFinder::updateVirtualConsoleTransferInfo(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            // PKHeX ParsePK7: only a Gen 7 record. A Virtual Console Pokemon in a later format keeps
            // its Game Boy match.
            const bool isVirtualConsole = GameBoyEntity::isVirtualConsole1(pokemon) || GameBoyEntity::isVirtualConsole2(pokemon);
            if (getFormatGeneration(pokemon) != 7 || !isVirtualConsole)
            {
                return;
            }
            const EncounterReference original = info.encounterMatch();
            info.encounterOriginalGameBoy = original;
            if (!original || EncounterInvalid::isTemplateType(original->templateType()))
            {
                return;
            }
            const std::span<const Evolutions::EvolutionCriteria> chain = info.evolutionChainsAllGens().getChain(Enums::GameVersion::SM);
            info.setEncounterMatch(makeEncounter<EncounterTransfer7>(getVirtualConsoleTransferEncounter(pokemon, original->speciesId(), chain)));
        }

        void EncounterFinder::findVerifiedEncounter(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            PeekEnumerator encounter(EncounterGenerator::getEncounters(pokemon, info));
            if (!encounter.peekIsNext())
            {
                verifyWithoutEncounter(pokemon, info);
                return;
            }

            const uint8_t firstGeneration = encounter.current()->generation();
            while (encounter.moveNext())
            {
                const EncounterReference candidate = encounter.current();

                // Basic compatibility first.
                CheckResult basic = EncounterVerifier::verifyEncounter(firstGeneration, pokemon, *candidate, info);
                if (!basic.isValid() && encounter.peekIsNext())
                {
                    continue;
                }

                // Possibly a good enough match: check whether it really is.
                info.setEncounterMatch(candidate);
                if (basic.code != ResultCode::Valid)
                {
                    info.parse.push_back(std::move(basic));
                }
                if (!verifySecondaryChecks(pokemon, info, encounter))
                {
                    continue;
                }

                // Some secondary checks are less thorough than the template's own partial-match test.
                const EncounterMatchTemplate *matchable = candidate->asEncounterMatch();
                if (matchable == nullptr)
                {
                    break;
                }
                if (matchable->getMatchRating(pokemon) < EncounterMatchRating::PartialMatch)
                {
                    break;
                }
                if (encounter.peekIsNext())
                {
                    continue;
                }
                // Out of candidates without a complete match.
                info.parse.push_back(CheckResult::get(Judgement::Invalid, CheckIdentifier::Encounter, ResultCode::EncounterInvalid));
                break;
            }

            if (info.manualFlag == EncounterYieldFlag::None)
            {
                return;
            }
            if (!info.frameMatches())
            {
                // Every valid RNG frame match has already been tried.
                info.parse.push_back(CheckResult::get(getFramePatternJudgement(info.generation), CheckIdentifier::PID, ResultCode::EncounterConditionBadRNGFrame));
            }
            else if (!info.pidivMatches())
            {
                // Every valid PID/IV match has already been tried.
                info.parse.push_back(CheckResult::get(Judgement::Invalid, CheckIdentifier::PID, ResultCode::PIDTypeMismatch));
            }
        }
    }
}
