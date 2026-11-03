#include "Legality/Encounters/PogoType.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX TransferSafariBall: GO's Safari Ball arrives in HOME as a Strange Ball.
            constexpr Enums::Ball TRANSFER_SAFARI_BALL = Enums::Ball::Strange;

            bool isMasterBallUsable(PogoBallRestriction restriction) noexcept
            {
                return restriction != PogoBallRestriction::Standard && restriction != PogoBallRestriction::OnlyPoke && restriction != PogoBallRestriction::OnlyBeast;
            }

            bool isSafariBallUsable(PogoBallRestriction restriction, PogoType type, uint8_t flags) noexcept
            {
                if (restriction == PogoBallRestriction::OnlySafari)
                {
                    return true;
                }
                if (isPogoRaid(type) || isPogoMaxBattle(type))
                {
                    return (flags & POGO_FLAG_FEATURED_WILD_AREA) != 0;
                }
                switch (type)
                {
                case PogoType::Egg:
                case PogoType::Egg12km:
                case PogoType::ResearchBreakthrough:
                case PogoType::VivillonCollector:
                case PogoType::ReferralBonus:
                case PogoType::GBL:
                case PogoType::GBLMythical:
                case PogoType::GBLEvent:
                case PogoType::Shadow:
                case PogoType::ShadowMythical:
                case PogoType::ShadowUltraBeast:
                    return false;
                default:
                    return true;
                }
            }

            bool isStandardBall(Enums::Ball ball) noexcept
            {
                return ball == Enums::Ball::Poke || ball == Enums::Ball::Great || ball == Enums::Ball::Ultra;
            }
        }

        bool isPogoRaid(PogoType type) noexcept
        {
            return type == PogoType::Raid || type == PogoType::RaidMythical || type == PogoType::RaidUltraBeast || type == PogoType::RaidShadow || type == PogoType::RaidShadowMythical || type == PogoType::RaidShadowUltraBeast;
        }

        bool isPogoShadow(PogoType type) noexcept
        {
            return type == PogoType::Shadow || type == PogoType::ShadowMythical || type == PogoType::ShadowUltraBeast || type == PogoType::RaidShadow || type == PogoType::RaidShadowMythical || type == PogoType::RaidShadowUltraBeast;
        }

        bool isPogoMaxBattle(PogoType type) noexcept
        {
            return type == PogoType::MaxBattle || type == PogoType::MaxBattleMythical || type == PogoType::MaxBattleUltraBeast || type == PogoType::MaxBattleGigantamax;
        }

        const char *getPogoTypeName(PogoType type) noexcept
        {
            switch (type)
            {
            case PogoType::Wild:
                return "Wild";
            case PogoType::Egg:
                return "Egg";
            case PogoType::Egg12km:
                return "Egg12km";
            case PogoType::Raid:
                return "Raid";
            case PogoType::RaidMythical:
                return "RaidMythical";
            case PogoType::RaidUltraBeast:
                return "RaidUltraBeast";
            case PogoType::RaidShadow:
                return "RaidShadow";
            case PogoType::RaidShadowMythical:
                return "RaidShadowMythical";
            case PogoType::RaidShadowUltraBeast:
                return "RaidShadowUltraBeast";
            case PogoType::FieldResearch:
                return "FieldResearch";
            case PogoType::ResearchBreakthrough:
                return "ResearchBreakthrough";
            case PogoType::SpecialResearch:
                return "SpecialResearch";
            case PogoType::TimedResearch:
                return "TimedResearch";
            case PogoType::CollectionChallenge:
                return "CollectionChallenge";
            case PogoType::VivillonCollector:
                return "VivillonCollector";
            case PogoType::PartyPlay:
                return "PartyPlay";
            case PogoType::StampRally:
                return "StampRally";
            case PogoType::GOPass:
                return "GOPass";
            case PogoType::ReferralBonus:
                return "ReferralBonus";
            case PogoType::GBL:
                return "GBL";
            case PogoType::GBLMythical:
                return "GBLMythical";
            case PogoType::GBLEvent:
                return "GBLEvent";
            case PogoType::Shadow:
                return "Shadow";
            case PogoType::ShadowMythical:
                return "ShadowMythical";
            case PogoType::ShadowUltraBeast:
                return "ShadowUltraBeast";
            case PogoType::MaxBattle:
                return "MaxBattle";
            case PogoType::MaxBattleMythical:
                return "MaxBattleMythical";
            case PogoType::MaxBattleUltraBeast:
                return "MaxBattleUltraBeast";
            case PogoType::MaxBattleGigantamax:
                return "MaxBattleGigantamax";
            case PogoType::PremierBallBug:
                return "PremierBallBug";
            case PogoType::PremierBallBugMythical:
                return "PremierBallBugMythical";
            default:
                return "None";
            }
        }

        Enums::Ball getPogoFixedBall(PogoBallRestriction restriction) noexcept
        {
            switch (restriction)
            {
            case PogoBallRestriction::OnlyPoke:
                return Enums::Ball::Poke;
            case PogoBallRestriction::OnlyPremier:
                return Enums::Ball::Premier;
            case PogoBallRestriction::OnlyBeast:
                return Enums::Ball::Beast;
            case PogoBallRestriction::OnlySafari:
                return TRANSFER_SAFARI_BALL;
            default:
                return Enums::Ball::None;
            }
        }

        bool isPogoBallValid(PogoBallRestriction restriction, Enums::Ball ball, PogoType type, uint8_t flags) noexcept
        {
            if (ball == Enums::Ball::Master)
            {
                return isMasterBallUsable(restriction);
            }
            if (ball == TRANSFER_SAFARI_BALL)
            {
                return isSafariBallUsable(restriction, type, flags);
            }
            switch (restriction)
            {
            case PogoBallRestriction::None:
                return true;
            case PogoBallRestriction::StandardMaster:
            case PogoBallRestriction::Standard:
                return isStandardBall(ball);
            case PogoBallRestriction::OnlyPoke:
                return ball == Enums::Ball::Poke;
            case PogoBallRestriction::OnlyPremier:
                return ball == Enums::Ball::Premier;
            case PogoBallRestriction::OnlyBeast:
                return ball == Enums::Ball::Beast;
            default:
                return false;
            }
        }
    }
}
