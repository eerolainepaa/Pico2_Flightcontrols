#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "tusb.h"

// ================================================================
// CONFIG
// ================================================================

// Collective
#define COLLECTIVE_GPIO       28
#define COLLECTIVE_ADC_CH     2

#define ADC_SAMPLES           8

// Set true if collective moves in the wrong direction
#define INVERT_COLLECTIVE     false

// USB report interval
#define REPORT_PERIOD_MS      2

// Button debounce
#define DEBOUNCE_MS           10


// ================================================================
// BUTTON MATRIX
// ================================================================

#define MATRIX_ROWS 4
#define MATRIX_COLS 3

static const uint row_pins[MATRIX_ROWS] = {
    0,      // R0
    1,      // R1
    2,      // R2
    3       // R3
};

static const uint col_pins[MATRIX_COLS] = {
    6,      // C0
    7,      // C1
    8       // C2
    
};

static const int8_t button_map[MATRIX_ROWS][MATRIX_COLS] = {

    // C0   C1   C2

    { 0,    6,   8  },   // R0
    { 1,    5,   9  },   // R1
    { 2,    4,  10  },   // R2
    { 3,    7,  11  }    // R3
};


// ================================================================
// HID REPORT
// ================================================================

/*
 * Must match usb_descriptors.c exactly:
 *
 * 2 bytes X
 * 2 bytes Y
 * 2 bytes Z
 * 2 bytes buttons
 *
 * Total = 8 bytes
 */

typedef struct __attribute__((packed))
{
    int16_t x;
    int16_t y;
    int16_t z;

    uint16_t buttons;

} joystick_report_t;


// ================================================================
// BUTTON STATE
// ================================================================

static bool raw_state[MATRIX_ROWS][MATRIX_COLS] = {0};

static bool debounced_state[MATRIX_ROWS][MATRIX_COLS] = {0};

static uint32_t state_change_time[MATRIX_ROWS][MATRIX_COLS] = {0};

static uint16_t button_mask = 0;


// ================================================================
// ADC
// ================================================================

static void adc_setup(void)
{
    adc_init();

    adc_gpio_init(COLLECTIVE_GPIO);
}


static uint16_t read_adc_average(uint input)
{
    adc_select_input(input);

    uint32_t total = 0;

    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        total += adc_read();
    }

    return (uint16_t)(total / ADC_SAMPLES);
}


/*
 * Convert:
 *
 * ADC:
 *     0 ... 4095
 *
 * HID:
 *    -32767 ... +32767
 *
 * Collective is NOT centred at startup.
 * It uses the entire potentiometer range.
 */

static int16_t scale_collective(uint16_t raw)
{
    int32_t value =
        ((int32_t)raw * 65534L / 4095L) - 32767L;


    if (value > 32767)
    {
        value = 32767;
    }

    if (value < -32767)
    {
        value = -32767;
    }


    if (INVERT_COLLECTIVE)
    {
        value = -value;
    }


    return (int16_t)value;
}


// ================================================================
// MATRIX
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
    /*
     * All inactive rows are high impedance.
     */
    matrix_disable_all_rows();


    /*
     * Selected row is driven LOW.
     */
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

        /*
         * Column normally HIGH.
         *
         * Pressed switch connects it
         * to the active LOW row.
         */
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


        /*
         * Give signals time to settle.
         */
        sleep_us(5);


        for (int col = 0; col < MATRIX_COLS; col++)
        {
            int8_t button =
                button_map[row][col];


            // Unused matrix position
            if (button < 0)
            {
                continue;
            }


            /*
             * Column LOW = switch closed.
             */
            bool pressed =
                !gpio_get(
                    col_pins[col]
                );


            /*
             * Raw state changed.
             */
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


            /*
             * Raw signal has been stable
             * long enough -> accept it.
             */
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
// HID
// ================================================================

static void hid_task(void)
{
    static uint32_t last_report_ms = 0;

    static joystick_report_t last_report = {0};

    static bool first_report = true;


    uint32_t now =
        to_ms_since_boot(
            get_absolute_time()
        );


    if (
        (now - last_report_ms)
        < REPORT_PERIOD_MS
    )
    {
        return;
    }


    last_report_ms = now;


    if (!tud_hid_ready())
    {
        return;
    }


    joystick_report_t report;


    /*
     * Cyclic is not connected yet.
     *
     * Keep X/Y centred.
     */

    report.x = 0;
    report.y = 0;


    /*
     * Collective is currently Z axis.
     */

    uint16_t raw_collective =
        read_adc_average(
            COLLECTIVE_ADC_CH
        );


    report.z =
        scale_collective(
            raw_collective
        );


    /*
     * Buttons 1-11.
     */

    report.buttons =
        button_mask;


    /*
     * Only send a USB packet when
     * something changed.
     */

    if (
        first_report ||
        memcmp(
            &report,
            &last_report,
            sizeof(report)
        ) != 0
    )
    {
        if (
            tud_hid_report(
                0,
                &report,
                sizeof(report)
            )
        )
        {
            last_report =
                report;

            first_report =
                false;
        }
    }
}


// ================================================================
// MAIN
// ================================================================

int main(void)
{
    stdio_init_all();


    adc_setup();

    matrix_init();


    /*
     * TinyUSB device stack.
     */
    tusb_init();


    while (true)
    {
        /*
         * TinyUSB must run continuously.
         */
        tud_task();


        /*
         * Scan physical switches.
         */
        matrix_scan();


        /*
         * Send joystick state.
         */
        hid_task();


        /*
         * ~1 kHz main loop.
         */
        sleep_ms(1);
    }
}