/**
 * A seed that starts an encounter, with the lead its frames required.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/LeadSeed.cs.
 */
#ifndef LEGALITY_RNG_LEADSEED_H
#define LEGALITY_RNG_LEADSEED_H

#include <cstdint>

#include "Legality/RNG/LeadRequired.h"

namespace Legality
{
    namespace RNG
    {
        struct LeadSeed
        {
            uint32_t seed = 0;
            LeadRequired lead = LeadRequired::Invalid;

            constexpr bool isNoRequirement() const noexcept { return lead == LeadRequired::None; }
            constexpr bool isValid() const noexcept { return lead != LeadRequired::Invalid; }
            constexpr bool isBetterThan(const LeadSeed &other) const noexcept { return lead > other.lead; }
        };
    }
}

#endif
