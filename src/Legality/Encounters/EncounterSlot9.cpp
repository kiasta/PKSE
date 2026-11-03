#include "Legality/Encounters/EncounterSlot9.h"

#include <string>

#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint16_t SPECIES_DEERLING = 585;
            constexpr uint16_t SPECIES_SAWSBUCK = 586;
            constexpr uint16_t SPECIES_ORICORIO = 741;
            constexpr uint16_t SPECIES_KINGAMBIT = 983;
            constexpr uint16_t SPECIES_ANNIHILAPE = 979;

            // PKHeX EncounterSlot9.GetTime: the bit of the time byte each time-of-day mark reads.
            constexpr int TIME_LUNCHTIME = 0;
            constexpr int TIME_SLEEPY_TIME = 1;
            constexpr int TIME_DUSK = 2;
            constexpr int TIME_DAWN = 3;

            // PKHeX EncounterSlot9.IsFormOkayWild
            bool isFormOkayWild(uint16_t speciesId, uint8_t formId) noexcept
            {
                switch (speciesId)
                {
                case SPECIES_ROTOM:
                    return formId <= 5;
                case SPECIES_DEERLING:
                case SPECIES_SAWSBUCK:
                    return formId < 4;
                case SPECIES_ORICORIO:
                    return formId < 4;
                default:
                    return false;
                }
            }

            /// PKHeX IsFormArgDeferralRelevant: the species whose form argument counts what it did to evolve.
            bool isFormArgumentDeferralRelevant(uint16_t speciesId) noexcept
            {
                return speciesId == SPECIES_KINGAMBIT || speciesId == SPECIES_ANNIHILAPE;
            }
        }

        std::string EncounterSlot9::name() const
        {
            return std::string("Wild Encounter (") + getVersionName9(version()) + ")";
        }

        // PKHeX EncounterSlot9.IsMatchExact
        bool EncounterSlot9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (slotFormId != evolution.formId && !isRandomUnspecificForm() && !isFormOkayWild(slotSpeciesId, evolution.formId))
            {
                return false;
            }
            if (slotGender != GENDER_RANDOM && pokemon.gender() != slotGender)
            {
                return false;
            }
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (pokemon.hasTeraType())
            {
                const uint8_t originalTeraType = pokemon.teraTypeOriginal();
                const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(Enums::GameVersion::SV, slotSpeciesId, slotFormId);
                const uint8_t secondType = record.type2 == Pokemon::PERSONAL_TYPE_NONE ? record.type1 : record.type2;
                if (record.type1 != originalTeraType && secondType != originalTeraType)
                {
                    return false;
                }
            }
            return true;
        }

        // PKHeX EncounterSlot9.GetMatchRating
        EncounterMatchRating EncounterSlot9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isPartialMatchHiddenAbility(pokemon, slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const RibbonMarks9 marks = readRibbonMarks9(pokemon);
            if (marks.isPresent && marks.hasEncounterMark)
            {
                if (marks.lunchtime && !canSpawnAtTime(TIME_LUNCHTIME))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                if (marks.sleepyTime && !canSpawnAtTime(TIME_SLEEPY_TIME))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                if (marks.dusk && !canSpawnAtTime(TIME_DUSK))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                if (marks.dawn && !canSpawnAtTime(TIME_DAWN))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                // A met location shared by a desert and a snow area satisfies both; a weather mark
                // the chosen area cannot have defers to the other.
                if (marks.weather != WeatherMark::None && !isMarkCompatible9(slotWeather, marks.weather))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
            }
            if (isFormArgumentDeferralRelevant(pokemon.speciesID()) && pokemon.hasFormArgument())
            {
                const bool isFormArgumentZero = pokemon.formArgument() == 0;
                const bool mustBeZero = isFormArgumentDeferralRelevant(slotSpeciesId);
                if (isFormArgumentZero != mustBeZero)
                {
                    return EncounterMatchRating::DeferredErrors;
                }
            }
            return EncounterMatchRating::Match;
        }

        const EncounterSlot9 &EncounterArea9::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }
    }
}
