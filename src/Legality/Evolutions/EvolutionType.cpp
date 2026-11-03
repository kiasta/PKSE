#include "Legality/Evolutions/EvolutionType.h"

namespace Legality
{
    namespace Evolutions
    {
        // PKHeX PKHeX.Core/Legality/Evolutions/Methods/EvolutionType.cs
        bool isTradeEvolution(EvolutionType evolutionType) noexcept
        {
            return evolutionType == EvolutionType::Trade || evolutionType == EvolutionType::TradeHeldItem || evolutionType == EvolutionType::TradeShelmetKarrablast;
        }

        bool isLevelUpRequired(EvolutionType evolutionType) noexcept
        {
            switch (evolutionType)
            {
            case EvolutionType::LevelUpFriendship:
            case EvolutionType::LevelUpFriendshipMorning:
            case EvolutionType::LevelUpFriendshipNight:
            case EvolutionType::LevelUp:
            case EvolutionType::LevelUpATK:
            case EvolutionType::LevelUpAttackEqualsDefense:
            case EvolutionType::LevelUpDEF:
            case EvolutionType::LevelUpECBelow5:
            case EvolutionType::LevelUpECAtLeast5:
            case EvolutionType::LevelUpNinjask:
            case EvolutionType::LevelUpShedinja:
            case EvolutionType::LevelUpBeauty:
            case EvolutionType::LevelUpHeldItemDay:
            case EvolutionType::LevelUpHeldItemNight:
            case EvolutionType::LevelUpKnowMove:
            case EvolutionType::LevelUpWithTeammate:
            case EvolutionType::LevelUpMale:
            case EvolutionType::LevelUpFemale:
            case EvolutionType::LevelUpElectric:
            case EvolutionType::LevelUpForest:
            case EvolutionType::LevelUpCold:
            case EvolutionType::LevelUpInverted:
            case EvolutionType::LevelUpAffection50MoveType:
            case EvolutionType::LevelUpMoveType:
            case EvolutionType::LevelUpWeather:
            case EvolutionType::LevelUpMorning:
            case EvolutionType::LevelUpNight:
            case EvolutionType::LevelUpFormFemale1:
            case EvolutionType::LevelUpVersion:
            case EvolutionType::LevelUpVersionDay:
            case EvolutionType::LevelUpVersionNight:
            case EvolutionType::LevelUpSummit:
            case EvolutionType::LevelUpDusk:
            case EvolutionType::LevelUpWormhole:
            case EvolutionType::LevelUpNatureAmped:
            case EvolutionType::LevelUpNatureLowKey:
            case EvolutionType::LevelUpWalkStepsWith:
            case EvolutionType::LevelUpUnionCircle:
            case EvolutionType::LevelUpInBattleEC100:
            case EvolutionType::LevelUpInBattleECElse:
            case EvolutionType::LevelUpCollect999:
            case EvolutionType::LevelUpDefeatEquals:
            case EvolutionType::LevelUpUseMoveSpecial:
            case EvolutionType::LevelUpKnowMoveECElse:
            case EvolutionType::LevelUpKnowMoveEC100:
            case EvolutionType::LevelUpRecoilDamageMale:
            case EvolutionType::LevelUpRecoilDamageFemale:
                return true;
            default:
                return false;
            }
        }

