/**
 * Species a game cannot produce itself, which can only have reached it by trade (PKHeX Legal.cs
 * IsForeign*). Shared by the transfer and per-format miscellaneous verifiers.
 */
#ifndef LEGALITY_VERIFIERS_FOREIGNSPECIES_H
#define LEGALITY_VERIFIERS_FOREIGNSPECIES_H

#include <cstdint>

namespace Legality
{
    /// PKHeX Legal.IsForeignFRLG: a species FireRed/LeafGreen cannot obtain on its own (foreign
    /// transfers and time-of-day evolutions).
    bool isForeignToFireRedLeafGreen(uint16_t speciesId);
}

#endif
