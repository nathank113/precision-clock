#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#include "clock_time.h"

#define PPS_GPIO 2
#define DISPBUS_LSB 8
#define DISPBUS_SIZE 12
#define DISPBUS_BITS 0xFFF
#define GPS_PPS_TIMEOUT 10 // Time that clock switches from GPS to RTC mode, in seconds, since last PPS rising edge
#define CLK_TUNING_STEP 0.01 // Proportional factor to tune local clock
#define RTC_UPDATE_RATE 500 // update rate in ms

#define ADC_PIN 40       // GPIO29 = ADC3
#define ADC_INPUT 0
#define VREF 3.3f
#define ADC_MAX_COUNT 4095.0f
#define LOW_VOLTAGE_THRESH 2.8f
#define VOLTAGE_RECOVER_THRESH 2.9f   // hysteresis reset
#define BATTERY_SAMPLE_MS 500  
volatile uint16_t adc_fifo_out = 0;
volatile bool battery_low_flag = false;

// Display function defs
void clock_disp_update_isr();
void update_clock_font(Time t, bool ms_enable, bool decimal);

// RTC function defs
void init_i2c();
void set_rtc_all(Time time);
void read_rtc(Time * time);

// GPS function defs
void init_uart_gps();
void read_nmea_sentence();

bool gps_active; // True: GPS ACTIVE | False: RTC TIME

uint16_t millisecond_update_rate; // 1000 by default, increase to slow clock down, decrease to speed clock up

Time localclk; // Current time
Time last_pps; // Time of last PPS rising edge

void pps_isr()
{
    gpio_acknowledge_irq(PPS_GPIO, GPIO_IRQ_EDGE_RISE); // Ack interrupt
    
    if (localclk.milliseconds) // If milliseconds isnt zero upon PPS
    {
        if (localclk.milliseconds < 500) // If milliseconds is less than 500, assume wraparound and clock is running slightly fast
        {
            millisecond_update_rate += localclk.milliseconds * CLK_TUNING_STEP; // TODO: Determine if you want proportional control
            localclk.milliseconds = 0; // Correct milliseconds
        }
        else // If milliseconds is 501 to 999, then clock is running slow
        {
            millisecond_update_rate -= (1000 - localclk.milliseconds) * CLK_TUNING_STEP;
            localclk.milliseconds = 0; // Wrap to next second
            localclk.seconds ++;
        }
    }
    
    memcpy(&last_pps, &localclk, sizeof(Time)); // Copy new current time to PPS

    gps_active = 1; // Switch back to GPS mode in case it was previously in RTC mode
    uint64_t target = timer1_hw->timerawl + 1000000 * GPS_PPS_TIMEOUT;
    timer1_hw->alarm[0] = (uint32_t) target; // Set new GPS PPS Timeout
}

void gps_timeout_isr()
{
    hw_clear_bits(&timer1_hw->intr, 1 << 0);
    gps_active = 0;
}

void clock_rtc_update_isr()
{
    hw_clear_bits(&timer1_hw->intr, 1 << 1);
    if (gps_active)
        set_rtc_all(localclk); // Update RTC every second if GPS active
    else
    {
        read_rtc(&localclk); // else update time from RTC
        localclk.milliseconds = 0;
    }
    uint64_t target = timer1_hw->timerawl + RTC_UPDATE_RATE * 1000;
    timer1_hw->alarm[1] = (uint32_t) target;
}

void clock_ms_update_isr()
{
    // TODO: Consider using a PWM slice counter peripheral instead of timer alarms for less jitter/more accuracy
    hw_clear_bits(&timer0_hw->intr, 1 << 0);
    uint64_t target = timer0_hw->timerawl + millisecond_update_rate; // t + 1ms
    timer0_hw->alarm[0] = (uint32_t) target; // Set new alarm immediately after clearing flag to reduce jitter
    if (++localclk.milliseconds > 999)
    {
        localclk.milliseconds = 0;
        if (++localclk.seconds > 59)
        {
            localclk.seconds = 0;
            if (++localclk.minutes > 59)
            {
                localclk.minutes = 0;
                if (++localclk.hours > 23)
                    localclk.hours = 0;
            }
        }
    }
    
    update_clock_font(localclk, gps_active, battery_low_flag);
}

void battery_check_isr(void)
{
    hw_clear_bits(&timer1_hw->intr, 1 << 2);

    float vbat = (adc_fifo_out * VREF) / ADC_MAX_COUNT;
    printf("Battery voltage: %.3f V\n", vbat);

    // low-voltage detect
    if (!battery_low_flag && vbat <= LOW_VOLTAGE_THRESH)
    {
        battery_low_flag = true;
        printf("Backup battery low (%.3f V)\n", vbat);
    }
    // battery replace detect
    else if (battery_low_flag && vbat >= VOLTAGE_RECOVER_THRESH)
    {
        battery_low_flag = false;
        printf("Battery replaced (%.3f V)\n", vbat);
    }
    // re-arm alarm 2 for next check
    timer1_hw->alarm[2] = (uint32_t)(timer1_hw->timerawl + BATTERY_SAMPLE_MS * 1000ULL);
}

