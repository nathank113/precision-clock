#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/uart.h"

#include "gps/gps.h"
#include "gps/PMTK.h"

#define GPS_UART ((uart_inst_t *)uart1_hw) // Identifier for UART instance for gps
#define GPS_TX_PIN 4 // TX PIN
#define GPS_RX_PIN 5 // RX PIN


#define NUM_NMEA_STRINGS (6)
#define NMEA_STRING_SIZE (128)
#define NMEA_BUF_SIZE 128

struct gps_tpv tpv;
Time time_alloc;

char nmea_buffer[256];
uint8_t buffer_index = 0;
uint8_t star_detected = -1;

extern Time localclk;
extern Time last_pps;

void test_timing() {
    absolute_time_t start, end;
    char nmea[NUM_NMEA_STRINGS][NMEA_STRING_SIZE];
    unsigned int i;

    /* Setup */
    gps_init_tpv(&tpv);
    strncpy(nmea[0], "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n", NMEA_STRING_SIZE);
    strncpy(nmea[1], "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\r\n", NMEA_STRING_SIZE);
    strncpy(nmea[2], "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", NMEA_STRING_SIZE);
    strncpy(nmea[3], "$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r\n", NMEA_STRING_SIZE);
    strncpy(nmea[4], "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n", NMEA_STRING_SIZE);
    strncpy(nmea[5], "$GPZDA,201530.00,04,07,2002,00,00*60\r\n", NMEA_STRING_SIZE);

    /* Start timing and run */
    start = get_absolute_time();

    for (i = 0; i < NUM_NMEA_STRINGS; ++i)
    {
        gps_decode(&tpv, nmea[i]);
    }

    /* End timing and report */
    end = get_absolute_time();

    printf("Time taken to decode %d NMEA sentences: %d us\n", NUM_NMEA_STRINGS, (int) absolute_time_diff_us(start, end));
}

void print_tpv_value(const char *name, const char *format, const int32_t value, const int32_t scale_factor)
{
    printf("%s: ", name);
    if (GPS_INVALID_VALUE != value)
    {
        printf(format, (double)value / scale_factor);
    }
    else
    {
        puts("INVALID");
    }
}

void test_decode_print(char *str) {
    int result;

    // /* Sets the data to a known state */
    gps_init_tpv(&tpv);

    /* Attempt to decode the user supplied string */
    result = gps_decode(&tpv, str);
    if (result != GPS_OK)
    {
        fprintf(stderr, "Error (%d): %s\n\n", result, gps_error_string(result));
        return;
    }

    /* Go through each TPV value and show what information was decoded */
    printf("Talker ID: %s\n", tpv.talker_id);
    printf("Time Stamp: %04d-%02d-%02dT%02d:%02d:%02d.%03d\n", tpv.time->year, tpv.time->month, tpv.time->date, tpv.time->hours, tpv.time->minutes, tpv.time->seconds, tpv.time->milliseconds); // ISO8601 : YYYY-MM-DDTHH:MM:SS.SSSZ
    print_tpv_value("Latitude", "%.6f\n", tpv.latitude, GPS_LAT_LON_FACTOR);
    print_tpv_value("Longitude", "%.6f\n", tpv.longitude, GPS_LAT_LON_FACTOR);
    print_tpv_value("Altitude", "%.3f\n", tpv.altitude, GPS_VALUE_FACTOR);
    print_tpv_value("Track", "%.3f\n", tpv.track, GPS_VALUE_FACTOR);
    print_tpv_value("Speed", "%.3f\n", tpv.speed, GPS_VALUE_FACTOR);

    printf("Mode: ");
    switch (tpv.mode)
    {
    case GPS_MODE_UNKNOWN:
        puts("Unknown");
        break;
    case GPS_MODE_NO_FIX:
        puts("No fix");
        break;
    case GPS_MODE_2D_FIX:
        puts("2D");
        break;
    case GPS_MODE_3D_FIX:
        puts("3D");
        break;
    default:
        break;
    }

    printf("\n\n");
}

void clock_decode(char * str) {
    int result;

    // /* Sets the data to a known state */
    gps_init_tpv(&tpv);

    /* Attempt to decode the user supplied string */
    result = gps_decode(&tpv, str);
    if (result != GPS_OK)
    {
        fprintf(stderr, "Error (%d): %s\n\n", result, gps_error_string(result));
        return;
    }

    if (tpv.time->year != DEFAULT_YEAR)
    {
        // Set clock time
        // Assume that the date of the GPS receive is correct
        localclk.year = tpv.time->year;
        localclk.month = tpv.time->month;
        localclk.date = tpv.time->date;
        
        // Update ms of the day
        int32_t delta_ms = time_to_ms(tpv.time) - time_to_ms(&last_pps);
        int32_t new_time = time_to_ms(&localclk) + delta_ms;
        if (new_time > 86400000LL)
        {
            localclk.date ++;
            new_time -= 86400000LL;
        } 
        else if (new_time < 0)
        {
            localclk.date --;
            new_time += 86400000LL;
        }
        ms_to_time(new_time, &localclk);
    }
}

void init_uart_gps() {
    gpio_set_function(GPS_TX_PIN, UART_FUNCSEL_NUM(GPS_UART, GPS_TX_PIN));
    gpio_set_function(GPS_RX_PIN, UART_FUNCSEL_NUM(GPS_UART, GPS_RX_PIN));

    uart_init(GPS_UART, 9600);

    //uart_write_blocking(GPS_UART, PMTK_SET_BAUD_57600, strlen(PMTK_SET_BAUD_57600)); this function isnt working
    //sleep_ms(MAXWAITSENTENCE);

    //uart_set_baudrate(GPS_UART, 57600);

    uart_write_blocking(GPS_UART, PMTK_SET_NMEA_OUTPUT_RMCGGA, strlen(PMTK_SET_NMEA_OUTPUT_RMCGGA));
    printf("%d\n%s", strlen(PMTK_SET_NMEA_OUTPUT_RMCGGA), PMTK_SET_NMEA_OUTPUT_RMCGGA);
    sleep_ms(MAXWAITSENTENCE);

    uart_write_blocking(GPS_UART, PMTK_SET_NMEA_UPDATE_1HZ, strlen(PMTK_SET_NMEA_UPDATE_1HZ));
    printf("%d\n%s", strlen(PMTK_SET_NMEA_UPDATE_1HZ), PMTK_SET_NMEA_UPDATE_1HZ);
    sleep_ms(MAXWAITSENTENCE);

    tpv.time = &time_alloc;
}

void read_nmea_sentence() {
    while (uart_is_readable(GPS_UART)) {
        char c = uart_getc(GPS_UART);

        if (c == '\n' && buffer_index > 0 && nmea_buffer[buffer_index - 1] == '\r') {
            nmea_buffer[buffer_index] = '\n'; // Null-terminate the string
            nmea_buffer[buffer_index + 1] = '\0'; // Null-terminate the string
            printf(nmea_buffer);
            clock_decode(nmea_buffer);
            buffer_index = 0;
        } else if (buffer_index < sizeof(nmea_buffer) - 1) {
            nmea_buffer[buffer_index++] = c;
        } else {
            // Buffer overflow, handle as needed
            buffer_index = 0; // Reset
        }
    }
}