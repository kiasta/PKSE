/**
 * What every Pokemon GO slot shares: its availability window, how it was obtained and its IV floor.
 * PKHeX IPogoSlot and IPogoDateRange with their extension methods, ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/GO/IPogoSlot.cs and IPogoDateRange.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_POGOSLOT_H
#define LEGALITY_ENCOUNTERS_POGOSLOT_H

#include <cstdint>
#include <string>

#include "Legality/Encounters/PogoType.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncountersGO.MAX_LEVEL: the highest level a GO Pokemon can be met at.
        constexpr uint8_t POGO_MAX_LEVEL = 50;

        /// PKHeX IPogoSlot (with IPogoDateRange). Days count from GO's launch (2016-07-06 is day 1);
        /// 0 means the window is open on that side.
        class PogoSlotTemplate
        {
        public:
            virtual ~PogoSlotTemplate() = default;
            virtual uint16_t dayStart() const noexcept = 0;
            virtual uint16_t dayEnd() const noexcept = 0;
            virtual PogoType pogoType() const noexcept = 0;
            virtual uint8_t minimumIV() const noexcept = 0;
            virtual uint8_t pogoFlags() const noexcept = 0;

            bool isLocalDayStart() const noexcept { return (pogoFlags() & POGO_FLAG_LOCAL_DATE_START) != 0; }
            bool isLocalDayEnd() const noexcept { return (pogoFlags() & POGO_FLAG_LOCAL_DATE_END) != 0; }
            bool isAlwaysSpecialTrade() const noexcept { return (pogoFlags() & POGO_FLAG_ALWAYS_SPECIAL_TRADE) != 0; }

            /// PKHeX IsWithinStartEnd, for a calendar date.
            bool isWithinStartEnd(int year, int month, int day) const noexcept;
            /// PKHeX IsWithinDistributionWindow(pk): the met date falls inside the window.
            bool isWithinDistributionWindow(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetIVsAboveMinimum.
            bool isIVAboveMinimum(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetIVsValid: GO's IVs doubled plus one, above the slot's floor.
            bool areIVsValid(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX's LongName: "GO Encounter (GO) (Type): start-end".
            std::string getLongName() const;
        };

        /// PKHeX DateOnly.DayNumber: days since 0001-01-01.
        int getDayNumber(int year, int month, int day) noexcept;

        struct PogoCalendarDate
        {
            int year;
            int month;
            int day;
        };

        /// The calendar date of a GO-relative day (PKHeX PogoDateRangeExtensions.GetDate).
        PogoCalendarDate getPogoCalendarDate(uint16_t relativeDay) noexcept;
    }
}

#endif
