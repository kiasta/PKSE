/**
 * Narrows a Gen 3 event's BACD correlation to the distribution it came from (restricted seed,
 * Mystery Mew, Channel Jirachi, the weighted table events), rewriting the PIDIV's mutated type.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/CommonEvent3Checker.cs. `observed` is
 * the type MethodFinder reported.
 */
#ifndef LEGALITY_RNG_COMMONEVENT3CHECKER_H
#define LEGALITY_RNG_COMMONEVENT3CHECKER_H

#include <cstdint>

#include "Legality/RNG/PIDIV.h"

namespace Legality
{
    namespace RNG
    {
        struct CommonEvent3Checker
        {
            /// The weighted table events, not shiny.
            static bool isRestrictedTable2(PIDIV &value, PIDType observed, uint16_t species, bool isWish) noexcept;
            /// The weighted table events, shiny.
            static bool isRestrictedTable3(PIDIV &value, PIDType observed, uint16_t species, bool isWish) noexcept;
            static bool isMystryMew(PIDIV &value, PIDType observed) noexcept;
            static bool isChannelJirachi(PIDIV &value, PIDType observed) noexcept;
            static bool isRestrictedSimple(PIDIV &value, PIDType observed) noexcept;
            static bool isUnrestrictedAnti(PIDIV &value, PIDType observed) noexcept;
            static bool isRestrictedAnti(PIDIV &value, PIDType observed) noexcept;
            static bool isUnrestrictedAntiX(PIDIV &value, PIDType observed) noexcept;
            static bool isBerryFixShiny(PIDIV &value, PIDType observed) noexcept;
        };
    }
}

#endif
