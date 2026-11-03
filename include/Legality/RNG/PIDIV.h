/**
 * What MethodFinder traced a PID (and IVs) to: the correlation type and the seed behind it.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/PIDIV.cs. PKHeX overlays three fields onto shared
 * storage, and this keeps the overlay: seed64() is originSeed (low) and encounterSeed (high), and
 * the lead an encounter needed and a mutated type share one byte.
 */
#ifndef LEGALITY_RNG_PIDIV_H
#define LEGALITY_RNG_PIDIV_H

#include <cstdint>

#include "Legality/RNG/LeadRequired.h"
#include "Legality/RNG/LeadSeed.h"
#include "Legality/RNG/PIDType.h"

namespace Legality
{
    namespace RNG
    {
        struct PIDIV
        {
            /// The seed that immediately generates the PID/IV, whichever comes first.
            uint32_t originSeed = 0;
            /// The seed that starts the encounter routine.
            uint32_t encounterSeed = 0;
            PIDType type = PIDType::None;
            /// Holds a LeadRequired after asEncounteredVia() and a PIDType after asMutated().
            uint8_t leadOrMutatedType = 0;

            constexpr PIDIV() noexcept = default;
            constexpr explicit PIDIV(PIDType pidType, uint32_t seed = 0) noexcept : originSeed(seed), type(pidType) {}

            static constexpr PIDIV fromSeed64(PIDType pidType, uint64_t seed) noexcept
            {
                PIDIV result(pidType, static_cast<uint32_t>(seed));
                result.encounterSeed = static_cast<uint32_t>(seed >> 32);
                return result;
            }

            constexpr uint64_t seed64() const noexcept { return (static_cast<uint64_t>(encounterSeed) << 32) | originSeed; }
            constexpr LeadRequired lead() const noexcept { return static_cast<LeadRequired>(leadOrMutatedType); }
            constexpr PIDType mutated() const noexcept { return static_cast<PIDType>(leadOrMutatedType); }

            /// PKHeX GetDerivedType: the mutated type when there is one.
            constexpr PIDType derivedType() const noexcept { return leadOrMutatedType == 0 ? type : mutated(); }

            /// True when no single origin seed is meaningful: the PID follows a pattern many seeds share.
            constexpr bool hasNoSeed() const noexcept { return type == PIDType::None || type == PIDType::Pokewalker || type == PIDType::Gen5MysteryGiftShiny; }

            constexpr bool isSeed64() const noexcept { return type == PIDType::Xoroshiro; }

            constexpr PIDIV asEncounteredVia(const LeadSeed &condition) const noexcept
            {
                PIDIV result = *this;
                result.leadOrMutatedType = static_cast<uint8_t>(condition.lead);
                result.encounterSeed = condition.seed;
                return result;
            }

            constexpr PIDIV asMutated(PIDType mutatedType, uint32_t origin) const noexcept
            {
                PIDIV result = *this;
                result.leadOrMutatedType = static_cast<uint8_t>(mutatedType);
                result.encounterSeed = origin;
                return result;
            }

            constexpr PIDIV asMutated(PIDType mutatedType) const noexcept { return asMutated(mutatedType, originSeed); }
        };
    }
}

#endif
