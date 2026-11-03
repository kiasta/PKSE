#include "Legality/Encounters/EncounterGenerator4.h"

#include <algorithm>

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator4.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/GroundTileAllowed.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Legality/RNG/LeadFinder.h"
#include "Legality/RNG/MethodFinder.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EncounterGenerator4.DeferralType: the legal reasons rank before the illegal ones.
            enum class DeferralType : uint8_t
            {
                None,
                PIDIVDefer,
                SlotNumber,
                Ball,
                Tile,
                PIDIV,
            };

            struct Deferral
            {
                DeferralType type = DeferralType::None;
                EncounterReference encounter;

                void update(DeferralType newType, const EncounterReference &candidate)
                {
                    if (newType >= type && encounter)
                    {
                        return;
                    }
                    type = newType;
                    encounter = candidate;
                }
            };

            bool isSafariOrSportBall(uint8_t ball) noexcept
            {
                return ball == static_cast<uint8_t>(Enums::Ball::Safari) || ball == static_cast<uint8_t>(Enums::Ball::Sport);
            }

            // PKHeX EncounterGenerator4.IsBallCompatible. PKSE reads no Battle Revolution box (BK4),
            // so the side-transfer case that forgets the Sport Ball never arises.
            bool isBallCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon)
            {
                const uint8_t ball = pokemon.ball();
                switch (encounter.fixedBall())
                {
                case Enums::Ball::Safari:
                    return ball == static_cast<uint8_t>(Enums::Ball::Safari);
                case Enums::Ball::Sport:
                    return ball == static_cast<uint8_t>(Enums::Ball::Sport);
                default:
                    return !isSafariOrSportBall(ball);
                }
            }

            // PKHeX EncounterGenerator4.IsTileCompatible
            bool isTileCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon)
            {
                if (!pokemon.hasGroundTile())
                {
                    return true;
                }
                const GroundTypeTileTemplate *tile = encounter.asGroundTypeTile();
                if (tile == nullptr)
                {
                    return pokemon.groundTile() == 0;
                }
                return isGroundTileAllowed(tile->groundTile(), pokemon.groundTile());
            }

            // PKHeX EncounterGenerator4.IsTypeCompatible
            RandomCorrelationRating isTypeCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon, RNG::PIDType type)
            {
                const RandomCorrelationTemplate *correlation = encounter.asRandomCorrelation();
                if (correlation != nullptr)
                {
                    return correlation->isCompatible(type, pokemon);
                }
                return type == RNG::PIDType::None ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }

            /// PKHeX LeadFinder.GetLevelConstraint: the met level while still in Gen 4, else the
            /// slot's range clipped to the chain stage.
            Evolutions::EvolutionCriteria getLevelConstraint(const Pokemon::Pokemon &pokemon, std::span<const Evolutions::EvolutionCriteria> chain, const EncounterSlot4 &slot)
            {
                Evolutions::EvolutionCriteria constraint;
                constraint.speciesId = slot.speciesId();
                if (getFormatGeneration(pokemon) == 4)
                {
                    constraint.levelMin = pokemon.metLevel();
                    constraint.levelMax = pokemon.metLevel();
                    return constraint;
                }
                for (const Evolutions::EvolutionCriteria &stage : chain)
                {
                    if (stage.speciesId != slot.speciesId())
                    {
                        continue;
                    }
                    constraint = stage;
                    constraint.levelMin = std::max(stage.levelMin, slot.levelMin());
                    constraint.levelMax = std::min(stage.levelMax, slot.levelMax());
                    return constraint;
                }
                // PKHeX throws here; the enumerator only yields slots whose species is in the
                // chain, so this is unreachable. An empty range finds no lead.
                constraint.levelMin = 1;
                constraint.levelMax = 0;
                return constraint;
            }
        }

        EncounterCandidates EncounterGenerator4::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 4, Enums::GameVersion::DP);
            return getEncounters(pokemon, chain, info);
        }

        EncounterCandidates EncounterGenerator4::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            info.setPIDIV(RNG::MethodFinder::analyze(pokemon));
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            Deferral deferral;
            RNG::LeadEncounterQueue<EncounterReference> leadQueue;

            for (const MatchedEncounter &matched : EncounterEnumerator4::enumerate(pokemon, chain, version))
            {
                const Encounterable &encounter = *matched.encounter;
                if (!isTileCompatible(encounter, pokemon))
                {
                    deferral.update(DeferralType::Tile, matched.encounter);
                    continue;
                }
                const RandomCorrelationRating typeCheck = isTypeCompatible(encounter, pokemon, info.pidiv.type);
                if (typeCheck != RandomCorrelationRating::Match)
                {
                    deferral.update(typeCheck == RandomCorrelationRating::NotIdeal ? DeferralType::PIDIVDefer : DeferralType::PIDIV, matched.encounter);
                    continue;
                }
                if (!isBallCompatible(encounter, pokemon))
                {
                    deferral.update(DeferralType::Ball, matched.encounter);
                    continue;
                }
                const EncounterSlot4 *slot = encounterAs<EncounterSlot4>(encounter);
                if (slot == nullptr)
                {
                    if (isSafariOrSportBall(pokemon.ball()))
                    {
                        deferral.update(DeferralType::Ball, matched.encounter);
                    }
                    else
                    {
                        co_yield matched.encounter;
                    }
                    continue;
                }

                const Evolutions::EvolutionCriteria constraint = getLevelConstraint(pokemon, chain.view(), *slot);
                const RNG::LeadSeed lead = RNG::LeadFinder::getLeadInfo4(pokemon, slot->getSlotParameters(), info.pidiv, constraint.levelMin, constraint.levelMax);
                if (!lead.isValid())
                {
                    deferral.update(DeferralType::SlotNumber, matched.encounter);
                    continue;
                }
                leadQueue.insert(lead, matched.encounter);
            }

            for (const RNG::LeadEncounter<EncounterReference> &cached : leadQueue.list)
            {
                info.pidiv = info.pidiv.asEncounteredVia(cached.lead);
                co_yield cached.encounter;
            }
            if (!leadQueue.list.empty())
            {
                co_return;
            }

            // The errors are flagged later for whatever is not handled here.
            if (!deferral.encounter)
            {
                co_return;
            }
            if (deferral.type == DeferralType::PIDIV)
            {
                info.manualFlag = EncounterYieldFlag::InvalidPIDIV;
            }
            else if (deferral.type == DeferralType::SlotNumber)
            {
                info.manualFlag = EncounterYieldFlag::InvalidFrame;
            }
            co_yield deferral.encounter;
        }

        bool EncounterGenerator4::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg4 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg4::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species, unless it is a split-breed parent.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::DP);
            if (tree == nullptr)
            {
                return false;
            }
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(devolved.speciesId, devolved.formId);
            if (baby.speciesId != devolved.speciesId && !isSplitBreedNotBabySpecies4(devolved.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::DP))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg4(baby.speciesId, version);
            return true;
        }

        bool EncounterGenerator4::tryGetSplit(const EncounterEgg4 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg4 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria *devolved = &chain.back();
            if (other.speciesId() == devolved->speciesId)
            {
                if (chain.size() < 2)
                {
                    return false;
                }
                devolved = &chain[chain.size() - 2];
            }
            if (!isSplitBreedNotBabySpecies4(devolved->speciesId))
            {
                return false;
            }
            result = other.withSpecies(devolved->speciesId);
            return true;
        }
    }
}
