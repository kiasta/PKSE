#include "Legality/Encounters/PogoSlot.h"

#include <cstdio>
#include <ctime>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/SpeciesCategory.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX IPogoDateRange.FirstDay: the day before GO launched (2016-07-06), for time zones.
            constexpr int FIRST_DAY_NUMBER = 736150 - 1;
            /// DateOnly.DayNumber of 1970-01-01, the epoch days_from_civil counts from.
            constexpr int UNIX_EPOCH_DAY_NUMBER = 719162;
            constexpr int SECONDS_PER_DAY = 86400;
            /// PKHeX GetMaxDate: an open-ended window runs to today in UTC+12 (Kiribati).
            constexpr int LATEST_TIME_ZONE_SECONDS = 12 * 3600;
            constexpr int MET_YEAR_EPOCH = 2000;

            /// Howard Hinnant's civil_from_days, from days since 1970-01-01.
            PogoCalendarDate getCalendarDate(int dayNumber) noexcept
            {
                const int daysSinceEpoch = dayNumber - UNIX_EPOCH_DAY_NUMBER + 719468;
                const int era = (daysSinceEpoch >= 0 ? daysSinceEpoch : daysSinceEpoch - 146096) / 146097;
                const int dayOfEra = daysSinceEpoch - era * 146097;
                const int yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
                const int dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
                const int monthIndex = (5 * dayOfYear + 2) / 153;
                const int day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
                const int month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
                return PogoCalendarDate{yearOfEra + era * 400 + (month <= 2 ? 1 : 0), month, day};
            }

            int getMaximumDayNumber() noexcept
            {
                const long long latestSeconds = static_cast<long long>(std::time(nullptr)) + LATEST_TIME_ZONE_SECONDS;
                return static_cast<int>(latestSeconds / SECONDS_PER_DAY) + UNIX_EPOCH_DAY_NUMBER;
            }

            /// PKHeX PogoDateRangeExtensions.GetDateString.
            std::string getDateString(uint16_t day, int shift, bool always)
            {
                if (day == 0 && !always)
                {
                    return "X";
                }
                const PogoCalendarDate date = getCalendarDate(FIRST_DAY_NUMBER + static_cast<uint16_t>(day + shift));
                char dateText[16];
                std::snprintf(dateText, sizeof(dateText), "%04d.%02d.%02d", date.year, date.month, date.day);
                return dateText;
            }
        }

        PogoCalendarDate getPogoCalendarDate(uint16_t relativeDay) noexcept
        {
            return getCalendarDate(FIRST_DAY_NUMBER + relativeDay);
        }

        // Howard Hinnant's days_from_civil, offset to DateOnly.DayNumber.
        int getDayNumber(int year, int month, int day) noexcept
        {
            const int shiftedYear = month <= 2 ? year - 1 : year;
            const int era = (shiftedYear >= 0 ? shiftedYear : shiftedYear - 399) / 400;
            const int yearOfEra = shiftedYear - era * 400;
            const int dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
            const int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
            return era * 146097 + dayOfEra - 719468 + UNIX_EPOCH_DAY_NUMBER;
        }

        // PKHeX PogoDateRangeExtensions.IsWithinStartEnd
        bool PogoSlotTemplate::isWithinStartEnd(int year, int month, int day) const noexcept
        {
            const int dayNumber = getDayNumber(year, month, day);
            const uint16_t relativeDay = static_cast<uint16_t>(dayNumber - FIRST_DAY_NUMBER);
            if (dayEnd() == 0)
            {
                return dayStart() <= relativeDay && dayNumber <= getMaximumDayNumber();
            }
            if (dayStart() == 0)
            {
                return relativeDay <= dayEnd();
            }
            return dayStart() <= relativeDay && relativeDay <= dayEnd();
        }

        bool PogoSlotTemplate::isWithinDistributionWindow(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return isWithinStartEnd(MET_YEAR_EPOCH + pokemon.metYear(), pokemon.metMonth(), pokemon.metDay());
        }

        bool PogoSlotTemplate::isIVAboveMinimum(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const int minimum = minimumIV();
            if (minimum == 0)
            {
                return true;
            }
            if (pokemon.ivATK() >> 1 < minimum)
            {
                return false;
            }
            if (pokemon.ivDEF() >> 1 < minimum)
            {
                return false;
            }
            return pokemon.ivHP() >> 1 >= minimum;
        }

        bool PogoSlotTemplate::areIVsValid(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (!isIVAboveMinimum(pokemon) && isAlwaysSpecialTrade())
            {
                return false;
            }
            // Below Good Friends a trade drops the floor to 0, unless it is a Special Trade.
            if (minimumIV() == 1 && (pokemon.ivHP() == 1 || (pokemon.ivATK() == 1 && pokemon.ivSPA() == 1) || (pokemon.ivDEF() == 1 && pokemon.ivSPD() == 1)))
            {
                if (isShiny(pokemon) || SpeciesCategory::isSpecialPokemon(pokemon.speciesID()) || isPogoShadow(pogoType()))
                {
                    return false;
                }
            }
            // GO's attack and defense become ATK/SPA and DEF/SPD, each doubled plus one; Speed is random.
            if ((pokemon.ivATK() & 1) != 1 || pokemon.ivATK() != pokemon.ivSPA())
            {
                return false;
            }
            if ((pokemon.ivDEF() & 1) != 1 || pokemon.ivDEF() != pokemon.ivSPD())
            {
                return false;
            }
            return (pokemon.ivHP() & 1) == 1;
        }

        std::string PogoSlotTemplate::getLongName() const
        {
            const std::string start = getDateString(dayStart(), isLocalDayStart() ? 1 : 0, true);
            const std::string end = getDateString(dayEnd(), isLocalDayEnd() ? -1 : 0, false);
            return std::string("GO Encounter (GO) (") + getPogoTypeName(pogoType()) + "): " + start + "-" + end;
        }
    }
}
