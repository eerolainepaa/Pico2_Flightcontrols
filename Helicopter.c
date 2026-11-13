#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "tusb.h"
#include "ads1115.h"
#include "axis_scaling.h"
#include "joystick_report.h"

// Pedals use the Pico ADC. Cyclic, collective and throttle use ADS1115 AIN0..3.
#define RUDDER_GPIO          26
#define LEFT_BRAKE_GPIO      27
#define RIGHT_BRAKE_GPIO     28
#define ADC_SAMPLES          8

#define INVERT_CYCLIC_X      false
#define INVERT_CYCLIC_Y      false
#define INVERT_COLLECTIVE    false
#define INVERT_THROTTLE      false
#define INVERT_RUDDER        false
#define INVERT_LEFT_BRAKE    false
#define INVERT_RIGHT_BRAKE   false

#define REPORT_PERIOD_MS     2
#define INPUT_PERIOD_MS      1
#define DEBOUNCE_MS          10

// Startup values until the first valid readings: cyclic/rudder centered,
// collective/throttle/brakes at the low electrical end. No startup calibration.
static joystick_report_t report = {
    .x = 0, .y = 0, .z = -32767, .rx = -32767,
    .ry = -32767, .rz = 0, .slider = -32767, .buttons = 0
};

// ================================================================
// BUTTON MATRIX
// ================================================================

#define MATRIX_ROWS 4
#define MATRIX_COLS 3

static const uint row_pins[MATRIX_ROWS] = {
    0,  // R0
    1,  // R1
    2,  // R2
    3   // R3
};

static const uint col_pins[MATRIX_COLS] = {
    6,  // C0
    7,  // C1
    8   // C2
};


/*
 * Matrix:
 *
 *              C0          C1          C2
 *
 * R0 GP0       BTN7        BTN3        SW1 UP
 * R1 GP1       BTN6        BTN2        SW1 DOWN
 * R2 GP2       BTN5        BTN1        SW2 UP
 * R3 GP3       BTN4        BTN8        SW2 DOWN
 *
 *
 * HID:
 *
 * Button 1  = BTN1
 * Button 2  = BTN2
 * Button 3  = BTN3
 * Button 4  = BTN4
 * Button 5  = BTN5
 * Button 6  = BTN6
 * Button 7  = BTN7
 * Button 8  = BTN8
 * Button 9  = SW1 UP
 * Button 10 = SW1 DOWN
 * Button 11 = SW2 UP
 * Button 12 = SW2 DOWN
 *
 * Values below are zero-based HID bit positions.
 */

static const int8_t button_map[MATRIX_ROWS][MATRIX_COLS] = {

    // C0   C1   C2

    { 6,    2,    8 },   // R0
    { 5,    1,    9 },   // R1
    { 4,    0,   10 },   // R2
    { 3,    7,   11 }    // R3
};


// ================================================================
// BUTTON STATE
// ================================================================

static bool raw_state[MATRIX_ROWS][MATRIX_COLS] = {0};

static bool debounced_state[MATRIX_ROWS][MATRIX_COLS] = {0};

static uint32_t state_change_time[MATRIX_ROWS][MATRIX_COLS] = {0};

static uint16_t button_mask = 0;


// ================================================================
// AXIS INPUTS -- fixed electrical scaling, no center calibration or deadzone
// ================================================================

static void adc_setup(void)
{
    adc_init();
    adc_gpio_init(RUDDER_GPIO);
    adc_gpio_init(LEFT_BRAKE_GPIO);
    adc_gpio_init(RIGHT_BRAKE_GPIO);
}

static uint16_t read_adc_average(uint gpio)
{
    adc_select_input(gpio - 26);
    (void)adc_read(); // Discard the first sample after changing the multiplexer.
    uint32_t total = 0;
    for (int i = 0; i < ADC_SAMPLES; ++i) total += adc_read();
    return (uint16_t)((total + ADC_SAMPLES / 2) / ADC_SAMPLES);
}

static void sample_pedals(void)
{
    report.rz = scale_axis(read_adc_average(RUDDER_GPIO), PICO_ADC_FULL_SCALE, INVERT_RUDDER);
    report.rx = scale_axis(read_adc_average(LEFT_BRAKE_GPIO), PICO_ADC_FULL_SCALE, INVERT_LEFT_BRAKE);
    report.ry = scale_axis(read_adc_average(RIGHT_BRAKE_GPIO), PICO_ADC_FULL_SCALE, INVERT_RIGHT_BRAKE);
}

