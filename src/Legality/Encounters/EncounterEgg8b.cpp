#include "Legality/Encounters/EncounterEgg8b.h"

#include "Legality/Verifiers/BreedingRules.h"

namespace Legality
{
    namespace Encounters
    {
        RNG::AbilityPermission EncounterEgg8b::ability() const noexcept
        {
            return isHiddenAbilityBreedable(8, eggSpeciesId, eggFormId) ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12;
        }

        bool EncounterEgg8b::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
