#include "Legality/Encounters/EncounterEgg4.h"

namespace Legality
{
    namespace Encounters
    {
        bool EncounterEgg4::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu;
        }
    }
}
