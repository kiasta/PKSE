/**
 * The Game Boy era rules the Gen 1 and Gen 2 encounter templates and generators share: the catch-rate
 * byte (a Gen 1 species' catch rate, or a Gen 2 held item after a Time Capsule trip), the tradeback
 * guess, and the PKM facts a Game Boy record answers differently. Ported from PKHeX
 * PKHeX.Core/Legality/Restrictions/GBRestrictions.cs and ItemConverter.IsCatchRateHeldItem.
 */
#ifndef LEGALITY_ENCOUNTERS_GBRESTRICTIONS_H
#define LEGALITY_ENCOUNTERS_GBRESTRICTIONS_H

#include <array>
#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        class Encounterable;

        /// PKHeX ParseSettings.AllowGen1Tradeback (TradebackSettings default).
        constexpr bool ALLOW_GEN1_TRADEBACK = true;

        /// PKHeX PotentialGBOrigin.
        enum class PotentialGBOrigin : uint8_t
        {
            Either,
            Gen1Only,
            Gen2Only,
        };

        /// PKHeX TimeCapsuleEvaluation.
        enum class TimeCapsuleEvaluation : uint8_t
        {
            Indeterminate,
            Transferred21,
            Transferred12,
            NotTransferred,
            BadCatchRate,
        };

        struct GBRestrictions
        {
            /// PKHeX ItemConverter.IsCatchRateHeldItem: the catch-rate byte holds a Gen 2 item.
            static bool isCatchRateHeldItem(uint8_t rate) noexcept;
            /// PKHeX EncounterUtil.GetPersonal1(version, species).CatchRate: Yellow's table for YW,
            /// else Red/Blue's.
            static uint8_t getCatchRate1(Enums::GameVersion version, uint16_t speciesId) noexcept;
            /// PKHeX RateMatchesEncounter.
            static bool rateMatchesEncounter(uint16_t speciesId, Enums::GameVersion version, uint8_t rate) noexcept;
            /// PKHeX IsSpeciesNotAvailableCatchRate.
            static bool isSpeciesNotAvailableCatchRate(uint16_t speciesId) noexcept;
            /// PKHeX CanVisitGen1.
            static bool canVisitGen1(uint16_t speciesId) noexcept;
            /// PKHeX IsTradeEvolution1.
            static bool isTradeEvolution1(uint16_t speciesId) noexcept;
            /// PKHeX GetTradebackStatusInitial.
            static PotentialGBOrigin getTradebackStatusInitial(const Pokemon::Pokemon &pokemon);
            /// PKHeX IsTimeCapsuleTransferred, for a Gen 1 format Pokemon.
            static TimeCapsuleEvaluation isTimeCapsuleTransferred(const Pokemon::Pokemon &pokemon, const std::array<LearnSource::MoveResult, 4> &moves, const Encounterable &encounter);
        };

        /// The PKM facts the Game Boy templates read. A struct, so callers qualify the names: several
        /// files keep their own file-local hasMove that an unqualified call would clash with.
        struct GameBoyEntity
        {
            /// PKHeX PK1.CatchRate; 0 for any other format.
            static uint8_t getCatchRate(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX ICaughtData2.CaughtData; 0 for a format without the field (only PK2 has it).
            static uint16_t getCaughtData(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX `pk is ICaughtData2`.
            static bool hasCaughtDataField(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX ICaughtData2.MetTimeOfDay.
            static uint8_t getMetTimeOfDay(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX PKM.Japanese and PKM.Korean.
            static bool isJapanese(const Pokemon::Pokemon &pokemon) noexcept;
            static bool isKorean(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX PKM.VC1 and PKM.VC2: a Gen 7+ record from a Virtual Console Gen 1 or Gen 2 game.
            static bool isVirtualConsole1(const Pokemon::Pokemon &pokemon) noexcept;
            static bool isVirtualConsole2(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX `pk.OriginalTrainerTrash is [TradeOTCode, Terminator, ..]` for a Gen 1/2 record.
            static bool isTradeTrainerMarker(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX PKM.IsShiny: the DV pattern for a Gen 1/2 record, the PID for any later one.
            static bool isShiny(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX PKM.HasMove.
            static bool hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId) noexcept;
        };
    }
}

#endif
