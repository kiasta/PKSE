#include "Legality/Encounters/EncounterEgg9.h"

#include "Legality/Verifiers/BreedingRules.h"

namespace Legality
{
    namespace Encounters
    {
        RNG::AbilityPermission EncounterEgg9::ability() const noexcept
        {
            return isHiddenAbilityBreedable(9, eggSpeciesId, eggFormId) ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12;
        }

        bool EncounterEgg9::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