static void sample_ads1115(void)
{
    uint8_t channel;
    int16_t raw;
    if (!ads1115_poll(&channel, &raw)) return;
    // Update only a completed channel. Keep the last good value on I2C failure.
    switch (channel) {
        case 0: report.x = scale_axis(raw, ADS1115_COUNTS_AT_3V3, INVERT_CYCLIC_X); break;
        case 1: report.y = scale_axis(raw, ADS1115_COUNTS_AT_3V3, INVERT_CYCLIC_Y); break;
        case 2: report.z = scale_axis(raw, ADS1115_COUNTS_AT_3V3, INVERT_COLLECTIVE); break;
        case 3: report.slider = scale_axis(raw, ADS1115_COUNTS_AT_3V3, INVERT_THROTTLE); break;
    }
}

// ================================================================
// BUTTON MATRIX
// ================================================================

static void matrix_disable_all_rows(void)
{
    for (int row = 0; row < MATRIX_ROWS; row++)
    {
        gpio_set_dir(
            row_pins[row],
            GPIO_IN
        );

        gpio_disable_pulls(
            row_pins[row]
        );
    }
}


static void matrix_activate_row(int row)
{
    //Inactive rows = high impedance.
    matrix_disable_all_rows();


    // Current row = LOW.

    gpio_put(
        row_pins[row],
        0
    );

    gpio_set_dir(
        row_pins[row],
        GPIO_OUT
    );
}


static void matrix_init(void)
{
    // Rows

    for (int row = 0; row < MATRIX_ROWS; row++)
    {
        gpio_init(
            row_pins[row]
        );

        gpio_set_dir(
            row_pins[row],
            GPIO_IN
        );

        gpio_disable_pulls(
            row_pins[row]
        );
    }


    // Columns

    for (int col = 0; col < MATRIX_COLS; col++)
    {
        gpio_init(
            col_pins[col]
        );

        gpio_set_dir(
            col_pins[col],
            GPIO_IN
        );

        
        // Columns normally HIGH.
         
        gpio_pull_up(
            col_pins[col]
        );
    }
}


// ================================================================
// MATRIX SCANNING + DEBOUNCE
// ================================================================

static void matrix_scan(void)
{
    uint32_t now =
        to_ms_since_boot(
            get_absolute_time()
        );


    for (int row = 0; row < MATRIX_ROWS; row++)
    {
        matrix_activate_row(row);

        sleep_us(5);


        for (int col = 0; col < MATRIX_COLS; col++)
        {
            int8_t button =
                button_map[row][col];


            bool pressed =
                !gpio_get(
                    col_pins[col]
                );


          
            // Raw electrical state changed.
            
            if (
                pressed !=
                raw_state[row][col]
            )
            {
                raw_state[row][col] =
                    pressed;

                state_change_time[row][col] =
                    now;
            }


          
            // Accept the new state after debounce.
            
            if (
                (now -
                 state_change_time[row][col])
                >= DEBOUNCE_MS
            )
            {
                if (
                    debounced_state[row][col]
                    != pressed
                )
                {
                    debounced_state[row][col] =
                        pressed;


                    if (pressed)
                    {
                        button_mask |=
                            ((uint16_t)1u << button);
                    }
                    else
                    {
                        button_mask &=
                            ~((uint16_t)1u << button);
                    }
                }
            }
        }
    }


    matrix_disable_all_rows();
}


// ================================================================
// USB HID -- seven axes and button matrix
// ================================================================

static void hid_task(void)
{
    static uint32_t last_report_ms;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_report_ms) < REPORT_PERIOD_MS || !tud_hid_ready()) return;
    report.buttons = button_mask;
    // Periodic reports also resend the current position after USB reconnect/resume.
    if (tud_hid_report(0, &report, sizeof(report))) last_report_ms = now;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                             hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    if (instance != 0 || report_id != 0 || report_type != HID_REPORT_TYPE_INPUT) return 0;
    uint16_t length = reqlen < sizeof(report) ? reqlen : sizeof(report);
    memcpy(buffer, &report, length);
    return length;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                         hid_report_type_t report_type, const uint8_t *buffer, uint16_t size)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)size;
}

int main(void)
{
    adc_setup();
    matrix_init();
    ads1115_init();
    sample_pedals();
    tusb_init();
    uint32_t last_input_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        tud_task();
        sample_ads1115(); // No sleep while the ADS1115 is converting.
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now - last_input_ms) >= INPUT_PERIOD_MS) {
            last_input_ms = now;
            matrix_scan();
            report.buttons = button_mask;
            sample_pedals();
        }
        hid_task();
        tight_loop_contents();
    }
}
