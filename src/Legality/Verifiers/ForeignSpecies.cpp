// Port of PKHeX PKHeX.Core/Legality/Legal.cs: IsForeign and its FireRed/LeafGreen bitset.
#include "Legality/Verifiers/ForeignSpecies.h"

#include <cstddef>

namespace Legality
{
    namespace
    {
        constexpr uint16_t FIRERED_LEAFGREEN_FIRST_FOREIGN_SPECIES = 151;

        // One bit per species from Mew (151) to Deoxys (386).
        // clang-format off
        constexpr uint8_t FOREIGN_FIRERED_LEAFGREEN_BITS[] = {
            0xFF, 0x33, 0x18, 0x60, 0x04, 0x63, 0x50, 0x0D,
            0x84, 0x40, 0x00, 0x04, 0xF0, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 0x07,
        };
        // clang-format on

        bool isSpeciesBitSet(const uint8_t *speciesBits, size_t speciesBitsSize, int speciesId, int firstSpeciesId)
        {
            const int bitIndex = speciesId - firstSpeciesId;
            if (bitIndex < 0)
            {
                return false;
            }
            const size_t byteIndex = static_cast<size_t>(bitIndex >> 3);
            if (byteIndex >= speciesBitsSize)
            {
                return false;
            }
            return (speciesBits[byteIndex] & (1 << (bitIndex & 7))) != 0;
        }
    }

    bool isForeignToFireRedLeafGreen(uint16_t speciesId)
    {
        return isSpeciesBitSet(FOREIGN_FIRERED_LEAFGREEN_BITS, sizeof(FOREIGN_FIRERED_LEAFGREEN_BITS), speciesId, FIRERED_LEAFGREEN_FIRST_FOREIGN_SPECIES);
    }
}
