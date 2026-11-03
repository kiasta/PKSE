/**
 * Rules the Legends: Z-A encounter templates share: the gender ratio a fixed gender stands for, the
 * Alpha move flag an Alpha must carry, and the seed check every template runs. Ported from the
 * GetParams / IsMatchDeferred / TryGetSeed bodies of PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterSlot9a.cs and its siblings, and
 * PKHeX.Core/Legality/RNG/SeedCorrelationResult.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES9A_H

#include <cstdint>

#include "Legality/RNG/LumioseRNG.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncounterArea9a.LocationHyperspace: Hyperspace Lumiose.
        constexpr uint16_t LOCATION_HYPERSPACE9A = 273;

        /// PKHeX SeedCorrelationResult.
        enum class SeedCorrelationResult : uint8_t
        {
            Success,
            Invalid,
            /// The seed search was skipped; another template may still match better.
            Ignore,
        };

        /// The GetParams gender switch: 0 male, 1 female, 2 genderless, anything else the species' ratio.
        uint8_t getGenderRatio9a(uint8_t fixedGender, uint16_t speciesId, uint8_t formId) noexcept;
        /// PKHeX `IsAlpha && pk is IPlusRecord && !GetMovePlusFlag(IndexOf(AlphaMove))`, for an Alpha
        /// template: the record's Plus flag for the species' Alpha move is not set. False for a format
        /// without Plus flags.
        bool isAlphaMoveFlagMissing9a(const Pokemon::Pokemon &pokemon, uint16_t speciesId, uint8_t formId) noexcept;
        /// PKHeX `GetParams(...).TryGetSeed(pk, out _)`.
        bool isSeedFound9a(const RNG::GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon) noexcept;
    }
}

#endif
