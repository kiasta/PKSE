/**
 * Where an evolution search starts: the most evolved species the encounter could have been, its
 * context and generation, and the level bounds. Ported from PKHeX
 * PKHeX.Core/Legality/Evolutions/EvolutionGroup/EvolutionOrigin.cs.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONORIGIN_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONORIGIN_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Evolutions
    {
        /// PKHeX OriginOptions flags.
        enum OriginOption : uint8_t
        {
            ORIGIN_OPTION_NONE = 0,
            /// Evolution conditions that read the entity (gender, trades, contest stats) pass.
            ORIGIN_OPTION_SKIP_CHECKS = 1 << 0,
            /// A Game Boy origin must not jump to the other Game Boy generation to keep devolving.
            ORIGIN_OPTION_FORCE_DISCARD = 1 << 1,
            /// Searching from an encounter template rather than a Pokemon.
            ORIGIN_OPTION_ENCOUNTER_TEMPLATE = ORIGIN_OPTION_SKIP_CHECKS | ORIGIN_OPTION_FORCE_DISCARD,
        };

        struct EvolutionOrigin
        {
            uint16_t speciesId = 0;
            /// A context group (EVOLUTION_CONTEXT_GROUPS).
            Enums::GameVersion contextGroup = Enums::GameVersion::Invalid;
            uint8_t generation = 0;
            uint8_t levelMin = 0;
            uint8_t levelMax = 0;
            /// OR of OriginOption.
            uint8_t options = ORIGIN_OPTION_NONE;

            bool shouldSkipChecks() const noexcept { return (options & ORIGIN_OPTION_SKIP_CHECKS) != 0; }

            /// PKHeX EvolutionOrigin.IsDiscardRequired.
            bool isDiscardRequired(int formatGeneration) const noexcept { return formatGeneration <= 2 && (options & ORIGIN_OPTION_FORCE_DISCARD) != 0; }
        };
    }
}

#endif
