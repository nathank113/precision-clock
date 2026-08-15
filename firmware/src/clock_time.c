/* Functions for clock.h Time struct */
#include <stdlib.h>
#include <stdio.h>
#include "clock_time.h"

int32_t ymd_to_days(uint16_t year, uint8_t month, uint8_t day)
{
    // Shift months so March is month 1 for easier leap-year math
    if (month <= 2) {
        year -= 1;
        month += 12;
    }

    const int32_t era = year / 400;
    const int32_t yoe = year - era * 400;
    const int32_t doy = (153 * (month - 3) + 2) / 5 + day - 1;
    const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe;   // days since 0000-03-01
}


int32_t time_to_ms(const Time* t)
{
    int32_t ms = ((int64_t)t->hours * 3600 * 1000)
               + ((int64_t)t->minutes * 60 * 1000)
               + ((int64_t)t->seconds * 1000)
               + t->milliseconds;

    return ms;
}

void ms_to_time(uint32_t ms_of_day, Time *t)
{
    // Hours
    t->hours = ms_of_day / 3600000u;   // 1000 * 60 * 60
    ms_of_day %= 3600000u;

    // Minutes
    t->minutes = ms_of_day / 60000u;   // 1000 * 60
    ms_of_day %= 60000u;

    // Seconds
    t->seconds = ms_of_day / 1000u;

    // Milliseconds
    t->milliseconds = ms_of_day % 1000u;

    // Date fields stay untouched
}

