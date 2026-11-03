#include "Legality/RNG/LeadRequired.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint16_t STATIC_ABILITY = 9;
            constexpr uint16_t SUCTION_CUPS_ABILITY = 21;
            constexpr uint16_t INTIMIDATE_ABILITY = 22;
            constexpr uint16_t SYNCHRONIZE_ABILITY = 28;
            constexpr uint16_t ILLUMINATE_ABILITY = 35;
            constexpr uint16_t MAGNET_PULL_ABILITY = 42;
            constexpr uint16_t PRESSURE_ABILITY = 46;
            constexpr uint16_t CUTE_CHARM_ABILITY = 56;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Frame/LeadRequired.cs GetAbility
        uint16_t getLeadAbility(LeadRequired lead) noexcept
        {
            switch (lead)
            {
            case LeadRequired::Synchronize:
            case LeadRequired::SynchronizeRadar:
                return SYNCHRONIZE_ABILITY;
            case LeadRequired::CuteCharm:
            case LeadRequired::CuteCharmRadar:
                return CUTE_CHARM_ABILITY;
            case LeadRequired::Static:
                return STATIC_ABILITY;
            case LeadRequired::MagnetPull:
                return MAGNET_PULL_ABILITY;
            case LeadRequired::PressureHustleSpirit:
                return PRESSURE_ABILITY;
            case LeadRequired::SuctionCups:
                return SUCTION_CUPS_ABILITY;
            case LeadRequired::Illuminate:
                return ILLUMINATE_ABILITY;
            case LeadRequired::IntimidateKeenEyeFail:
                return INTIMIDATE_ABILITY;
            default:
                return 0;
            }
        }

        // PKHeX PKHeX.Core/Legality/RNG/Frame/LeadRequired.cs GetDisplayAbility
        LeadDisplay getLeadDisplay(LeadRequired lead) noexcept
        {
            bool isFail = false;
            if (isFailTuple(lead))
            {
                isFail = true;
                lead = getRegularLead(lead);
            }
            else if (lead == LeadRequired::IntimidateKeenEyeFail)
            {
                isFail = true;
            }

            const bool isRadar = lead == LeadRequired::Radar || lead == LeadRequired::SynchronizeRadar || lead == LeadRequired::CuteCharmRadar;
            return LeadDisplay{getLeadAbility(lead), isFail, isRadar ? EncounterTriggerCondition::Radar : EncounterTriggerCondition::None};
        }
    }
}
