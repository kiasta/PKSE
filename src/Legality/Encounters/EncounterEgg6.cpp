#include "Legality/Encounters/EncounterEgg6.h"

#include "Legality/Verifiers/BreedingRules.h"

namespace Legality
{
    namespace Encounters
    {
        RNG::AbilityPermission EncounterEgg6::ability() const noexcept
        {
            return isHiddenAbilityBreedable(6, eggSpeciesId, eggFormId) ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12;
        }

        bool EncounterEgg6::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
