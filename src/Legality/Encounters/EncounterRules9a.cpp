#include "Legality/Encounters/EncounterRules9a.h"

#include "Legality/RNG/LumioseSolver.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon9LZA.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;
        }

        uint8_t getGenderRatio9a(uint8_t fixedGender, uint16_t speciesId, uint8_t formId) noexcept
        {
            switch (fixedGender)
            {
            case 0:
                return RATIO_MAGIC_MALE;
            case 1:
                return RATIO_MAGIC_FEMALE;
            case 2:
                return RATIO_MAGIC_GENDERLESS;
            default:
                return Pokemon::getPersonalRecord(Enums::GameVersion::ZA, speciesId, formId).genderRatio;
            }
        }

        bool isAlphaMoveFlagMissing9a(const Pokemon::Pokemon &pokemon, uint16_t speciesId, uint8_t formId) noexcept
        {
            if (pokemon.getGameGroup() != Enums::GameVersion::ZA)
            {
                return false;
            }
            const uint16_t alphaMoveId = Pokemon::getAlphaMove9LZA(speciesId, formId);
            for (int plusIndex = 0; plusIndex < Pokemon::PLUS_MOVE_COUNT_9LZA; plusIndex++)
            {
                if (Pokemon::PLUS_MOVES_9LZA[plusIndex] == alphaMoveId)
                {
                    return !static_cast<const Pokemon::Pokemon9LZA &>(pokemon).isPlusFlagSet(plusIndex);
                }
            }
            // An Alpha move missing from the Plus list has no flag to check (PKHeX would throw here).
            return false;
        }

        bool isSeedFound9a(const RNG::GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon) noexcept
        {
            uint64_t seed = 0;
            return RNG::LumioseSolver::tryGetSeed(parameters, pokemon, seed);
        }
    }
}