void init_dma(void)
{
    dma_hw->ch[0].read_addr = &adc_hw->fifo;
    dma_hw->ch[0].write_addr = &adc_fifo_out;
    dma_hw->ch[0].transfer_count =
        (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << 28) | 1;
    dma_hw->ch[0].ctrl_trig = 0;

    uint32_t temp =
        (DMA_CH0_CTRL_TRIG_DATA_SIZE_VALUE_SIZE_HALFWORD
         << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
    temp |= (DREQ_ADC << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB);
    temp |= (1u << DMA_CH0_CTRL_TRIG_EN_LSB);
    dma_hw->ch[0].ctrl_trig = temp;
}

void init_adc_freerun(void)
{
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_INPUT);
    hw_set_bits(&adc_hw->cs, ADC_CS_START_MANY_BITS); // continuous conversions
}

void init_adc_dma(void)
{
    init_dma();
    init_adc_freerun();
    adc_hw->fcs |= ADC_FCS_EN_BITS;      // enable FIFO
    adc_hw->fcs |= ADC_FCS_DREQ_EN_BITS; // enable DMA requests
}

void init_timers()
{
    // Enable the interrupt for alarms
    hw_set_bits(&timer0_hw->inte, 0x3);
    hw_set_bits(&timer1_hw->inte, 0x7);
    // Set irq handler for alarm irq 0 and 1
    irq_set_exclusive_handler(TIMER0_IRQ_0, clock_ms_update_isr);
    irq_set_exclusive_handler(TIMER0_IRQ_1, clock_disp_update_isr);
    irq_set_exclusive_handler(TIMER1_IRQ_0, gps_timeout_isr);
    irq_set_exclusive_handler(TIMER1_IRQ_1, clock_rtc_update_isr);
    irq_set_exclusive_handler(TIMER1_IRQ_2, battery_check_isr);
    // Enable the alarm irqs
    irq_set_enabled(TIMER0_IRQ_0, 1);
    irq_set_enabled(TIMER0_IRQ_1, 1);
    irq_set_enabled(TIMER1_IRQ_0, 1);
    irq_set_enabled(TIMER1_IRQ_1, 1);
    irq_set_enabled(TIMER1_IRQ_2, 1);
    
    // Set timer 0 trigger time - 1ms
    uint64_t target_0 = timer0_hw->timerawl + millisecond_update_rate;
    // Set timer 1 trigger time
    uint64_t target_1 = timer0_hw->timerawl + 10;
    
    uint64_t target_3 = timer1_hw->timerawl + RTC_UPDATE_RATE * 1000;

    uint64_t target_4 = timer1_hw->timerawl + (BATTERY_SAMPLE_MS * 1000ULL); // 5 s

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer0_hw->alarm[0] = (uint32_t) target_0;
    timer0_hw->alarm[1] = (uint32_t) target_1;

    timer1_hw->alarm[1] = (uint32_t) target_3;
    timer1_hw->alarm[2] = (uint32_t) target_4;
}

void init_gpio()
{
    // Set directions to output and default to zero
    sio_hw->gpio_oe_set = DISPBUS_BITS << DISPBUS_LSB;
    sio_hw->gpio_clr = DISPBUS_BITS << DISPBUS_LSB;
    // Input enable on, output disable off
    for (int i = DISPBUS_LSB; i < DISPBUS_LSB + DISPBUS_SIZE; i++)
    {
        hw_write_masked(&pads_bank0_hw->io[i], PADS_BANK0_GPIO0_IE_BITS, PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
        io_bank0_hw->io[i].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
        #if HAS_PADS_BANK0_ISOLATION
            // Remove pad isolation now that the correct peripheral is in control of the pad
            hw_clear_bits(&pads_bank0_hw->io[i], PADS_BANK0_GPIO0_ISO_BITS);
        #endif
    }

    // fill in
    sio_hw->gpio_oe_clr = (1 << PPS_GPIO);
    // Input enable on, output disable off
    hw_write_masked(&pads_bank0_hw->io[PPS_GPIO], PADS_BANK0_GPIO0_IE_BITS, PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    // Set GPIO functions to SIO
    io_bank0_hw->io[PPS_GPIO].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    #if HAS_PADS_BANK0_ISOLATION
        // Remove pad isolation now that the correct peripheral is in control of the pad
        hw_clear_bits(&pads_bank0_hw->io[PPS_GPIO], PADS_BANK0_GPIO0_ISO_BITS);
    #endif

    // Add handlers
    gpio_add_raw_irq_handler_masked((1 << PPS_GPIO), pps_isr);
    // Enable rising edge interrupts
    gpio_set_irq_enabled(PPS_GPIO, GPIO_IRQ_EDGE_RISE, 1);
    // Enable bank0 IRQ
    irq_set_enabled(IO_IRQ_BANK0, 1);
}

void clock_init()
{
    printf("Initializing Clock\n");

    init_gpio();
    init_i2c();
    init_uart_gps();
    init_adc_dma();

    millisecond_update_rate = 1000;
    gps_active = 0; // Default to GPS off and RTC mode

    read_rtc(&localclk);
    localclk.milliseconds = 0;

    update_clock_font(localclk, gps_active, battery_low_flag);

    init_timers(); // Do this after all long setup tasks as it starts the timers
}

int main()
{
    // Configures our microcontroller to 
    // communicate over UART through the TX/RX pins
    stdio_init_all();

    clock_init();
    
    for (;;)
        read_nmea_sentence();
    return 0;
}
