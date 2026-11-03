/**
 * Scarlet/Violet Tera Raids: the Tera type a raid seed rolls, and whether a 32-bit raid seed's star
 * and opponent rolls select a given raid.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen9/Tera9RNG.cs. The opponent-rate totals
 * PKHeX reads from EncounterTera9.GetRateTotalSL/VL are passed in.
 */
#ifndef LEGALITY_RNG_TERA9RNG_H
#define LEGALITY_RNG_TERA9RNG_H

#include <cstdint>

#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct Tera9RNG
        {
            /// False for a gem type that is neither fixed, Default nor Random, where PKHeX throws.
            static bool isMatchTeraType(uint32_t seed, GemType gem, uint16_t species, uint8_t form, uint8_t original) noexcept;
            static bool isMatchTeraType(GemType gem, uint16_t species, uint8_t form, uint8_t original) noexcept;
            static uint8_t getTeraType(uint64_t seed, GemType gem, uint16_t species, uint8_t form) noexcept;

            static bool isMatchTeraTypePersonalEgg(uint16_t species, uint8_t form, uint8_t original) noexcept;
            static bool isMatchTeraTypePersonal(uint16_t species, uint8_t form, uint8_t original) noexcept;
            static bool isMatchTeraTypePersonalAnyForm(uint16_t species, uint8_t original) noexcept;
            static bool isMatchTeraTypePersonalImport(uint16_t species, uint8_t form, uint8_t original) noexcept;
            static bool isMatchTeraTypePersonalAnyFormImport(uint16_t species, uint8_t original) noexcept;
            static uint8_t getTeraTypeFromPersonal(uint16_t species, uint8_t form, uint64_t pivot) noexcept;

            static bool isMatchStarChoice(uint32_t seed, uint8_t stars, uint8_t raidRate, int16_t randomRateMinimumScarlet, int16_t randomRateMinimumViolet, uint16_t rateTotalScarlet, uint16_t rateTotalViolet) noexcept;

            static uint32_t getOriginalSeed(const Pokemon::Pokemon &pokemon) noexcept;
        };
    }
}

#endif
