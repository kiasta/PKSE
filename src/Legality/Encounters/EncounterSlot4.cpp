#include "Legality/Encounters/EncounterSlot4.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/Encounters/GroundTileAllowed.h"
#include "Legality/RNG/CuteCharm4.h"
#include "Legality/RNG/UnownRuins4.h"
#include "Pokemon/PIDSearch.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_UNOWN = 201;
            constexpr uint16_t SPECIES_BURMY = 412;
            constexpr uint16_t SPECIES_MUNCHLAX = 446;
            constexpr uint16_t SPECIES_SHEDINJA = 292;

            // PKHeX EncounterArea4.LocationID_HoneyTree: the met location of each honey tree.
            constexpr std::array<uint8_t, HONEY_TREE_COUNT> HONEY_TREE_LOCATIONS = {20, 20, 21, 22, 23, 24, 25, 25, 26, 27, 27, 28, 29, 30, 33, 36, 37, 47, 48, 49, 58};

            // PKHeX SlotType4 names with '_' shown as a space.
            const char *getSlotTypeName(RNG::SlotType4 type) noexcept
            {
                switch (type)
                {
                case RNG::SlotType4::Grass:
                    return "Grass";
                case RNG::SlotType4::Surf:
                    return "Surf";
                case RNG::SlotType4::OldRod:
                    return "Old Rod";
                case RNG::SlotType4::GoodRod:
                    return "Good Rod";
                case RNG::SlotType4::SuperRod:
                    return "Super Rod";
                case RNG::SlotType4::RockSmash:
                    return "Rock Smash";
                case RNG::SlotType4::Headbutt:
                    return "Headbutt";
                case RNG::SlotType4::HeadbuttSpecial:
                    return "HeadbuttSpecial";
                case RNG::SlotType4::BugContest:
                    return "BugContest";
                case RNG::SlotType4::HoneyTree:
                    return "HoneyTree";
                case RNG::SlotType4::SafariGrass:
                    return "Safari Grass";
                case RNG::SlotType4::SafariSurf:
                    return "Safari Surf";
                case RNG::SlotType4::SafariOldRod:
                    return "Safari Old Rod";
                case RNG::SlotType4::SafariGoodRod:
                    return "Safari Good Rod";
                case RNG::SlotType4::SafariSuperRod:
                    return "Safari Super Rod";
                }
                return "";
            }

            // PKHeX WurmpleUtil.IsWurmpleEvoValid
            bool isWurmpleEvolutionValid(const Pokemon::Pokemon &pokemon) noexcept
            {
                constexpr uint16_t speciesSilcoon = 266;
                constexpr uint16_t speciesBeautifly = 267;
                constexpr uint16_t speciesCascoon = 268;
                constexpr uint16_t speciesDustox = 269;
                const uint16_t speciesId = pokemon.speciesID();
                int evolutionGroup = -1;
                if (speciesId == speciesSilcoon || speciesId == speciesBeautifly)
                {
                    evolutionGroup = 0;
                }
                else if (speciesId == speciesCascoon || speciesId == speciesDustox)
                {
                    evolutionGroup = 1;
                }
                return Pokemon::wurmpleEvolutionFromPID(pokemon.encryptionConstant()) == evolutionGroup;
            }
        }

        const EncounterSlot4 &EncounterArea4::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        // PKHeX EncounterArea4.GetPressureMax
        uint8_t EncounterArea4::getPressureMax(uint16_t speciesId, uint8_t levelMax) const noexcept
        {
            for (uint16_t slotIndex = 0; slotIndex < areaSlotCount; slotIndex++)
            {
                const EncounterSlot4 &slot = areaSlots[slotIndex];
                if (slot.speciesId() != speciesId)
                {
                    continue;
                }
                if (slot.levelMax() <= levelMax)
                {
                    continue;
                }
                levelMax = slot.levelMax();
            }
            return levelMax;
        }

        // PKHeX HoneyTreeUtil.CalculateMunchlaxTrees
        std::array<uint8_t, 4> calculateMunchlaxTrees(uint32_t ID32) noexcept
        {
            std::array<uint8_t, 4> trees = {static_cast<uint8_t>(ID32 >> 24), static_cast<uint8_t>(ID32 >> 16), static_cast<uint8_t>(ID32 >> 8), static_cast<uint8_t>(ID32)};
            for (uint8_t &tree : trees)
            {
                tree %= HONEY_TREE_COUNT;
            }
            // A tree equal to an earlier one moves to the next; the game does not recheck, so an id
            // can end with fewer than four distinct trees.
            for (size_t treeIndex = 1; treeIndex < trees.size(); treeIndex++)
            {
                for (size_t earlierIndex = 0; earlierIndex < treeIndex; earlierIndex++)
                {
                    if (trees[treeIndex] != trees[earlierIndex])
                    {
                        continue;
                    }
                    trees[treeIndex]++;
                    if (trees[treeIndex] >= HONEY_TREE_COUNT)
                    {
                        trees[treeIndex] = 0;
                    }
                }
            }
            return trees;
        }

        // PKHeX EncounterArea4.IsMunchlaxTree
        bool EncounterArea4::isMunchlaxTree(uint32_t ID32) const noexcept
        {
            const std::array<uint8_t, 4> trees = calculateMunchlaxTrees(ID32);
            for (const uint8_t tree : trees)
            {
                if (HONEY_TREE_LOCATIONS[tree] == areaLocation)
                {
                    return true;
                }
            }
            return false;
        }

        bool EncounterArea4::isUnownFormValid(const Pokemon::Pokemon &pokemon, uint8_t formId, bool isRuinsOfAlph)
        {
            return isRuinsOfAlph ? RNG::RuinsOfAlph4::isFormValid(pokemon, formId) : RNG::SolaceonRuins4::isFormValid(pokemon, formId);
        }

        std::string EncounterSlot4::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot4::longName() const
        {
            return name() + " " + getSlotTypeName(parent->type());
        }

        // PKHeX EncounterSlot4.GetRequiredBallValue
        Enums::Ball EncounterSlot4::fixedBall() const noexcept
        {
            if (parent->type() == RNG::SlotType4::BugContest)
            {
                return Enums::Ball::Sport;
            }
            return isSafariBallRequired4(parent->location()) ? Enums::Ball::Safari : Enums::Ball::None;
        }

        bool EncounterSlot4::canUseRadar() const noexcept
        {
            const uint8_t versionId = static_cast<uint8_t>(parent->version());
            return versionId >= static_cast<uint8_t>(Enums::GameVersion::D) && (parent->groundTile() & GROUND_TILE_GRASS) != 0 && !isMarshLocation4(parent->location());
        }

        bool EncounterSlot4::isRerollMinimum31() const noexcept
        {
            return parent->type() == RNG::SlotType4::BugContest || isSafariLocationHGSS(parent->location());
        }

        bool EncounterSlot4::isInvalidMunchlaxTree(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (parent->type() != RNG::SlotType4::HoneyTree)
            {
                return false;
            }
            return slotSpeciesId == SPECIES_MUNCHLAX && !parent->isMunchlaxTree(pokemon.id32());
        }

        // PKHeX EncounterSlot4.IsMatchExact
        bool EncounterSlot4::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (slotFormId != evolution.formId && slotSpeciesId != SPECIES_BURMY && !isRandomUnspecificForm())
            {
                return false;
            }
            if (getFormatGeneration(pokemon) == 4)
            {
                // A pressure slot passes here only because the lead check (PKHeX's default Invalid
                // severity for RNGFrameNotFound4) judges it later.
                if (!isLevelWithinRange(pokemon.metLevel()) && (parent->type() != RNG::SlotType4::Grass || pokemon.metLevel() != pressureLevel()))
                {
                    return false;
                }
            }
            else if (evolution.levelMax < slotLevelMin)
            {
                return false;
            }
            return !isInvalidMunchlaxTree(pokemon);
        }

        // PKHeX EncounterSlot4.GetMatchRating
        EncounterMatchRating EncounterSlot4::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const uint8_t ball = pokemon.ball();
            if ((ball == static_cast<uint8_t>(Enums::Ball::Safari)) != isSafariBallRequired4(parent->location()))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if ((ball == static_cast<uint8_t>(Enums::Ball::Sport)) != (parent->type() == RNG::SlotType4::BugContest))
            {
                // Evolving Nincada into Shedinja can reset the ball to a Poke Ball.
                if (pokemon.speciesID() != SPECIES_SHEDINJA || ball != static_cast<uint8_t>(Enums::Ball::Poke))
                {
                    return EncounterMatchRating::PartialMatch;
                }
            }
            constexpr uint16_t speciesWurmple = 265;
            if (slotSpeciesId == speciesWurmple && pokemon.speciesID() != speciesWurmple && !isWurmpleEvolutionValid(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (slotSpeciesId == SPECIES_UNOWN && !EncounterArea4::isUnownFormValid(pokemon, pokemon.formID(), parent->location() == RNG::RuinsOfAlph4::LOCATION))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterSlot4.IsCompatible
        RandomCorrelationRating EncounterSlot4::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            if (type == RNG::PIDType::Method1)
            {
                return RandomCorrelationRating::Match;
            }
            if (type == RNG::PIDType::ChainShiny)
            {
                return isShiny(pokemon) && canUseRadar() ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (type == RNG::PIDType::CuteCharm)
            {
                return RNG::CuteCharm4::isValid(slotSpeciesId, pokemon) ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            return RandomCorrelationRating::Mismatch;
        }

        // PKHeX EncounterSlot4.PressureLevel
        uint8_t EncounterSlot4::pressureLevel() const noexcept
        {
            if (parent->type() != RNG::SlotType4::Grass)
            {
                return slotLevelMax;
            }
            return parent->getPressureMax(slotSpeciesId, slotLevelMax);
        }

        RNG::EncounterSlot4Parameters EncounterSlot4::getSlotParameters() const noexcept
        {
            RNG::EncounterSlot4Parameters parameters;
            parameters.species = slotSpeciesId;
            parameters.form = slotFormId;
            parameters.levelMin = slotLevelMin;
            parameters.levelMax = slotLevelMax;
            parameters.slotNumber = slotSlotNumber;
            parameters.staticIndex = slotStaticIndex;
            parameters.magnetPullIndex = slotMagnetPullIndex;
            parameters.staticCount = slotStaticCount;
            parameters.magnetPullCount = slotMagnetPullCount;
            parameters.pressureLevel = pressureLevel();
            parameters.areaRate = parent->rate();
            parameters.type = parent->type();
            parameters.isRerollMinimum31 = isRerollMinimum31();
            parameters.canUseRadar = canUseRadar();
            parameters.isCoronetFeebasArea = parent->isCoronetFeebasArea();
            return parameters;
        }
    }
}
