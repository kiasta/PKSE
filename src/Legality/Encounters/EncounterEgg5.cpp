#include "Legality/Encounters/EncounterEgg5.h"

#include "Legality/Verifiers/BreedingRules.h"

namespace Legality
{
    namespace Encounters
    {
        RNG::AbilityPermission EncounterEgg5::ability() const noexcept
        {
            return isHiddenAbilityBreedable(5, eggSpeciesId, eggFormId) ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12;
        }

        bool EncounterEgg5::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
