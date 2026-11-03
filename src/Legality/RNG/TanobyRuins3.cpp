#include "Legality/RNG/TanobyRuins3.h"

#include <algorithm>

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint8_t FIRST_CHAMBER = 188;
            constexpr uint8_t LAST_CHAMBER = 194;

            // Monean, Liptoo, Weepth, Dilford, Scufib, Rixy and Viapois Chambers, in location order.
            constexpr uint8_t CHAMBER_FORMS[7][12] = {
                {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27},
                {2, 2, 2, 3, 3, 3, 7, 7, 7, 20, 20, 14},
                {13, 13, 13, 13, 18, 18, 18, 18, 8, 8, 4, 4},
                {15, 15, 11, 11, 9, 9, 17, 17, 17, 16, 16, 16},
                {24, 24, 19, 19, 6, 6, 6, 5, 5, 5, 10, 10},
                {21, 21, 21, 22, 22, 22, 23, 23, 12, 12, 1, 1},
                {25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 26},
            };

            bool isLocationInRuins(uint8_t location) noexcept
            {
                return location >= FIRST_CHAMBER && location <= LAST_CHAMBER;
            }
        }

        bool TanobyRuins3::isFormInLocation(uint8_t location, uint8_t form) noexcept
        {
            if (!isLocationInRuins(location))
            {
                return false;
            }
            const std::span<const uint8_t> forms = getForms(location);
            return std::find(forms.begin(), forms.end(), form) != forms.end();
        }

        bool TanobyRuins3::isFormValid(uint8_t location, uint8_t form, uint8_t slot) noexcept
        {
            if (!isLocationInRuins(location))
            {
                return false;
            }
            const std::span<const uint8_t> forms = getForms(location);
            if (form > forms.size())
            {
                return false;
            }
            // PKHeX indexes by slot unchecked and throws past the table; that is a non-match here.
            if (slot >= forms.size())
            {
                return false;
            }
            return forms[slot] == form;
        }

        uint8_t TanobyRuins3::getForm(uint8_t location, uint8_t slot) noexcept
        {
            if (!isLocationInRuins(location))
            {
                return 0;
            }
            const std::span<const uint8_t> forms = getForms(location);
            // PKHeX checks slot > Length and would throw at Length itself; that is 0 here.
            if (slot >= forms.size())
            {
                return 0;
            }
            return forms[slot];
        }

        std::span<const uint8_t> TanobyRuins3::getForms(uint8_t location) noexcept
        {
            if (!isLocationInRuins(location))
            {
                return {};
            }
            return CHAMBER_FORMS[location - FIRST_CHAMBER];
        }
    }
}
