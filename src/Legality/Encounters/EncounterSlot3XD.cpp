#include "Legality/Encounters/EncounterSlot3XD.h"

#include "Legality/Encounters/EncounterArea3XD.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX PokeSpotArea.ToString().
            const char *getPokeSpotName(PokeSpotArea area) noexcept
            {
                switch (area)
                {
                case PokeSpotArea::Rock:
                    return "Rock";
                case PokeSpotArea::Oasis:
                    return "Oasis";
                case PokeSpotArea::Cave:
                    return "Cave";
                }
                return "";
            }
        }

        std::string EncounterSlot3XD::longName() const
        {
            return name() + " - " + getPokeSpotName(parent->type()) + " Spot";
        }

        uint16_t EncounterSlot3XD::location() const noexcept
        {
            return parent->location();
        }
    }
}
