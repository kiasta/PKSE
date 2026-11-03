#include "Legality/Encounters/EncounterEgg3.h"

namespace Legality
{
    namespace Encounters
    {
        bool EncounterEgg3::canHaveVoltTackle() const noexcept
        {
            constexpr uint16_t speciesPichu = 172;
            return eggSpeciesId == speciesPichu && eggVersion == Enums::GameVersion::EM;
        }
    }
}
