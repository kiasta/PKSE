#include "Legality/Encounters/EncounterSlot3.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
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
            constexpr uint16_t SPECIES_WURMPLE = 265;
            constexpr uint16_t SPECIES_SILCOON = 266;
            constexpr uint16_t SPECIES_BEAUTIFLY = 267;
            constexpr uint16_t SPECIES_CASCOON = 268;
            constexpr uint16_t SPECIES_DUSTOX = 269;

            // PKHeX SlotType3 names with '_' shown as a space.
            const char *getSlotTypeName(RNG::SlotType3 type) noexcept
            {
                switch (type)
                {
                case RNG::SlotType3::Grass:
                    return "Grass";
                case RNG::SlotType3::Surf:
                    return "Surf";
                case RNG::SlotType3::OldRod:
                    return "Old Rod";
                case RNG::SlotType3::GoodRod:
                    return "Good Rod";
                case RNG::SlotType3::SuperRod:
                    return "Super Rod";
                case RNG::SlotType3::RockSmash:
                    return "Rock Smash";
                case RNG::SlotType3::SwarmGrass50:
                    return "SwarmGrass50";
                case RNG::SlotType3::SwarmFish50:
                    return "SwarmFish50";
                }
                return "";
            }

            // PKHeX WurmpleUtil.GetWurmpleEvoGroup: 0 Silcoon line, 1 Cascoon line, -1 other.
            int getWurmpleEvolutionGroup(uint16_t speciesId) noexcept
            {
                if (speciesId == SPECIES_SILCOON || speciesId == SPECIES_BEAUTIFLY)
                {
                    return 0;
                }
                if (speciesId == SPECIES_CASCOON || speciesId == SPECIES_DUSTOX)
                {
                    return 1;
                }
                return -1;
            }
        }

        const EncounterSlot3 &EncounterArea3::slotAt(uint16_t slotIndex) const noexcept
        {
            if (swarmSlots != nullptr)
            {
                return swarmSlots[slotIndex];
            }
            return regularSlots[slotIndex];
        }

        // PKHeX EncounterArea3.GetPressureMax
        uint8_t EncounterArea3::getPressureMax(uint16_t speciesId, uint8_t levelMax) const noexcept
        {
            for (uint16_t slotIndex = 0; slotIndex < areaSlotCount; slotIndex++)
            {
                const EncounterSlot3 &slot = slotAt(slotIndex);
                if (slot.speciesId() != speciesId)
                {
                    continue;
                }
                if (slot.levelMax() < levelMax)
                {
                    continue;
                }
                levelMax = slot.levelMax();
            }
            return levelMax;
        }

        std::string EncounterSlot3::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot3::longName() const
        {
            return name() + " " + getSlotTypeName(parent->type());
        }

        bool EncounterSlot3::isSafari() const noexcept
        {
            return isSafariZoneLocation3(parent->location());
        }

        bool EncounterSlot3::isSafariHoenn() const noexcept
        {
            return isSafariZoneLocation3RSE(parent->location());
        }

        // PKHeX EncounterSlot3.IsMatchExact
        bool EncounterSlot3::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (slotFormId != evolution.formId && slotSpeciesId != SPECIES_BURMY)
            {
                return false;
            }
            if (getFormatGeneration(pokemon) == 3)
            {
                return isLevelWithinRange(pokemon.metLevel());
            }
            return evolution.levelMax >= slotLevelMin;
        }

        // PKHeX EncounterSlot3.GetMatchRating
        EncounterMatchRating EncounterSlot3::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isSafariBall = pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari);
            if (isSafariBall != isSafari())
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isDeferredWurmple = slotSpeciesId == SPECIES_WURMPLE && pokemon.speciesID() != SPECIES_WURMPLE && Pokemon::wurmpleEvolutionFromPID(pokemon.encryptionConstant()) != getWurmpleEvolutionGroup(pokemon.speciesID());
            if (isDeferredWurmple)
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterSlot3.PressureLevel
        uint8_t EncounterSlot3::pressureLevel() const noexcept
        {
            if (parent->type() != RNG::SlotType3::Grass)
            {
                return slotLevelMax;
            }
            return parent->getPressureMax(slotSpeciesId, slotLevelMax);
        }

        // PKHeX EncounterSlot3.IsCompatible
        RandomCorrelationRating EncounterSlot3::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            (void)pokemon;
            using RNG::PIDType;
            const bool isMatch = slotSpeciesId != SPECIES_UNOWN ? (type == PIDType::Method1 || type == PIDType::Method2 || type == PIDType::Method3 || type == PIDType::Method4) : (type == PIDType::Method1Unown || type == PIDType::Method2Unown || type == PIDType::Method3Unown || type == PIDType::Method4Unown);
            return isMatch ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }

        RNG::PIDType EncounterSlot3::getSuggestedCorrelation() const noexcept
        {
            return slotSpeciesId == SPECIES_UNOWN ? RNG::PIDType::Method1Unown : RNG::PIDType::Method1;
        }

        RNG::EncounterSlot3Parameters EncounterSlot3::getSlotParameters() const noexcept
        {
            RNG::EncounterSlot3Parameters parameters;
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
            parameters.isSafariHoenn = isSafariHoenn();
            return parameters;
        }
    }
}
