#include "Legality/Encounters/EncounterGenerator.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator12.h"
#include "Legality/Encounters/EncounterGenerator3.h"
#include "Legality/Encounters/EncounterGenerator3GC.h"
#include "Legality/Encounters/EncounterGenerator4.h"
#include "Legality/Encounters/EncounterGenerator5.h"
#include "Legality/Encounters/EncounterGenerator6.h"
#include "Legality/Encounters/EncounterGenerator7.h"
#include "Legality/Encounters/EncounterGenerator8.h"
#include "Legality/Encounters/EncounterGenerator9.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX GameVersion.CXD: Colosseum and XD share one stored version id.
            constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        }

        EncounterCandidates EncounterGenerator::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            switch (info.generation)
            {
            case 1:
            case 2:
                return EncounterGenerator12::getEncounters(pokemon, info);
            case 3:
                if (getOriginVersion(pokemon) == VERSION_COLOSSEUM_XD)
                {
                    return EncounterGenerator3GC::getEncounters(pokemon, info);
                }
                return EncounterGenerator3::getEncounters(pokemon, info);
            case 4:
                return EncounterGenerator4::getEncounters(pokemon, info);
            case 5:
                return EncounterGenerator5::getEncounters(pokemon, info);
            case 6:
                return EncounterGenerator6::getEncounters(pokemon, info);
            case 7:
                return EncounterGenerator7X::getEncounters(pokemon, info);
            case 8:
                return EncounterGenerator8X::getEncounters(pokemon, info);
            case 9:
                return EncounterGenerator9X::getEncounters(pokemon, info);
            default:
                // PKHeX EncounterGeneratorDummy: yields nothing.
                return EncounterCandidates();
            }
        }

    }
}
