#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "tusb.h"

// ================================================================
// CONFIG
// ================================================================

// Cyclic X
#define CYCLIC_X_GPIO         26
#define CYCLIC_X_ADC_CH       0

// Cyclic Y
#define CYCLIC_Y_GPIO         27
#define CYCLIC_Y_ADC_CH       1

// Collective
#define COLLECTIVE_GPIO       28
#define COLLECTIVE_ADC_CH     2

#define ADC_SAMPLES           8

// Cyclic deadzone around calibrated center
#define CYCLIC_DEADZONE       50

// Change these if an axis moves in the wrong direction
#define INVERT_CYCLIC_X       false
#define INVERT_CYCLIC_Y       false
#define INVERT_COLLECTIVE     false

#define REPORT_PERIOD_MS      2
#define DEBOUNCE_MS           10


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
// HID REPORT
// ================================================================

typedef struct __attribute__((packed))
{
    int16_t x;
    int16_t y;
    int16_t z;

    uint16_t buttons;

} joystick_report_t;


// ================================================================
// AXIS STATE
// ================================================================

static uint16_t cyclic_x_center = 2048;
static uint16_t cyclic_y_center = 2048;


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

    adc_gpio_init(CYCLIC_X_GPIO);
    adc_gpio_init(CYCLIC_Y_GPIO);
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


// ================================================================
// CYCLIC CALIBRATION
// ================================================================

static void calibrate_cyclic(void)
{
    /*
     * Keep cyclic centered while Pico starts.
     *
     * Take several readings and use their average
     * as the mechanical center.
     */

    uint32_t x_total = 0;
    uint32_t y_total = 0;

    const int samples = 64;


    for (int i = 0; i < samples; i++)
    {
        x_total += read_adc_average(CYCLIC_X_ADC_CH);
        y_total += read_adc_average(CYCLIC_Y_ADC_CH);

        sleep_ms(2);
    }


    cyclic_x_center =
        (uint16_t)(x_total / samples);

    cyclic_y_center =
        (uint16_t)(y_total / samples);
}


// ================================================================
// CYCLIC AXIS SCALING
// ================================================================

static int16_t scale_cyclic(
    uint16_t raw,
    uint16_t center,
    bool invert
)
{
    int32_t delta =
        (int32_t)raw -
        (int32_t)center;


    /*
     * Deadzone around center.
     */
    if (
        delta > -CYCLIC_DEADZONE &&
        delta < CYCLIC_DEADZONE
    )
    {
        delta = 0;
    }
    else if (delta > 0)
    {
        delta -= CYCLIC_DEADZONE;
    }
    else
    {
        delta += CYCLIC_DEADZONE;
    }


    /*
     * Positive and negative travel can be different
     * because the calibrated center may not be
     * exactly 2048.
     */

    int32_t positive_span =
        4095 -
        center -
        CYCLIC_DEADZONE;

    int32_t negative_span =
        center -
        CYCLIC_DEADZONE;


    if (positive_span < 1)
    {
        positive_span = 1;
    }

    if (negative_span < 1)
    {
        negative_span = 1;
    }


    int32_t output;


    if (delta >= 0)
    {
        output =
            (delta * 32767L) /
            positive_span;
    }
    else
    {
        output =
            (delta * 32767L) /
            negative_span;
    }


    if (output > 32767)
    {
        output = 32767;
    }

    if (output < -32767)
    {
        output = -32767;
    }


    if (invert)
    {
        output = -output;
    }


    return (int16_t)output;
}


// ================================================================
// COLLECTIVE AXIS SCALING
// ================================================================

static int16_t scale_collective(uint16_t raw)
{
    /*
     * Collective uses the complete potentiometer range.
     *
     * ADC 0    -> -32767
     * ADC 2048 -> ~0
     * ADC 4095 -> +32767
     */

    int32_t output =
        ((int32_t)raw * 65534L / 4095L)
        - 32767L;


    if (output > 32767)
    {
        output = 32767;
    }

    if (output < -32767)
    {
        output = -32767;
    }


    if (INVERT_COLLECTIVE)
    {
        output = -output;
    }


    return (int16_t)output;
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
    /*
     * Inactive rows = high impedance.
     */
    matrix_disable_all_rows();


    /*
     * Current row = LOW.
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
         * Columns normally HIGH.
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

        sleep_us(5);


        for (int col = 0; col < MATRIX_COLS; col++)
        {
            int8_t button =
                button_map[row][col];


            bool pressed =
                !gpio_get(
                    col_pins[col]
                );


            /*
             * Raw electrical state changed.
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
             * Accept the new state after debounce.
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
// HID TASK
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


    /*
     * Read all three analog controls.
     */

    uint16_t raw_x =
        read_adc_average(
            CYCLIC_X_ADC_CH
        );

    uint16_t raw_y =
        read_adc_average(
            CYCLIC_Y_ADC_CH
        );

    uint16_t raw_collective =
        read_adc_average(
            COLLECTIVE_ADC_CH
        );


    joystick_report_t report;


    report.x =
        scale_cyclic(
            raw_x,
            cyclic_x_center,
            INVERT_CYCLIC_X
        );


    report.y =
        scale_cyclic(
            raw_y,
            cyclic_y_center,
            INVERT_CYCLIC_Y
        );


    report.z =
        scale_collective(
            raw_collective
        );


    report.buttons =
        button_mask;


    /*
     * Only send report if something changed.
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
     * IMPORTANT:
     *
     * Keep cyclic centered during this calibration.
     *
     * Collective position does not matter.
     */
    calibrate_cyclic();


    /*
     * Start TinyUSB HID device.
     */
    tusb_init();


    while (true)
    {
        /*
         * USB stack.
         */
        tud_task();


        /*
         * Buttons and switches.
         */
        matrix_scan();


        /*
         * X, Y, collective and buttons -> USB HID.
         */
        hid_task();


        sleep_ms(1);
    }
}
