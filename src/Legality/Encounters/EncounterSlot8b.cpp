#include "Legality/Encounters/EncounterSlot8b.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/EncounterArea4.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_WURMPLE = 265;
            constexpr uint16_t SPECIES_BURMY = 412;
            constexpr uint16_t SPECIES_MUNCHLAX = 446;
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;

            // PKHeX EncounterArea8b.LocationID_HoneyTree: the met location of each honey tree.
            constexpr std::array<uint16_t, HONEY_TREE_COUNT> HONEY_TREE_LOCATIONS8B = {359, 361, 362, 364, 365, 367, 373, 375, 378, 379, 383, 385, 392, 394, 400, 404, 407, 197, 199, 201, 253};

            // PKHeX EncounterArea8b.CanCrossoverTo: the surf areas whose slot lists differ.
            bool canCrossoverSurfTo(uint16_t areaLocation, uint16_t location) noexcept
            {
                constexpr uint16_t locationRoute223 = 486;
                constexpr uint16_t locationPokemonLeague = 167;
                constexpr uint16_t locationRoute229 = 420;
                constexpr uint16_t locationRoute230 = 489;
                switch (areaLocation)
                {
                case locationRoute223:
                    return location == locationPokemonLeague;
                case locationPokemonLeague:
                    return location == locationRoute223;
                case locationRoute229:
                    return location == locationRoute230;
                case locationRoute230:
                    return location == locationRoute229;
                default:
                    // Every other surf crossover has an identical slot list.
                    return false;
                }
            }

            // PKHeX EncounterSlot8b.CanUseRadarOverworld: the caves and buildings the Poke Radar does not work in.
            bool canUseRadarOverworld(uint16_t location) noexcept
            {
                switch (location)
                {
                case 195: // Oreburgh Mine
                case 196:
                case 203: // Mount Coronet, 206/207 exterior
                case 204:
                case 205:
                case 208:
                case 209:
                case 210:
                case 211:
                case 212:
                case 213:
                case 214:
                case 215:
                case 244: // Victory Road
                case 245:
                case 246:
                case 247:
                case 248:
                case 249:
                case 252: // Ravaged Path
                case 255: // Oreburgh Gate
                case 256:
                case 260: // Stark Mountain, 259 exterior
                case 261:
                case 262:
                case 286: // Snowpoint Temple
                case 287:
                case 288:
                case 289:
                case 290:
                case 291:
                case 292: // Wayward Cave
                case 293:
                case 294: // Ruin Maniac Cave
                case 295:
                case 296: // Maniac Tunnel
                case 299: // Iron Island, 298 exterior
                case 300:
                case 301:
                case 302:
                case 303:
                case 304:
                case 305:
                case 306: // Old Chateau
                case 307:
                case 308:
                case 309:
                case 310:
                case 311:
                case 312:
                case 313:
                case 314:
                case 368: // Route 209 (Lost Tower)
                case 369:
                case 370:
                case 371:
                case 372:
                    return false;
                default:
                    break;
                }
                const bool isSolaceonRuins = location >= 225 && location <= 243;
                const bool isTurnbackCave = location >= 264 && location <= 284;
                return !isSolaceonRuins && !isTurnbackCave;
            }
        }

        EncounterSlot8b EncounterArea8b::slotAt(uint16_t slotIndex) const noexcept
        {
            const EncounterSlotRecord8b &record = areaSlots[slotIndex];
            return EncounterSlot8b(*this, record.speciesId, record.formId, record.levelMin, record.levelMax);
        }

        // PKHeX EncounterArea8b.IsMatchLocation
        bool EncounterArea8b::isMatchLocation(uint16_t location) const noexcept
        {
            if (location == areaLocation)
            {
                return true;
            }
            if (areaType != SlotType8b::Surf)
            {
                return false;
            }
            return canCrossoverSurfTo(areaLocation, location);
        }

        // PKHeX EncounterArea8b.IsMunchlaxTree. The slot resource does not record which honey tree a
        // slot is, so any of the rare trees sharing the area's location matches.
        bool EncounterArea8b::isMunchlaxTree(uint32_t ID32) const noexcept
        {
            const std::array<uint8_t, 4> trees = calculateMunchlaxTrees(ID32);
            return std::any_of(trees.begin(), trees.end(), [this](uint8_t tree) { return HONEY_TREE_LOCATIONS8B[tree] == areaLocation; });
        }

        std::string EncounterSlot8b::name() const
        {
            return std::string("Wild Encounter (") + getVersionName8(version()) + ")";
        }

        std::string EncounterSlot8b::longName() const
        {
            return name() + " " + getSlotTypeName8b(type());
        }

        bool EncounterSlot8b::canUseRadar() const noexcept
        {
            return type() == SlotType8b::Grass && !isMoveBonusPossible() && !isMarsh() && canUseRadarOverworld(location());
        }

        std::span<const uint16_t> EncounterSlot8b::getMoveBonusPossible() const noexcept
        {
            uint16_t hatchSpeciesId = 0;
            uint8_t hatchFormId = 0;
            LearnSource::getHatchSpeciesForm(LearnSource::LearnEnvironment::BDSP, slotSpeciesId, slotFormId, hatchSpeciesId, hatchFormId);
            return LearnSource::getEggMoves(LearnSource::LearnEnvironment::BDSP, hatchSpeciesId, 0);
        }

        bool EncounterSlot8b::isMoveBonus(uint16_t moveId) const noexcept
        {
            const std::span<const uint16_t> moves = getMoveBonusPossible();
            return (moves.empty() && moveId == 0) || std::find(moves.begin(), moves.end(), moveId) != moves.end();
        }

        bool EncounterSlot8b::isInvalidMunchlaxTree(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (type() != SlotType8b::HoneyTree)
            {
                return false;
            }
            return slotSpeciesId == SPECIES_MUNCHLAX && !parent->isMunchlaxTree(pokemon.id32());
        }

        // PKHeX EncounterSlot8b.IsMatchExact
        bool EncounterSlot8b::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (slotFormId != evolution.formId && slotSpeciesId != SPECIES_BURMY)
            {
                return false;
            }
            // Only Munchlax is a honey tree's C-table encounter, reached from the four trees the save's
            // trainer id picks; the A and B tables are on every tree.
            return !isInvalidMunchlaxTree(pokemon);
        }

        // PKHeX EncounterSlot8b.GetMatchRating
        EncounterMatchRating EncounterSlot8b::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isDeferredWurmple = slotSpeciesId == SPECIES_WURMPLE && pokemon.speciesID() != SPECIES_WURMPLE && !WurmpleUtil::isWurmpleEvolutionValid(pokemon);
            if (isDeferredWurmple)
            {
                return EncounterMatchRating::PartialMatch;
            }
            // PKHeX IsDeferredHiddenAbility: a slot without the radar cannot give the Hidden Ability.
            if (!canUseRadar() && isHidden)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }
    }
}