        const char *getEvolutionTypeSourceName(EvolutionType evolutionType) noexcept
        {
            switch (evolutionType)
            {
            case EvolutionType::None:
                return "None";
            case EvolutionType::LevelUpFriendship:
                return "LevelUpFriendship";
            case EvolutionType::LevelUpFriendshipMorning:
                return "LevelUpFriendshipMorning";
            case EvolutionType::LevelUpFriendshipNight:
                return "LevelUpFriendshipNight";
            case EvolutionType::LevelUp:
                return "LevelUp";
            case EvolutionType::Trade:
                return "Trade";
            case EvolutionType::TradeHeldItem:
                return "TradeHeldItem";
            case EvolutionType::TradeShelmetKarrablast:
                return "TradeShelmetKarrablast";
            case EvolutionType::UseItem:
                return "UseItem";
            case EvolutionType::LevelUpATK:
                return "LevelUpATK";
            case EvolutionType::LevelUpAttackEqualsDefense:
                return "LevelUpAeqD";
            case EvolutionType::LevelUpDEF:
                return "LevelUpDEF";
            case EvolutionType::LevelUpECBelow5:
                return "LevelUpECl5";
            case EvolutionType::LevelUpECAtLeast5:
                return "LevelUpECgeq5";
            case EvolutionType::LevelUpNinjask:
                return "LevelUpNinjask";
            case EvolutionType::LevelUpShedinja:
                return "LevelUpShedinja";
            case EvolutionType::LevelUpBeauty:
                return "LevelUpBeauty";
            case EvolutionType::UseItemMale:
                return "UseItemMale";
            case EvolutionType::UseItemFemale:
                return "UseItemFemale";
            case EvolutionType::LevelUpHeldItemDay:
                return "LevelUpHeldItemDay";
            case EvolutionType::LevelUpHeldItemNight:
                return "LevelUpHeldItemNight";
            case EvolutionType::LevelUpKnowMove:
                return "LevelUpKnowMove";
            case EvolutionType::LevelUpWithTeammate:
                return "LevelUpWithTeammate";
            case EvolutionType::LevelUpMale:
                return "LevelUpMale";
            case EvolutionType::LevelUpFemale:
                return "LevelUpFemale";
            case EvolutionType::LevelUpElectric:
                return "LevelUpElectric";
            case EvolutionType::LevelUpForest:
                return "LevelUpForest";
            case EvolutionType::LevelUpCold:
                return "LevelUpCold";
            case EvolutionType::LevelUpInverted:
                return "LevelUpInverted";
            case EvolutionType::LevelUpAffection50MoveType:
                return "LevelUpAffection50MoveType";
            case EvolutionType::LevelUpMoveType:
                return "LevelUpMoveType";
            case EvolutionType::LevelUpWeather:
                return "LevelUpWeather";
            case EvolutionType::LevelUpMorning:
                return "LevelUpMorning";
            case EvolutionType::LevelUpNight:
                return "LevelUpNight";
            case EvolutionType::LevelUpFormFemale1:
                return "LevelUpFormFemale1";
            case EvolutionType::Unused:
                return "UNUSED";
            case EvolutionType::LevelUpVersion:
                return "LevelUpVersion";
            case EvolutionType::LevelUpVersionDay:
                return "LevelUpVersionDay";
            case EvolutionType::LevelUpVersionNight:
                return "LevelUpVersionNight";
            case EvolutionType::LevelUpSummit:
                return "LevelUpSummit";
            case EvolutionType::LevelUpDusk:
                return "LevelUpDusk";
            case EvolutionType::LevelUpWormhole:
                return "LevelUpWormhole";
            case EvolutionType::UseItemWormhole:
                return "UseItemWormhole";
            case EvolutionType::CriticalHitsInBattle:
                return "CriticalHitsInBattle";
            case EvolutionType::HitPointsLostInBattle:
                return "HitPointsLostInBattle";
            case EvolutionType::Spin:
                return "Spin";
            case EvolutionType::LevelUpNatureAmped:
                return "LevelUpNatureAmped";
            case EvolutionType::LevelUpNatureLowKey:
                return "LevelUpNatureLowKey";
            case EvolutionType::TowerOfDarkness:
                return "TowerOfDarkness";
            case EvolutionType::TowerOfWaters:
                return "TowerOfWaters";
            case EvolutionType::LevelUpWalkStepsWith:
                return "LevelUpWalkStepsWith";
            case EvolutionType::LevelUpUnionCircle:
                return "LevelUpUnionCircle";
            case EvolutionType::LevelUpInBattleEC100:
                return "LevelUpInBattleEC100";
            case EvolutionType::LevelUpInBattleECElse:
                return "LevelUpInBattleECElse";
            case EvolutionType::LevelUpCollect999:
                return "LevelUpCollect999";
            case EvolutionType::LevelUpDefeatEquals:
                return "LevelUpDefeatEquals";
            case EvolutionType::LevelUpUseMoveSpecial:
                return "LevelUpUseMoveSpecial";
            case EvolutionType::LevelUpKnowMoveECElse:
                return "LevelUpKnowMoveECElse";
            case EvolutionType::LevelUpKnowMoveEC100:
                return "LevelUpKnowMoveEC100";
            case EvolutionType::LevelUpRecoilDamageMale:
                return "LevelUpRecoilDamageMale";
            case EvolutionType::LevelUpRecoilDamageFemale:
                return "LevelUpRecoilDamageFemale";
            case EvolutionType::Hisui:
                return "Hisui";
            case EvolutionType::UseMoveBarbBarrage:
                return "UseMoveBarbBarrage";
            case EvolutionType::UseItemFullMoon:
                return "UseItemFullMoon";
            case EvolutionType::UseMoveAgileStyle:
                return "UseMoveAgileStyle";
            case EvolutionType::UseMoveStrongStyle:
                return "UseMoveStrongStyle";
            case EvolutionType::Invalid:
                return "Invalid";
            }
            return "Unknown";
        }
    }
}
