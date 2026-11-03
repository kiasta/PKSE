#include "Legality/Encounters/EncounterEgg8.h"

#include "Legality/Verifiers/BreedingRules.h"

namespace Legality
{
    namespace Encounters
    {
        RNG::AbilityPermission EncounterEgg8::ability() const noexcept
        {
            return isHiddenAbilityBreedable(8, eggSpeciesId, eggFormId) ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12;
        }

        bool EncounterEgg8::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
