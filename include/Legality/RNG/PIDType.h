/**
 * How a PID (and usually the IVs) was generated: the correlation MethodFinder traced it to.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/PIDType.cs. The order and values are PKHeX's, so the
 * range checks below (isRestricted) mean what they mean there.
 */
#ifndef LEGALITY_RNG_PIDTYPE_H
#define LEGALITY_RNG_PIDTYPE_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        enum class PIDType : uint8_t
        {
            /// No relationship between the PID and the IVs.
            None,

            // LCRNG: PID halves then IVs, Method 2/3/4 with one unused call at different points.
            Method1,
            Method2,
            Method3,
            Method4,
            /// FireRed/LeafGreen Unown: the PID halves are swapped.
            Method1Unown,
            Method2Unown,
            Method3Unown,
            Method4Unown,
            /// Ruby/Sapphire roamer: only the low 8 bits of the IVs are kept.
            Method1Roamer,

            // Gen 3 events, PID generated in reversed half order ("BACD").
            BACD,
            /// Anti-shiny by adding to the PID.
            BACDAntishiny,
            /// Anti-shiny by xoring the trainer ids into the high half.
            BACDAntishinyXor,
            BACDShiny,
            // Egg-only tags: the shiny state was decided against a different (hatching) OT.
            BACDEggAntishiny,
            BACDEggAntishinyXor,
            BACDEggShiny,
            /// No origin seed restriction.
            BACDUnrestricted,
            BACDUnrestrictedAntishinyXor,
            /// Origin seed restricted to 16 bits.
            BACDRestricted,
            BACDRestrictedAntishiny,
            /// Origin seed in [0, 213]: the digit sum of a binary coded decimal hh:mm:ss timestamp.
            BACDRestrictedBinaryCodedDecimal,
            /// 16-bit origin seed, two calls spent picking the gift from a table, anti-shiny.
            BACDTableAntishiny,
            BACDTableShiny,
            /// Origin seed from the Mystry Mew table only.
            BACDMystryMew,

            /// Gen 4 Cute Charm: an 8-bit PID chosen from the gender and gender ratio.
            CuteCharm,
            /// Gen 4 Poke Radar chain shiny.
            ChainShiny,

            // XDRNG.
            ColosseumXD,
            /// Re-rolled because it would have been shiny.
            ColosseumXDAntishiny,
            /// Made right after the TID and SID calls; the second starter follows the first.
            ColosseumStarter,
            /// Pokemon Channel Jirachi.
            Channel,
            PokeSpot,

            // ARNG.
            /// Gen 4 Mystery Gift PID re-rolled so it is not shiny for the receiver.
            Gen4MysteryGiftAntishiny,

            // Formulaic.
            Gen5MysteryGiftShiny,
            /// From TID, SID and gender alone, unrelated to the IVs.
            Pokewalker,
            /// Gen 8 PID and EC from a 64-bit seed.
            Xoroshiro,
            Overworld8,
            Roaming8b,
            Tera9,

            // MRNG.
            /// Gen 4 My Pokemon Ranch gifts.
            Ranch,
        };

        /// PKHeX PIDTypeExtensions.IsClassicMethod: Methods 1-4, the Unown variants included.
        constexpr bool isClassicMethod(PIDType type) noexcept
        {
            return type >= PIDType::Method1 && type <= PIDType::Method4Unown;
        }

        /// PKHeX PIDTypeExtensions.IsRestricted: an event PID whose origin seed is restricted.
        constexpr bool isRestricted(PIDType type) noexcept
        {
            return type >= PIDType::BACDRestricted && type <= PIDType::BACDMystryMew;
        }

        /// PKHeX PIDTypeExtensions.IsUnown.
        constexpr bool isUnown(PIDType type) noexcept
        {
            return type >= PIDType::Method1Unown && type <= PIDType::Method4Unown;
        }

        /// PKHeX's own spelling of the type (Method_1, BACD_R_A, CXDAnti ...), for comparing
        /// against PKHeX output.
        const char *getPIDTypeSourceName(PIDType type) noexcept;
    }
}

#endif
