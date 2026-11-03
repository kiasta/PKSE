/**
 * How an evolution is triggered, numbered as the games and PKHeX number it.
 * Ported from PKHeX PKHeX.Core/Legality/Evolutions/Methods/EvolutionType.cs.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONTYPE_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONTYPE_H

#include <cstdint>

namespace Legality
{
    namespace Evolutions
    {
        enum class EvolutionType : uint8_t
        {
            None = 0,
            LevelUpFriendship = 1,
            LevelUpFriendshipMorning = 2,
            LevelUpFriendshipNight = 3,
            LevelUp = 4,
            Trade = 5,
            TradeHeldItem = 6,
            TradeShelmetKarrablast = 7,
            UseItem = 8,
            LevelUpATK = 9,
            LevelUpAttackEqualsDefense = 10,
            LevelUpDEF = 11,
            LevelUpECBelow5 = 12,
            LevelUpECAtLeast5 = 13,
            LevelUpNinjask = 14,
            LevelUpShedinja = 15,
            LevelUpBeauty = 16,
            UseItemMale = 17,
            UseItemFemale = 18,
            LevelUpHeldItemDay = 19,
            LevelUpHeldItemNight = 20,
            LevelUpKnowMove = 21,
            LevelUpWithTeammate = 22,
            LevelUpMale = 23,
            LevelUpFemale = 24,
            LevelUpElectric = 25,
            LevelUpForest = 26,
            LevelUpCold = 27,
            LevelUpInverted = 28,
            LevelUpAffection50MoveType = 29,
            LevelUpMoveType = 30,
            LevelUpWeather = 31,
            LevelUpMorning = 32,
            LevelUpNight = 33,
            LevelUpFormFemale1 = 34,
            Unused = 35,
            LevelUpVersion = 36,
            LevelUpVersionDay = 37,
            LevelUpVersionNight = 38,
            LevelUpSummit = 39,
            LevelUpDusk = 40,
            LevelUpWormhole = 41,
            UseItemWormhole = 42,
            CriticalHitsInBattle = 43,
            HitPointsLostInBattle = 44,
            Spin = 45,
            LevelUpNatureAmped = 46,
            LevelUpNatureLowKey = 47,
            TowerOfDarkness = 48,
            TowerOfWaters = 49,
            LevelUpWalkStepsWith = 50,
            LevelUpUnionCircle = 51,
            LevelUpInBattleEC100 = 52,
            LevelUpInBattleECElse = 53,
            LevelUpCollect999 = 54,
            LevelUpDefeatEquals = 55,
            LevelUpUseMoveSpecial = 56,
            LevelUpKnowMoveECElse = 57,
            LevelUpKnowMoveEC100 = 58,
            LevelUpRecoilDamageMale = 59,
            LevelUpRecoilDamageFemale = 60,
            Hisui = 61,
            UseMoveBarbBarrage = 62,
            // Legends: Arceus' own numbering clashed with the mainline values, so PKHeX moved these.
            UseItemFullMoon = 90,
            UseMoveAgileStyle = 91,
            UseMoveStrongStyle = 92,
            /// Not a game value: marks a chain stage that has not been reached (PKHeX's sentinel).
            Invalid = 255,
        };

        /// PKHeX EvolutionType.IsTrade.
        bool isTradeEvolution(EvolutionType evolutionType) noexcept;

        /// PKHeX EvolutionType.IsLevelUpRequired: the evolution happens on a level up.
        bool isLevelUpRequired(EvolutionType evolutionType) noexcept;

        /// PKHeX's enumerator name, for logs and the parity harness.
        const char *getEvolutionTypeSourceName(EvolutionType evolutionType) noexcept;
    }
}

#endif
