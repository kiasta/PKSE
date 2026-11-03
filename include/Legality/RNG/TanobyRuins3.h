/**
 * The Unown letters each FireRed/LeafGreen Tanoby chamber's twelve slots hold.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/TanobyRuins3.cs.
 */
#ifndef LEGALITY_RNG_TANOBYRUINS3_H
#define LEGALITY_RNG_TANOBYRUINS3_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        struct TanobyRuins3
        {
            static bool isFormInLocation(uint8_t location, uint8_t form) noexcept;
            static bool isFormValid(uint8_t location, uint8_t form, uint8_t slot) noexcept;
            static uint8_t getForm(uint8_t location, uint8_t slot) noexcept;
            /// Empty outside the chambers (188-194), where PKHeX throws.
            static std::span<const uint8_t> getForms(uint8_t location) noexcept;
        };
    }
}

#endif
