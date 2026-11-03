// Port of PKHeX PKHeX.Core/Legality/Verifiers/MarkingVerifier.cs: box markings and the favorite flag.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t SINGLE_MARKINGS_GEN3_MASK = 0b1111;
        constexpr uint16_t SINGLE_MARKINGS_MASK = 0b111111;
        constexpr uint16_t COLORED_MARKINGS_MASK = 0b1111'1111'1111;
        constexpr uint8_t MARKING_COLOR_PINK = 2;

        // PKHeX EvolutionHistory.HasVisitedLGPE: only a Let's Go record, or one that came from Let's Go,
        // has passed through Let's Go.
        bool hasVisitedLetsGo(const AnalysisContext &context)
        {
            if (context.formatGroup == Enums::GameVersion::GG)
            {
                return true;
            }
            const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(context.pokemon.originGame());
            return originVersion == Enums::GameVersion::GP || originVersion == Enums::GameVersion::GE;
        }
    }

    void verifyMarkings(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        // Only Let's Go sets the favorite flag; HOME and later games keep it.
        if (pokemon.hasFavoriteFlag() && pokemon.isFavorite() && !hasVisitedLetsGo(context))
        {
            context.add(Severity::Invalid, CheckIdentifier::Marking, ResultCode::FavoriteMarkingUnavailable);
        }

        const uint8_t markingCount = pokemon.markingCount();
        const uint16_t markingValue = pokemon.markingValue();
        if (markingCount == 0 || markingValue == 0)
        {
            return;
        }
        if (!pokemon.hasColoredMarkings())
        {
            const uint16_t markingMask = markingCount == 4 ? SINGLE_MARKINGS_GEN3_MASK : SINGLE_MARKINGS_MASK;
            if (markingValue > markingMask)
            {
                context.add(Severity::Invalid, CheckIdentifier::Marking, ResultCode::MarkValueUnusedBitsPresent);
            }
            return;
        }
        if (markingValue > COLORED_MARKINGS_MASK)
        {
            context.add(Severity::Invalid, CheckIdentifier::Marking, ResultCode::MarkValueUnusedBitsPresent);
        }
        for (int markingIndex = 0; markingIndex < markingCount; markingIndex++)
        {
            const uint8_t markingColor = (markingValue >> (markingIndex * 2)) & 0b11;
            if (markingColor > MARKING_COLOR_PINK)
            {
                context.add(Severity::Invalid, CheckIdentifier::Marking, ResultCode::MarkValueOutOfRange, std::to_string(markingIndex));
            }
        }
    }
}
