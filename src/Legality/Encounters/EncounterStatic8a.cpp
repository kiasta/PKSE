#include "Legality/Encounters/EncounterStatic8a.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterRules8a.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/RNG/GenerateParameters.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/Overworld8aRNG.h"
#include "Legality/RNG/Xoroshiro128PlusReversal.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon8LA.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX EncounterStatic8a.ScaleMax: HOME 3.0.1's size for the average-sized Alphas.
            constexpr uint8_t SCALE_MAXIMUM = 255;
            constexpr uint8_t SCALE_AVERAGE = 127;
        }

        // PKHeX EncounterStatic8a.IsMatchExact
        bool EncounterStatic8a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (isAlphaMismatch(pokemon, properties.isAlpha))
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::PLA, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            return isMatchSize(pokemon);
        }

        // The size half of PKHeX IsMatchExact (`pk is IScaledSize`).
        bool EncounterStatic8a::isMatchSize(const Pokemon::Pokemon &pokemon) const
        {
            if (!RNG::HomeQuirks::hasSizeScalars(pokemon))
            {
                return true;
            }
            const uint8_t heightScalar = pokemon.heightScalar();
            const uint8_t weightScalar = pokemon.weightScalar();
            if (!isAlpha127())
            {
                if (hasFixedHeight() && heightScalar != properties.heightScalar)
                {
                    return false;
                }
                return !hasFixedWeight() || weightScalar == properties.weightScalar;
            }
            // Three of the Alpha statics were mistakenly set to 127 sizes; HOME 3.0.1 or later raises
            // them to 255. The match defers anything finer to the later checks.
            const bool isMaximum = heightScalar == SCALE_MAXIMUM && weightScalar == SCALE_MAXIMUM;
            const Enums::GameVersion currentContextGroup = getEntityContextGroup(pokemon);
            if (currentContextGroup != Enums::GameVersion::PLA && currentContextGroup != Enums::GameVersion::SV)
            {
                return isMaximum;
            }
            if (heightScalar == SCALE_AVERAGE && weightScalar == SCALE_AVERAGE)
            {
                // Original; Legends: Arceus and Scarlet/Violet could keep them safe through HOME 3.0.0.
                return true;
            }
            // The HOME-updated values must come with the Alpha Mark HOME sets at the same time.
            return isMaximum && !isMissingAlphaMark(pokemon);
        }

        // PKHeX EncounterStatic8a.IsMatchLocation
        bool EncounterStatic8a::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            const LocationRemapState metState = getRemapState(Enums::GameVersion::PLA, getEntityContextGroup(pokemon));
            const uint16_t metLocation = pokemon.metLocation();
            const bool isExact = metLocation == properties.location;
            const bool isRemapped = metLocation == LOCATION_HOME_SWLA;
            if (metState == LocationRemapState::Original)
            {
                return isExact;
            }
            if (metState == LocationRemapState::Remapped)
            {
                return isRemapped;
            }
            return isExact || isRemapped;
        }

        // PKHeX EncounterStatic8a.GetMatchRating
        EncounterMatchRating EncounterStatic8a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.shiny != RNG::Shiny::Random && !isShinyValid(properties.shiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (properties.fixedBall != Enums::Ball::None && pokemon.ball() != static_cast<uint8_t>(properties.fixedBall))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (!isForcedMasteryCorrect(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (!isMarkValidAlpha(pokemon, properties.isAlpha) || isAlphaMismatch(pokemon, properties.isAlpha))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (properties.isAlpha && pokemon.getGameGroup() == Enums::GameVersion::PLA && static_cast<const Pokemon::Pokemon8LA &>(pokemon).alphaMove() == 0)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic8a.IsForcedMasteryCorrect
        bool EncounterStatic8a::isForcedMasteryCorrect(const Pokemon::Pokemon &pokemon) const noexcept
        {
            // PKHeX IMoveShop8Mastery: only a PA8 keeps the move-shop flags.
            if (pokemon.getGameGroup() != Enums::GameVersion::PLA)
            {
                return true;
            }
            const auto &legendsPokemon = static_cast<const Pokemon::Pokemon8LA &>(pokemon);
            // A scripted Alpha's move-shop move is its first move.
            uint16_t alphaMove = 0;
            if (properties.isAlpha && properties.moves.hasMoves())
            {
                alphaMove = legendsPokemon.alphaMove();
                if (alphaMove != properties.moves.moveIds[0])
                {
                    return false;
                }
            }
            // Every static Alpha was catchable before the 1.1 patch.
            constexpr bool allowAlphaPurchaseBug = true;
            const uint8_t metLevel = pokemon.metLevel();
            const LearnSource::Learnset learnset = LearnSource::getLearnset(LearnSource::LearnEnvironment::PLA, properties.speciesId, properties.formId);
            if (!isValidPurchasedEncounter8a(legendsPokemon, learnset, metLevel, alphaMove, allowAlphaPurchaseBug))
            {
                return false;
            }
            std::array<uint16_t, 4> encounterMoves = properties.moves.moveIds;
            if (!properties.moves.hasMoves())
            {
                learnset.setEncounterMoves(metLevel, encounterMoves);
            }
            return isValidMasteredEncounter8a(legendsPokemon, encounterMoves, learnset, Pokemon::getMasteryLevels8LA(properties.speciesId, properties.formId), metLevel, alphaMove, allowAlphaPurchaseBug);
        }

        // PKHeX EncounterStatic8a.TryGetSeed
        SeedCorrelationResult EncounterStatic8a::tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const noexcept
        {
            RNG::OverworldParam8a parameters;
            parameters.isAlpha = properties.isAlpha;
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.shiny = properties.shiny;
            // Everything static is shiny-locked anyway.
            parameters.rollCount = 1;
            parameters.genderRatio = getGenderRatio8a(properties.gender, properties.speciesId, properties.formId);
            RNG::XoroMachineSkip seeds(pokemon.encryptionConstant(), pokemon.pid());
            while (seeds.moveNext())
            {
                if (RNG::Overworld8aRNG::verify(pokemon, seeds.current(), parameters, hasFixedHeight(), hasFixedWeight()))
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
