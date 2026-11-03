#include "Legality/Encounters/EncounterSlot8a.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/RNG/GenerateParameters.h"
#include "Legality/RNG/Overworld8aRNG.h"
#include "Legality/RNG/Xoroshiro128PlusReversal.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon8LA.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_WURMPLE = 265;
            constexpr uint16_t SPECIES_BURMY = 412;
            constexpr uint16_t SPECIES_WORMADAM = 413;
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint8_t SLOT_GENDER_RANDOM = 2;

            // PKHeX EncounterSlot8a.GetRollCountBoost: a mass outbreak adds shiny rolls.
            uint8_t getRollCountBoost(SlotType8a type) noexcept
            {
                switch (type)
                {
                case SlotType8a::MassOutbreakMassive:
                    return 12;
                case SlotType8a::MassOutbreakRegular:
                    return 25;
                default:
                    return 0;
                }
            }
        }

        EncounterSlot8a EncounterArea8a::slotAt(uint16_t slotIndex) const noexcept
        {
            return EncounterSlot8a(*this, areaSlots[slotIndex]);
        }

        bool EncounterArea8a::isMatchLocation(uint16_t location) const noexcept
        {
            const uint8_t truncatedLocation = static_cast<uint8_t>(location);
            return std::find(areaLocations, areaLocations + areaLocationCount, truncatedLocation) != areaLocations + areaLocationCount;
        }

        // PKHeX EncounterSlot8a.IsMatchExact
        bool EncounterSlot8a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            const bool isFormFree = record.speciesId == SPECIES_ROTOM || record.speciesId == SPECIES_BURMY || record.speciesId == SPECIES_WORMADAM;
            return record.formId == evolution.formId || isFormFree;
        }

        // PKHeX EncounterSlot8a.GetMatchRating
        EncounterMatchRating EncounterSlot8a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (record.gender != SLOT_GENDER_RANDOM && pokemon.gender() != record.gender)
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isDeferredWurmple = record.speciesId == SPECIES_WURMPLE && pokemon.speciesID() != SPECIES_WURMPLE && !WurmpleUtil::isWurmpleEvolutionValid(pokemon);
            if (isDeferredWurmple)
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (!isMarkValidAlpha(pokemon, isAlpha()) || isAlphaMismatch(pokemon, isAlpha()))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (record.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < record.flawlessIVCount)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (isFormArgumentMismatch8a(pokemon, record.speciesId))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (!isForcedMasteryCorrect(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return getMoveCompatibility(pokemon);
        }

        // PKHeX EncounterSlot8a.IsForcedMasteryCorrect
        bool EncounterSlot8a::isForcedMasteryCorrect(const Pokemon::Pokemon &pokemon) const noexcept
        {
            // PKHeX IMoveShop8Mastery: only a PA8 keeps the move-shop flags.
            if (pokemon.getGameGroup() != Enums::GameVersion::PLA)
            {
                return true;
            }
            const auto &legendsPokemon = static_cast<const Pokemon::Pokemon8LA &>(pokemon);
            // Every Alpha but a massive mass outbreak's was catchable before the 1.1 patch.
            const bool allowAlphaPurchaseBug = type() != SlotType8a::MassOutbreakMassive;
            const uint8_t metLevel = pokemon.metLevel();
            const LearnSource::Learnset learnset = LearnSource::getLearnset(LearnSource::LearnEnvironment::PLA, record.speciesId, record.formId);
            const uint16_t alphaMove = legendsPokemon.alphaMove();
            if (!isValidPurchasedEncounter8a(legendsPokemon, learnset, metLevel, alphaMove, allowAlphaPurchaseBug))
            {
                return false;
            }
            std::array<uint16_t, 4> encounterMoves{};
            if (alphaMove != 0)
            {
                encounterMoves[0] = alphaMove;
                learnset.setEncounterMovesBackwards(metLevel, encounterMoves, 1);
            }
            else
            {
                learnset.setEncounterMoves(metLevel, encounterMoves);
            }
            return isValidMasteredEncounter8a(legendsPokemon, encounterMoves, learnset, Pokemon::getMasteryLevels8LA(record.speciesId, record.formId), metLevel, alphaMove, allowAlphaPurchaseBug);
        }

        // PKHeX EncounterSlot8a.GetMoveCompatibility: an Alpha's one move-shop move.
        EncounterMatchRating EncounterSlot8a::getMoveCompatibility(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (pokemon.getGameGroup() != Enums::GameVersion::PLA)
            {
                return EncounterMatchRating::Match;
            }
            const auto &legendsPokemon = static_cast<const Pokemon::Pokemon8LA &>(pokemon);
            const uint16_t alphaMove = legendsPokemon.alphaMove();
            if (!legendsPokemon.isAlpha() || type() == SlotType8a::Landmark)
            {
                return alphaMove == 0 ? EncounterMatchRating::Match : EncounterMatchRating::DeferredErrors;
            }
            if (alphaMove == 0)
            {
                // None set, but one is granted whenever the species has a move shop.
                return Pokemon::getMoveShopPermissions8LA(record.speciesId, record.formId) != 0 ? EncounterMatchRating::Deferred : EncounterMatchRating::Match;
            }
            // PKHeX `pa.Permit`: the record's own species and form.
            const int shopIndex = getMoveShopIndex8a(alphaMove);
            if (shopIndex < 0)
            {
                return EncounterMatchRating::Deferred;
            }
            const uint64_t permissions = Pokemon::getMoveShopPermissions8LA(pokemon.speciesID(), pokemon.form());
            return ((permissions >> shopIndex) & 1) != 0 ? EncounterMatchRating::Match : EncounterMatchRating::Deferred;
        }

        // PKHeX EncounterSlot8a.TryGetSeed
        SeedCorrelationResult EncounterSlot8a::tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const noexcept
        {
            RNG::OverworldParam8a parameters;
            parameters.shiny = RNG::Shiny::Random;
            parameters.isAlpha = isAlpha();
            parameters.flawlessIVs = record.flawlessIVCount;
            parameters.rollCount = static_cast<uint8_t>(1 + getRollCountBoost(type()));
            parameters.genderRatio = getGenderRatio8a(record.gender, record.speciesId, record.formId);
            RNG::XoroMachineSkip seeds(pokemon.encryptionConstant(), pokemon.pid());
            while (seeds.moveNext())
            {
                if (RNG::Overworld8aRNG::verify(pokemon, seeds.current(), parameters))
                {
                    seed = seeds.current();
                    return SeedCorrelationResult::Success;
                }
            }
            seed = 0;
            return SeedCorrelationResult::Ignore;
        }
    }
}
