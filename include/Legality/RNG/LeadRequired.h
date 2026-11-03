/**
 * The party lead (its ability, and whether that ability's effect had to fail) a Gen 3/4 wild
 * encounter needed for its RNG frames to produce the Pokemon.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Frame/LeadRequired.cs. Higher values are better
 * explanations (LeadSeed::isBetterThan); the order is PKHeX's.
 */
#ifndef LEGALITY_RNG_LEADREQUIRED_H
#define LEGALITY_RNG_LEADREQUIRED_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        enum class LeadRequired : uint8_t
        {
            Invalid = 0,

            // Illuminate cannot fail to apply. A Suction Cups failure loses the encounter, and an
            // Intimidate success does.
            StaticMagnetFail,
            CuteCharmFail,
            SynchronizeFail,
            PressureHustleSpiritFail,

            IntimidateKeenEyeFail,

            Illuminate,
            SuctionCups,

            PressureHustleSpirit,
            MagnetPull,
            Static,
            CuteCharm,
            Synchronize,

            CuteCharmRadar,
            SynchronizeRadar,
            Radar,

            None = 255,
        };

        enum class EncounterTriggerCondition : uint8_t
        {
            None,
            Radar,
        };

        /// What PKHeX's report shows for a lead.
        struct LeadDisplay
        {
            uint16_t abilityId;
            bool isFail;
            EncounterTriggerCondition condition;
        };

        constexpr bool isFailTuple(LeadRequired lead) noexcept
        {
            return lead == LeadRequired::PressureHustleSpiritFail || lead == LeadRequired::SynchronizeFail || lead == LeadRequired::CuteCharmFail || lead == LeadRequired::StaticMagnetFail;
        }

        /// The lead a failed one would have been had its effect applied; None for anything else.
        constexpr LeadRequired getRegularLead(LeadRequired lead) noexcept
        {
            switch (lead)
            {
            case LeadRequired::PressureHustleSpiritFail:
                return LeadRequired::PressureHustleSpirit;
            case LeadRequired::SynchronizeFail:
                return LeadRequired::Synchronize;
            case LeadRequired::CuteCharmFail:
                return LeadRequired::CuteCharm;
            case LeadRequired::StaticMagnetFail:
                return LeadRequired::Static;
            default:
                return LeadRequired::None;
            }
        }

        /// The ability id (Gen 3+ numbering) the lead must have; 0 when none is needed.
        uint16_t getLeadAbility(LeadRequired lead) noexcept;

        LeadDisplay getLeadDisplay(LeadRequired lead) noexcept;
    }
}

#endif
