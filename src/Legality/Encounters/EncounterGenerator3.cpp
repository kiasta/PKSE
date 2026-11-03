#include "Legality/Encounters/EncounterGenerator3.h"

#include <algorithm>

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator3.h"
#include "Legality/Encounters/EncounterGenerator8.h"
#include "Legality/Encounters/EncounterSlot3.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionEntity.h"
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
            /// PKHeX version ids with no GameVersion member: the Nintendo Switch Online FireRed and
            /// LeafGreen.
            constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
            constexpr uint8_t VERSION_LEAFGREEN_SWITCH = 57;

            constexpr uint16_t SPECIES_MARILL = 183;
            constexpr uint16_t SPECIES_AZUMARILL = 184;
            constexpr uint16_t SPECIES_CELEBI = 251;

            /// PKHeX EntityGender.MM: the ratio Azurill's gender is read with once it evolves.
            constexpr uint8_t GENDER_RATIO_MARILL = 63;
            constexpr uint8_t GENDER_MALE = 0;
            constexpr uint8_t GENDER_FEMALE = 1;

            /// PKHeX EncounterGenerator3.DeferralType: the legal reasons rank before the illegal ones.
            enum class DeferralType : uint8_t
            {
                None,
                PIDIVDefer,
                SlotNumber,
                Ball,
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

            bool isBallCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon)
            {
                const bool isSafariBall = pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari);
                if (encounter.fixedBall() == Enums::Ball::Safari && isSafariBall)
                {
                    return true;
                }
                return !isSafariBall;
            }

            RandomCorrelationRating isTypeCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon, RNG::PIDIV &pidiv)
            {
                const RandomCorrelationTemplate *correlation = encounter.asRandomCorrelation();
                if (correlation != nullptr && correlation->hasReviseReset())
                {
                    return correlation->isCompatibleReviseReset(pidiv, pokemon);
                }
                if (correlation != nullptr)
                {
                    return correlation->isCompatible(pidiv.type, pokemon);
                }
                return pidiv.type == RNG::PIDType::None ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }

            /// PKHeX LeadFinder.GetLevelConstraint: the met level while still in Gen 3, else the
            /// slot's range clipped to the chain stage.
            Evolutions::EvolutionCriteria getLevelConstraint(const Pokemon::Pokemon &pokemon, std::span<const Evolutions::EvolutionCriteria> chain, const EncounterSlot3 &slot)
            {
                Evolutions::EvolutionCriteria constraint;
                constraint.speciesId = slot.speciesId();
                if (getFormatGeneration(pokemon) == 3)
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

        Enums::GameVersion EncounterGenerator3::getGameToIterate(Enums::GameVersion version) noexcept
        {
            switch (static_cast<uint8_t>(version))
            {
            case VERSION_FIRERED_SWITCH:
                return Enums::GameVersion::FR;
            case VERSION_LEAFGREEN_SWITCH:
                return Enums::GameVersion::LG;
            default:
                return version;
            }
        }

        EncounterCandidates EncounterGenerator3::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 3, Enums::GameVersion::RSE);
            return getEncounters(pokemon, chain, info, static_cast<Enums::GameVersion>(getOriginVersion(pokemon)));
        }

        EncounterCandidates EncounterGenerator3::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }

            // PKHeX's workaround for HOME's Celebi: one from the Switch FireRed/LeafGreen is the Gen 8
            // HOME gift.
            if (chain.stages[0].speciesId == SPECIES_CELEBI && Evolutions::isVersionVirtualConsoleGen3(info.entity))
            {
                for (const EncounterReference &encounter : EncounterGenerator8::getEncounters(pokemon, chain, info))
                {
                    co_yield encounter;
                }
                co_return;
            }

            info.setPIDIV(RNG::MethodFinder::analyze(pokemon));
            const Enums::GameVersion game = getGameToIterate(version);
            Deferral deferral;
            RNG::LeadEncounterQueue<EncounterReference> leadQueue;

            const bool emerald = isVersion(pokemon, Enums::GameVersion::EM);
            uint8_t gender = pokemon.gender();
            if (pokemon.speciesID() == SPECIES_MARILL || pokemon.speciesID() == SPECIES_AZUMARILL)
            {
                // PKHeX EntityGender.GetFromPID(EC, MM): an evolved Azurill keeps the gender its
                // PID gave it as an Azurill.
                gender = (pokemon.encryptionConstant() & 0xFF) < GENDER_RATIO_MARILL ? GENDER_FEMALE : GENDER_MALE;
            }

            for (const MatchedEncounter &matched : EncounterEnumerator3::enumerate(pokemon, chain, game))
            {
                const Encounterable &encounter = *matched.encounter;
                const RandomCorrelationRating typeCheck = isTypeCompatible(encounter, pokemon, info.pidiv);
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
                const EncounterSlot3 *slot = encounterAs<EncounterSlot3>(encounter);
                if (slot == nullptr)
                {
                    co_yield matched.encounter;
                    continue;
                }

                const Evolutions::EvolutionCriteria constraint = getLevelConstraint(pokemon, chain.view(), *slot);
                const RNG::LeadSeed lead = RNG::LeadFinder::getLeadInfo3(slot->getSlotParameters(), info.pidiv, constraint.levelMin, constraint.levelMax, emerald, gender, getFormatGeneration(pokemon));
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
            // vBlanks make the lead analysis imperfect, so PKHeX yields the rest rather than stopping.

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

        bool EncounterGenerator3::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg3 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg3::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species, unless it is a split-breed parent.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::RSE);
            if (tree == nullptr)
            {
                return false;
            }
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(devolved.speciesId, devolved.formId);
            if (baby.speciesId != devolved.speciesId && !isSplitBreedNotBabySpecies3(devolved.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::RSE))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg3(baby.speciesId, version);
            return true;
        }

        bool EncounterGenerator3::tryGetSplit(const EncounterEgg3 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg3 &result)
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
            if (!isSplitBreedNotBabySpecies3(devolved->speciesId))
            {
                return false;
            }
            result = other.withSpecies(devolved->speciesId);
            return true;
        }
    }
}
