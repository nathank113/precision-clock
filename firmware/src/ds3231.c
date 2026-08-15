#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "clock_time.h"

#define I2C_SDA_PIN 36
#define I2C_SCL_PIN 37
#define BAUDRATE 100000
#define RTC_ADDR 0x68

void init_i2c(){
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    i2c_init(i2c0, BAUDRATE);
}

uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10); //converts decimal to bcd (binary coded decimal)
}

uint8_t bcd_to_dec(uint8_t bcd) {
    uint8_t ones = bcd;
    return ((bcd>>4) * 10) + (ones & 0xF); //converts bcd to decimal
}

//set RTC seconds
void set_rtc_seconds(uint8_t sec){
    uint8_t addr_val[2] = {0x00, dec_to_bcd(sec)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set RTC minutes
void set_rtc_minutes(uint8_t min){
    uint8_t addr_val[2] = {0x01, dec_to_bcd(min)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set RTC hours
void set_rtc_hours(uint8_t hours){
    uint8_t addr_val[2] = {0x02, dec_to_bcd(hours)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set RTC Date
void set_rtc_date(uint8_t date){
    uint8_t addr_val[2] = {0x04, dec_to_bcd(date)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set RTC Month
void set_rtc_month(uint8_t month){
    uint8_t addr_val[2] = {0x05, dec_to_bcd(month)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set RTC Year
void set_rtc_year(uint8_t year){
    uint8_t addr_val[2] = {0x06, dec_to_bcd(year)}; 
    i2c_write_blocking(i2c0, RTC_ADDR, addr_val, 2, false);
}

//set rtc using Time Struct
void set_rtc_all(Time time){
    set_rtc_seconds(time.seconds);
    set_rtc_minutes(time.minutes);
    set_rtc_hours(time.hours);
    set_rtc_date(time.date);
    set_rtc_month(time.month);
    set_rtc_year(time.year);
}

//reads rtc data returns Time Struct
void read_rtc(Time * time) {
    uint8_t num_bytes; //number of bytes read
    uint8_t rtc_data[7];
    uint8_t start_reg = 0x00;  // start at seconds register
    i2c_write_blocking(i2c0, RTC_ADDR, &start_reg, 1, true); // set read incriment to 0x0 reg
    num_bytes = i2c_read_blocking(i2c0, RTC_ADDR, rtc_data, 7, false); //read 7 bytes (rtc data)
    
    if(num_bytes != 7) {
        printf("RTC READ ERROR");
    }

    //process and store time data in Time struct
    time->seconds = bcd_to_dec(rtc_data[0]);
    time->minutes = bcd_to_dec(rtc_data[1]);
    time->hours = bcd_to_dec(rtc_data[2]);
    time->date = bcd_to_dec(rtc_data[4]);
    time->month = bcd_to_dec(rtc_data[5]);
    time->year = bcd_to_dec(rtc_data[6]);
    time->milliseconds = 0;
}