/**
 * The Pokemon Channel Jirachi gift: before its PID and IVs, the menu shuffles three patterns and
 * Jirachi's accept animation passes or fails two checks, and the seed must be reachable through
 * both.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/ChannelJirachi.cs.
 */
#ifndef LEGALITY_RNG_CHANNELJIRACHI_H
#define LEGALITY_RNG_CHANNELJIRACHI_H

#include <cstdint>
#include <span>

#include "Legality/RNG/XDRNG.h"

namespace Legality
{
    namespace RNG
    {
        /// Which accept branch was taken, most likely first.
        enum class ChannelJirachiRandomResult : uint8_t
        {
            None = 0,
            BothFail = 1,
            FirstPass = 2,
            SecondPass = 3,
        };

        struct ChannelJirachiPossibility
        {
            ChannelJirachiRandomResult pattern = ChannelJirachiRandomResult::None;
            uint32_t seed = 0;
        };

        struct ChannelJirachiMenuPattern
        {
            uint8_t first;
            uint8_t second;
            uint8_t third;
            uint32_t endSeed;
        };

        struct ChannelJirachi
        {
            static bool isPossible(uint32_t seed) noexcept;

            /// The first accept branch that reaches `seed`, and the seed before the menu pattern.
            static ChannelJirachiPossibility getPossible(uint32_t seed) noexcept;

            static bool isValid(uint32_t &origin, uint32_t possibleBranch) noexcept;

            /// Branch 0 failed both checks, 1 passed the first, 2 failed the first and passed the
            /// second. False for a branch above 2, where PKHeX throws.
            static bool isValidAccept(uint32_t &seed, uint32_t acceptPivot) noexcept;

            /// Walks back through the menu shuffle; false when the end value repeats first.
            static bool isValidMenu(uint32_t &seed) noexcept;

            static ChannelJirachiMenuPattern getMenuPattern(uint32_t seed) noexcept;

            static uint32_t skipToPIDIV(uint32_t seed) noexcept;

            /// The state just before the IVs (after SID, PID, held item, version and OT gender).
            static constexpr uint32_t skipToIVs(uint32_t seed) noexcept { return XDRNG::next6(seed); }

            /// 6 calls for the IVs and 6 for the PID and the rest.
            static constexpr uint32_t previousToOrigin(uint32_t seed) noexcept { return XDRNG::previous12(seed); }

            static uint32_t skipMenuPattern(uint32_t seed) noexcept;

            static uint32_t skipAccept(uint32_t seed) noexcept;
        };
    }
}

#endif
