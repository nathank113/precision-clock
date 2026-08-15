#ifndef CLK_H

#define CLK_H

#include <stdio.h>

#define DEFAULT_DATE 1
#define DEFAULT_MONTH 1
#define DEFAULT_YEAR 2000

typedef struct board_time
{
    uint8_t month;
    uint8_t date;
    uint16_t year;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint16_t milliseconds;
} Time;

int32_t ymd_to_days(uint16_t year, uint8_t month, uint8_t day);
int32_t time_to_ms(const Time* t);
void ms_to_time(uint32_t ms_of_day, Time *t);

#endif