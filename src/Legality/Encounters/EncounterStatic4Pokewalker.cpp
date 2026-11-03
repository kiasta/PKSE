#include "Legality/Encounters/EncounterStatic4Pokewalker.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/RNG/CuteCharm4.h"
#include "Legality/RNG/PokewalkerRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PIKACHU = 25;
            constexpr uint16_t SPECIES_AZURILL = 298;
            constexpr uint8_t GENDER_FEMALE = 1;
            /// PKHeX EntityGender.MF: the ratio an evolved Azurill's gender is read with in Gen 4.
            constexpr uint8_t GENDER_RATIO_MOSTLY_FEMALE = 191;

            // PKHeX PokewalkerCourse4 names, by value.
            constexpr std::array<const char *, 27> COURSE_NAMES = {
                "RefreshingField", "NoisyForest", "RuggedRoad", "BeautifulBeach", "SuburbanArea", "DimCave", "BlueLake", "TownOutskirts", "HoennField",
                "WarmBeach", "VolcanoPath", "Treehouse", "ScaryCave", "SinnohField", "IcyMountainRoad", "BigForest", "WhiteLake", "StormyBeach",
                "Resort", "QuietCave", "BeyondTheSea", "NightSkysEdge", "YellowForest", "Rally", "Sightseeing", "WinnersPath", "AmityMeadow",
            };

            // PKHeX EncounterStatic4Pokewalker.IsMatchSeed
            bool isMatchSeed(const Pokemon::Pokemon &pokemon) noexcept
            {
                const RNG::PokewalkerSeedResult seed = RNG::PokewalkerRNG::getLeastEffortSeed(pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPA(), pokemon.ivSPD(), pokemon.ivSPE());
                return seed.type != RNG::PokewalkerSeedType::None;
            }

            bool hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId) noexcept
            {
                for (int moveSlot = 0; moveSlot < 4; moveSlot++)
                {
                    if (pokemon.move(moveSlot) == moveId)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        std::string EncounterStatic4Pokewalker::name() const
        {
            const char *courseName = walkerCourseIndex < COURSE_NAMES.size() ? COURSE_NAMES[walkerCourseIndex] : "";
            return std::string("Pokéwalker Encounter (") + courseName + ")";
        }

        // PKHeX EncounterStatic4Pokewalker.IsMatchExact and its helpers
        bool EncounterStatic4Pokewalker::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            const bool isFormat4 = getFormatGeneration(pokemon) == 4;
            if (isFormat4 && pokemon.metLocation() != LOCATION_POKEWALKER4)
            {
                return false;
            }
            if (isFormat4 ? pokemon.metLevel() != walkerLevel : evolution.levelMax < walkerLevel)
            {
                return false;
            }
            // An Azurill that was female can turn male as a Marill in Gen 4, where the gender ratio differs.
            if (pokemon.gender() != walkerGender && pokemon.speciesID() != walkerSpeciesId && walkerSpeciesId == SPECIES_AZURILL && walkerGender == GENDER_FEMALE)
            {
                const uint8_t genderAsAzurill = (pokemon.pid() & 0xFF) < GENDER_RATIO_MOSTLY_FEMALE ? GENDER_FEMALE : 0;
                if (genderAsAzurill != walkerGender)
                {
                    return false;
                }
            }
            if (evolution.formId != 0 && !isFormChangeable(walkerSpeciesId, 0, pokemon.formID(), Enums::GameVersion::DP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic4Pokewalker.GetMatchRating
        EncounterMatchRating EncounterStatic4Pokewalker::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke) || !isMatchSeed(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (isShiny(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            // The level 10 Pikachu with Thunder Wave comes before the one without: yield the glitched one first.
            constexpr uint8_t glitchedPikachuLevel = 10;
            if (walkerSpeciesId == SPECIES_PIKACHU && walkerLevel == glitchedPikachuLevel && walkerMoves.moveIds[3] != 0 && !hasMove(pokemon, walkerMoves.moveIds[3]))
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic4Pokewalker.IsCompatible
        RandomCorrelationRating EncounterStatic4Pokewalker::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            if (type == RNG::PIDType::Pokewalker)
            {
                return RandomCorrelationRating::Match;
            }
            // The Pokewalker's PID routine can look like Cute Charm.
            if (type == RNG::PIDType::CuteCharm)
            {
                return RNG::CuteCharm4::isCuteCharm(pokemon, pokemon.encryptionConstant()) && RNG::CuteCharm4::isValid(walkerSpeciesId, pokemon) ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            return RandomCorrelationRating::Mismatch;
        }
    }
}
