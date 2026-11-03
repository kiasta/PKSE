/**
 * The RNG patterns a Legends: Z-A encounter follows when it generates its values. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9a/LumioseCorrelation.cs.
 */
#ifndef LEGALITY_RNG_LUMIOSECORRELATION_H
#define LEGALITY_RNG_LUMIOSECORRELATION_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        enum class LumioseCorrelation : uint8_t
        {
            /// Every value enters the game's FixInitSpec unset, and none is overwritten after.
            Normal = 0,
            /// The rolled IVs are discarded and reapplied from a separate, unrelated seed.
            ReApplyIVs = 1,
            /// A separate, unrelated seed picks the flawless IVs before FixInitSpec runs.
            PreApplyIVs = 2,
            /// No fake trainer id is rolled: the real trainer id is already known to FixInitSpec.
            SkipTrainer = 3,
        };
    }
}

#endif
