/**
 * How a Pokemon GO Pokemon was obtained, which balls that allows, and the format HOME imported it
 * as. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/GO/PogoType.cs, the PogoFlags enum
 * in EncounterSlot7GO.cs and the PogoImportFormat enum in EncounterSlot8GO.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_POGOTYPE_H
#define LEGALITY_ENCOUNTERS_POGOTYPE_H

#include <cstdint>

#include "Enums/Ball.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX PogoType, with its values (the encounter data stores them).
        enum class PogoType : uint8_t
        {
            None = 0,
            Wild,
            Egg,
            Egg12km,
            Raid = 10,
            RaidMythical,
            RaidUltraBeast,
            RaidShadow,
            RaidShadowMythical,
            RaidShadowUltraBeast,
            FieldResearch = 20,
            ResearchBreakthrough,
            SpecialResearch,
            TimedResearch,
            CollectionChallenge,
            VivillonCollector,
            PartyPlay,
            StampRally,
            GOPass,
            ReferralBonus,
            GBL = 40,
            GBLMythical,
            GBLEvent,
            Shadow = 50,
            ShadowMythical,
            ShadowUltraBeast,
            MaxBattle = 60,
            MaxBattleMythical,
            MaxBattleUltraBeast,
            MaxBattleGigantamax,
            PremierBallBug = 254,
            PremierBallBugMythical = 255,
        };

        /// PKHeX PogoBallRestriction.
        enum class PogoBallRestriction : uint8_t
        {
            None = 0,
            StandardMaster = 1,
            Standard = 2,
            OnlyPoke = 3,
            OnlyPremier = 4,
            OnlyBeast = 5,
            OnlySafari = 6,
        };

        /// PKHeX PogoImportFormat: the format HOME creates the Pokemon in.
        enum class PogoImportFormat : uint8_t
        {
            PK7 = 0,
            PB7 = 1,
            PK8 = 2,
            PA8 = 3,
            PK9 = 4,
        };

        /// PKHeX PogoFlags bits.
        constexpr uint8_t POGO_FLAG_LOCAL_DATE_START = 1 << 0;
        constexpr uint8_t POGO_FLAG_LOCAL_DATE_END = 1 << 1;
        constexpr uint8_t POGO_FLAG_FEATURED_WILD_AREA = 1 << 2;
        constexpr uint8_t POGO_FLAG_ALWAYS_SPECIAL_TRADE = 1 << 4;

        /// PKHeX PogoType.IsRaid, IsShadow, IsMaxBattle.
        bool isPogoRaid(PogoType type) noexcept;
        bool isPogoShadow(PogoType type) noexcept;
        bool isPogoMaxBattle(PogoType type) noexcept;
        /// PKHeX's enumerator name, which the template's LongName prints.
        const char *getPogoTypeName(PogoType type) noexcept;

        /// PKHeX PogoBallRestriction.GetFixedBall; Ball::None when the restriction fixes none.
        Enums::Ball getPogoFixedBall(PogoBallRestriction restriction) noexcept;
        /// PKHeX PogoBallRestriction.IsValidBall.
        bool isPogoBallValid(PogoBallRestriction restriction, Enums::Ball ball, PogoType type, uint8_t flags) noexcept;
    }
}

#endif
