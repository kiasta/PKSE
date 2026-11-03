#include "Legality/RNG/CuteCharm4.h"

#include "Enums/GameVersion.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/PIDSearch.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t NATURE_COUNT = 25;
            constexpr uint16_t MAX_SPECIES_ID_4 = 493;
            constexpr uint8_t FEMALE_ONLY_RATIO = 254;

            constexpr uint16_t EEVEE_SPECIES = 133;
            constexpr uint16_t SCYTHER_SPECIES = 123;
            constexpr uint16_t PRIMEAPE_SPECIES = 57;
            constexpr uint16_t MR_MIME_SPECIES = 122;
            constexpr uint16_t STANTLER_SPECIES = 234;
            constexpr uint16_t MARILL_SPECIES = 183;
            constexpr uint16_t AZUMARILL_SPECIES = 184;
            constexpr uint16_t SNEASEL_SPECIES = 215;
            constexpr uint16_t URSARING_SPECIES = 217;
            constexpr uint16_t KIRLIA_SPECIES = 281;
            constexpr uint16_t NINCADA_SPECIES = 290;
            constexpr uint16_t SHEDINJA_SPECIES = 292;
            constexpr uint16_t AZURILL_SPECIES = 298;
            constexpr uint16_t SNORUNT_SPECIES = 361;
            constexpr uint16_t BURMY_SPECIES = 412;
            constexpr uint16_t WORMADAM_SPECIES = 413;
            constexpr uint16_t MOTHIM_SPECIES = 414;
            constexpr uint16_t COMBEE_SPECIES = 415;
            constexpr uint16_t VESPIQUEN_SPECIES = 416;
            constexpr uint16_t GALLADE_SPECIES = 475;
            constexpr uint16_t FROSLASS_SPECIES = 478;
            constexpr uint16_t SYLVEON_SPECIES = 700;
            constexpr uint16_t WYRDEER_SPECIES = 899;
            constexpr uint16_t KLEAVOR_SPECIES = 900;
            constexpr uint16_t URSALUNA_SPECIES = 901;
            constexpr uint16_t SNEASLER_SPECIES = 903;
            constexpr uint16_t ANNIHILAPE_SPECIES = 979;
            constexpr uint16_t MR_RIME_SPECIES = 866;

            uint32_t getBufferedBasePID(uint8_t genderRatio) noexcept
            {
                return NATURE_COUNT * ((genderRatio / NATURE_COUNT) + 1);
            }

            // Within Gen 4's species the HeartGold/SoulSilver ratio; later species fall back to the
            // species' base ratio (PKHeX EntityGender.GetGenderRatio).
            uint8_t getSpeciesGenderRatio(uint16_t speciesId) noexcept
            {
                return Pokemon::getGenderRatio(speciesId, 0, speciesId <= MAX_SPECIES_ID_4 ? Enums::GameVersion::HGSS : Enums::GameVersion::SV);
            }

            bool isAzurillMale(uint32_t PID) noexcept
            {
                return PID >= 0xC8 && PID <= 0xE0;
            }

            uint16_t devolveToSpeciesFromGen4(uint16_t speciesId) noexcept
            {
                if (speciesId <= MAX_SPECIES_ID_4)
                {
                    return speciesId;
                }
                switch (speciesId)
                {
                case SYLVEON_SPECIES:
                    return EEVEE_SPECIES;
                case MR_RIME_SPECIES:
                    return MR_MIME_SPECIES;
                case WYRDEER_SPECIES:
                    return STANTLER_SPECIES;
                case KLEAVOR_SPECIES:
                    return SCYTHER_SPECIES;
                case SNEASLER_SPECIES:
                    return SNEASEL_SPECIES;
                case URSALUNA_SPECIES:
                    return URSARING_SPECIES;
                case ANNIHILAPE_SPECIES:
                    return PRIMEAPE_SPECIES;
                default:
                    return speciesId;
                }
            }

            struct SpeciesGender
            {
                uint16_t speciesId;
                uint8_t gender;
            };

            // A fixed-gender evolution leaves the PID unmodified, so the check runs as the
            // encountered species; Nincada's line ends genderless, and Azurill changes ratio.
            SpeciesGender getGenderSpecies(const Pokemon::Pokemon &pokemon, uint32_t PID, uint16_t currentSpeciesId) noexcept
            {
                switch (currentSpeciesId)
                {
                case SHEDINJA_SPECIES:
                    return SpeciesGender{NINCADA_SPECIES, Pokemon::genderFromPID(PID, getSpeciesGenderRatio(NINCADA_SPECIES))};
                case WORMADAM_SPECIES:
                    return SpeciesGender{BURMY_SPECIES, 1};
                case MOTHIM_SPECIES:
                    return SpeciesGender{BURMY_SPECIES, 0};
                case VESPIQUEN_SPECIES:
                    return SpeciesGender{COMBEE_SPECIES, 1};
                case GALLADE_SPECIES:
                    return SpeciesGender{KIRLIA_SPECIES, 0};
                case FROSLASS_SPECIES:
                    return SpeciesGender{SNORUNT_SPECIES, 1};
                case MARILL_SPECIES:
                case AZUMARILL_SPECIES:
                    if (isAzurillMale(PID))
                    {
                        return SpeciesGender{AZURILL_SPECIES, 0};
                    }
                    break;
                default:
                    break;
                }
                return SpeciesGender{devolveToSpeciesFromGen4(currentSpeciesId), pokemon.gender()};
            }

            bool isCuteCharmPID(uint32_t PID, uint8_t gender, uint16_t speciesId) noexcept
            {
                if (gender == 0)
                {
                    const uint8_t genderRatio = getSpeciesGenderRatio(speciesId);
                    if (genderRatio >= FEMALE_ONLY_RATIO)
                    {
                        return false;
                    }
                    const uint32_t natureId = PID % NATURE_COUNT;
                    return PID == CuteCharm4::getPIDMale(genderRatio, natureId);
                }
                if (gender == 1)
                {
                    if (PID >= NATURE_COUNT)
                    {
                        return false;
                    }
                    return getSpeciesGenderRatio(speciesId) < FEMALE_ONLY_RATIO;
                }
                return false;
            }
        }

        uint32_t CuteCharm4::getPIDMale(uint8_t genderRatio, uint32_t natureId) noexcept
        {
            return getBufferedBasePID(genderRatio) + natureId;
        }

        bool CuteCharm4::isValid(uint16_t encounterSpeciesId, const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint8_t gender = pokemon.gender();
            if (gender != 0 && gender != 1)
            {
                return pokemon.speciesID() == SHEDINJA_SPECIES;
            }
            const uint16_t speciesId = pokemon.speciesID();
            if (speciesId != MARILL_SPECIES && speciesId != AZUMARILL_SPECIES)
            {
                return true;
            }
            if (!isAzurillMale(pokemon.encryptionConstant()))
            {
                return true;
            }
            return encounterSpeciesId == AZURILL_SPECIES;
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/CuteCharm4.cs IsCuteCharm
        bool CuteCharm4::isCuteCharm(const Pokemon::Pokemon &pokemon, uint32_t PID) noexcept
        {
            if (PID > 0xFF)
            {
                return false;
            }
            const SpeciesGender speciesGender = getGenderSpecies(pokemon, PID, pokemon.speciesID());
            return isCuteCharmPID(PID, speciesGender.gender, speciesGender.speciesId);
        }
    }
}
