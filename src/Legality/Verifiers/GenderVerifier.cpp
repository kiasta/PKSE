// Port of PKHeX PKHeX.Core/Legality/Verifiers/GenderVerifier.cs: gender, and the PID's gender and nature in Gens 3-5.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t MARILL_SPECIES = 183;
        constexpr uint16_t AZUMARILL_SPECIES = 184;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint8_t GENDER_RATIO_MALE_ONLY = 0;
        constexpr uint8_t GENDER_RATIO_HALF = 127;
        constexpr uint8_t GENDER_RATIO_MOSTLY_FEMALE = 191;
        constexpr uint8_t GENDER_RATIO_FEMALE_ONLY = 254;
        constexpr uint8_t GENDER_RATIO_GENDERLESS = 255;
        constexpr uint8_t GENDER_MALE = 0;
        constexpr uint8_t GENDER_FEMALE = 1;
        constexpr uint8_t GENDER_GENDERLESS = 2;

        /// The gender ratio in PKHeX's personal table for the format (LegalityAnalysis.PersonalInfo). It
        /// differs from Pokemon::getGenderRatio in two places: the Gen 6 and 7 tables give both Meowstic
        /// forms the dual ratio, and Omega Ruby/Alpha Sapphire's gives the Cosplay Pikachu forms the dual
        /// ratio although the game only hands them out female.
        uint8_t getFormatGenderRatio(const AnalysisContext &context)
        {
            constexpr uint16_t pikachuSpecies = 25;
            constexpr uint8_t lastCosplayForm = 6;
            constexpr uint16_t meowsticSpecies = 678;
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t speciesId = pokemon.speciesID();
            if (speciesId == meowsticSpecies && (context.formatGeneration == 6 || context.formatGeneration == 7))
            {
                return GENDER_RATIO_HALF;
            }
            if (speciesId == pikachuSpecies && pokemon.form() >= 1 && pokemon.form() <= lastCosplayForm && context.formatGroup == Enums::GameVersion::ORAS)
            {
                return GENDER_RATIO_HALF;
            }
            return Pokemon::getGenderRatio(speciesId, pokemon.form(), context.formatGroup);
        }

        /// PKHeX EntityGender.GetFromPIDAndRatio.
        uint8_t getGenderFromPID(uint32_t PID, uint8_t genderRatio)
        {
            switch (genderRatio)
            {
            case GENDER_RATIO_GENDERLESS:
                return GENDER_GENDERLESS;
            case GENDER_RATIO_FEMALE_ONLY:
                return GENDER_FEMALE;
            case GENDER_RATIO_MALE_ONLY:
                return GENDER_MALE;
            default:
                return (PID & 0xFF) < genderRatio ? GENDER_FEMALE : GENDER_MALE;
            }
        }

        /// PKHeX SpeciesCategory.IsFixedGenderFromDual: a single-gender evolution of a dual-gender species.
        bool isFixedGenderFromDual(uint16_t speciesId)
        {
            constexpr uint16_t fixedGenderSpecies[] = {
                292,  // Shedinja
                413,  // Wormadam
                414,  // Mothim
                416,  // Vespiquen
                475,  // Gallade
                478,  // Froslass
                678,  // Meowstic
                758,  // Salazzle
                916,  // Oinkologne
                902,  // Basculegion
            };
            for (const uint16_t fixedGenderSpeciesId : fixedGenderSpecies)
            {
                if (speciesId == fixedGenderSpeciesId)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX GenderVerifier.IsValidGenderMismatch: evolutions whose PID gender was read with the
        /// pre-evolution's ratio.
        bool isValidGenderMismatch(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t speciesId = pokemon.speciesID();
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            // D/P/Pt and HG/SS do not make an evolved Shedinja genderless: it keeps Nincada's gender.
            if (speciesId == SHEDINJA_SPECIES && context.formatGeneration == 4)
            {
                return pokemon.gender() == getGenderFromPID(encryptionConstant, GENDER_RATIO_HALF);
            }
            // An Azurill evolved where gender still follows the PID "changes" gender under Marill's ratio.
            if ((speciesId == MARILL_SPECIES || speciesId == AZUMARILL_SPECIES) && context.formatGeneration >= 6)
            {
                const uint8_t genderValue = static_cast<uint8_t>(encryptionConstant & 0xFF);
                return pokemon.gender() == GENDER_FEMALE && genderValue >= GENDER_RATIO_HALF && genderValue < GENDER_RATIO_MOSTLY_FEMALE;
            }
            return false;
        }

        /// PKHeX PKM.IsGenderValid, for a Gen 3-5 origin, where the gender follows the PID.
        bool isGenderValidForPID(const Pokemon::Pokemon &pokemon, uint8_t genderRatio)
        {
            const uint8_t gender = pokemon.gender();
            if (genderRatio == GENDER_RATIO_GENDERLESS)
            {
                return gender == GENDER_GENDERLESS;
            }
            if (genderRatio == GENDER_RATIO_FEMALE_ONLY)
            {
                return gender == GENDER_FEMALE;
            }
            if (genderRatio == GENDER_RATIO_MALE_ONLY)
            {
                return gender == GENDER_MALE;
            }
            if (gender >= GENDER_GENDERLESS)
            {
                return false;
            }
            return gender == getGenderFromPID(pokemon.pid(), genderRatio);
        }

        /// PKHeX GenderVerifier.IsValidGenderPID.
        bool isValidGenderPID(const AnalysisContext &context, uint8_t genderRatio)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (!isGenderValidForPID(pokemon, genderRatio))
            {
                return isValidGenderMismatch(context);
            }
            if (!isFixedGenderFromDual(pokemon.speciesID()))
            {
                return true;
            }
            // The gender was decided by the species the Pokemon was encountered as; an unknown
            // encounter leaves that unknown.
            if (pokemon.gender() == GENDER_GENDERLESS)
            {
                return true;
            }
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr)
            {
                return true;
            }
            const uint8_t originalRatio = Pokemon::getGenderRatio(encounterMatch->speciesId(), 0, context.formatGroup);
            return getGenderFromPID(pokemon.encryptionConstant(), originalRatio) == pokemon.gender();
        }
    }

    void verifyGender(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const uint8_t genderRatio = getFormatGenderRatio(context);
        const uint8_t gender = pokemon.gender();
        const bool isGenderless = genderRatio == GENDER_RATIO_GENDERLESS;
        if (isGenderless != (gender == GENDER_GENDERLESS))
        {
            // D/P/Pt and HG/SS's Shedinja keeps Nincada's gender when it evolves.
            const bool isShedinjaGlitch = context.formatGeneration == 4 && pokemon.speciesID() == SHEDINJA_SPECIES && pokemon.metLevel() != Evolutions::getEntityCurrentLevel(pokemon);
            if (!isShedinjaGlitch)
            {
                context.add(Severity::Invalid, CheckIdentifier::Gender, ResultCode::GenderInvalidNone);
            }
            return;
        }

        const int originGeneration = getOriginGeneration(context);
        if (originGeneration >= 3 && originGeneration <= 5)
        {
            if (!isValidGenderPID(context, genderRatio))
            {
                context.add(Severity::Invalid, CheckIdentifier::Gender, ResultCode::PIDGenderMismatch);
            }
            constexpr uint32_t natureCount = 25;
            if (originGeneration != 5 && pokemon.encryptionConstant() % natureCount != pokemon.nature())
            {
                context.add(Severity::Invalid, CheckIdentifier::Nature, ResultCode::PIDNatureMismatch);
            }
            return;
        }

        if ((genderRatio == GENDER_RATIO_FEMALE_ONLY && gender != GENDER_FEMALE) || (genderRatio == GENDER_RATIO_MALE_ONLY && gender != GENDER_MALE))
        {
            context.add(Severity::Invalid, CheckIdentifier::Gender, ResultCode::GenderInvalidNone);
        }
    }
}
