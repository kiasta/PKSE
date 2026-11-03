// Port of PKHeX PKHeX.Core/Legality/Verifiers/ConsoleRegionVerifier.cs: the 3DS console region and country.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        // PKHeX Locale3DS.IsConsoleRegionCountryValid. Region 3 was reserved for Australia and merged
        // into Europe, so no country belongs to it.
        bool isConsoleRegionCountryValid(uint8_t consoleRegion, uint8_t consoleCountry)
        {
            switch (consoleRegion)
            {
            case 0:
                return consoleCountry == 1;
            case 1:
                return (consoleCountry >= 8 && consoleCountry <= 52) || consoleCountry == 153 || consoleCountry == 156 || consoleCountry == 168 || consoleCountry == 174 || consoleCountry == 186;
            case 2:
                return (consoleCountry >= 64 && consoleCountry <= 127) || consoleCountry == 169 || consoleCountry == 184 || consoleCountry == 185;
            case 4:
                return consoleCountry == 144 || consoleCountry == 160;
            case 5:
                return consoleCountry == 136;
            case 6:
                return consoleCountry == 144 || consoleCountry == 128;
            default:
                return false;
            }
        }
    }

    void verifyConsoleRegion(const AnalysisContext &context)
    {
        constexpr uint8_t consoleRegionCount = 7;
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasConsoleRegion())
        {
            return;
        }
        const uint8_t consoleRegion = pokemon.consoleRegion();
        if (consoleRegion >= consoleRegionCount)
        {
            context.add(Severity::Invalid, CheckIdentifier::Geography, ResultCode::GeographyHardwareRange);
            return;
        }
        if (!isConsoleRegionCountryValid(consoleRegion, pokemon.consoleCountry()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Geography, ResultCode::GeographyHardwareInvalid);
        }
    }
}
