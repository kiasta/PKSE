#include "Legality/Encounters/MysteryGiftPCD.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/RNG/ARNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_MILOTIC = 350;
            constexpr uint16_t EVENT_LOCATION_OFFSET4 = 3000;

            int8_t getPackedIndividualValue(uint32_t packedIVs, int shift) noexcept
            {
                return static_cast<int8_t>((packedIVs >> shift) & 0x1F);
            }
        }

        // PKHeX PGT.Shiny
        RNG::Shiny MysteryGiftPCD::shiny() const noexcept
        {
            if (properties.isEgg)
            {
                return RNG::Shiny::Random;
            }
            if (isRandomPID())
            {
                return RNG::Shiny::Never;
            }
            return properties.isShiny ? RNG::Shiny::Always : RNG::Shiny::Never;
        }

        // PKHeX PGT.GetIVs: HP, Atk, Def, Spe, SpA, SpD in IV32 order.
        RNG::IndividualValueSet MysteryGiftPCD::fixedIVs() const noexcept
        {
            if (properties.packedIVs == 0)
            {
                return RNG::IndividualValueSet{};
            }
            RNG::IndividualValueSet individualValues;
            individualValues.HP = getPackedIndividualValue(properties.packedIVs, 0);
            individualValues.ATK = getPackedIndividualValue(properties.packedIVs, 5);
            individualValues.DEF = getPackedIndividualValue(properties.packedIVs, 10);
            individualValues.SPE = getPackedIndividualValue(properties.packedIVs, 15);
            individualValues.SPA = getPackedIndividualValue(properties.packedIVs, 20);
            individualValues.SPD = getPackedIndividualValue(properties.packedIVs, 25);
            individualValues.isSpecified = true;
            return individualValues;
        }

        // PKHeX PCD.IsMatchExact
        bool MysteryGiftPCD::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isFormat4 = getFormatGeneration(pokemon) == 4;
            const uint16_t eventLocation = static_cast<uint16_t>(properties.eventLocationIndex + EVENT_LOCATION_OFFSET4);
            if (!properties.isEgg)
            {
                if (properties.TID16 != pokemon.tid16() || properties.SID16 != pokemon.sid16())
                {
                    return false;
                }
                if (pokemon.otName() != std::u16string(properties.trainerName))
                {
                    return false;
                }
                if (properties.trainerGender != pokemon.otGender())
                {
                    return false;
                }
                if (properties.language != 0 && properties.language != pokemon.language())
                {
                    return false;
                }
                if (!isFormat4)
                {
                    // The met location is checked with the other transfer rules.
                    if (properties.currentLevel > pokemon.metLevel())
                    {
                        return false;
                    }
                    if (pokemon.eggLocation() != eggLocation())
                    {
                        return false;
                    }
                }
                else
                {
                    if (eventLocation != pokemon.metLocation() || properties.currentLevel != pokemon.metLevel())
                    {
                        return false;
                    }
                }
            }
            else
            {
                if (eventLocation != pokemon.eggLocation() && pokemon.eggLocation() != LOCATION_LINK_TRADE4)
                {
                    return false;
                }
                if (properties.currentLevel != pokemon.metLevel())
                {
                    return false;
                }
                if (pokemon.isEgg() && !isFormat4)
                {
                    return false;
                }
            }

            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::DP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.ball != pokemon.ball())
            {
                return false;
            }
            if (properties.trainerGender < 3 && properties.trainerGender != pokemon.otGender())
            {
                return false;
            }
            // Milotic is the only gift with contest stats.
            if (properties.speciesId == SPECIES_MILOTIC && pokemon.hasContestStats())
            {
                for (int statIndex = 0; statIndex < static_cast<int>(properties.contestStats.size()); statIndex++)
                {
                    if (pokemon.contestStat(statIndex) < properties.contestStats[statIndex])
                    {
                        return false;
                    }
                }
            }
            if (isRandomPID())
            {
                // A random PID is never shiny. Its gender is not checked: the Korean Arcanine can be
                // either, whatever the template says.
                return !isShiny(pokemon);
            }
            return properties.gender == pokemon.gender();
        }

        // PKHeX MysteryGift.GetMatchRating with PCD's IsMatchPartial (never Deferred)
        EncounterMatchRating MysteryGiftPCD::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (getOriginVersion(pokemon) != properties.version)
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX PGT.IsCompatible for a gift that is not the Manaphy egg
        RandomCorrelationRating MysteryGiftPCD::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const
        {
            if (properties.PID != 1 && type == RNG::PIDType::Gen5MysteryGiftShiny)
            {
                return RandomCorrelationRating::Match;
            }
            return type == RNG::PIDType::None ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }

        // PKHeX PGT.IsRangerManaphy
        bool MysteryGiftPGT::isRangerManaphy(const Pokemon::Pokemon &pokemon)
        {
            if (pokemon.speciesID() != SPECIES_MANAPHY)
            {
                return false;
            }
            // Never Korean or a later language; 0 and 6 are flagged by other checks.
            if (pokemon.language() >= static_cast<uint8_t>(Enums::LanguageID::Korean))
            {
                return false;
            }
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!pokemon.isEgg())
            {
                return eggLocation == LOCATION_LINK_TRADE4 || eggLocation == LOCATION_RANGER4;
            }
            if (getFormatGeneration(pokemon) != 4)
            {
                return false;
            }
            // Trading the egg moves its met location from 0 to Link Trade; the egg location stays Ranger.
            if (eggLocation != LOCATION_RANGER4)
            {
                return false;
            }
            const uint16_t metLocation = pokemon.metLocation();
            return metLocation == LOCATION_LINK_TRADE4 || metLocation == 0;
        }

        // PKHeX PGT.IsCompatible and IsG4ManaphyPIDValid
        RandomCorrelationRating MysteryGiftPGT::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            const bool wasTradedEgg = pokemon.eggLocation() == LOCATION_LINK_TRADE4;
            // A shiny can roll ARNG on receipt, and an untraded egg on hatching.
            if (type == RNG::PIDType::Method1)
            {
                const bool isValid = pokemon.isEgg() ? !isShiny(pokemon) : (wasTradedEgg || !isShiny(pokemon));
                return isValid ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (type != RNG::PIDType::Gen4MysteryGiftAntishiny)
            {
                return RandomCorrelationRating::Mismatch;
            }
            // A hatched traded egg is not rerolled for its new trainer.
            if (!pokemon.isEgg() && wasTradedEgg)
            {
                return RandomCorrelationRating::Match;
            }
            // Anti-shiny: the PID before the reroll was shiny for the current trainer.
            const uint32_t originalPID = RNG::ARNG::previous(pokemon.pid());
            const uint32_t trainerShinyValue = static_cast<uint32_t>(pokemon.tid16() ^ pokemon.sid16()) >> 3;
            const uint32_t pidShinyValue = ((originalPID & 0xFFFF) ^ (originalPID >> 16)) >> 3;
            return trainerShinyValue == pidShinyValue ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
        }
    }
}
