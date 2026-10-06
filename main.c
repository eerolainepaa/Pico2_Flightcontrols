/*
 * USB HID Joystick for Raspberry Pi Pico 2 W (RP2350)
 *
 * Wiring:
 *   Stick X axis (pot wiper)  -> GP26 (ADC0)
 *   Stick Y axis (pot wiper)  -> GP27 (ADC1)
 *   Pot ends                  -> 3V3(OUT) and AGND
 *   Buttons 1-8               -> GP2..GP9, other side to GND (internal pull-ups)
 *
 * The Wi-Fi/Bluetooth chip is not used, so no cyw43 code is needed.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "tusb.h"

// ---------------------------------------------------------------- config
#define ADC_X_GPIO        26
#define ADC_X_CHANNEL     0
#define ADC_Y_GPIO        27
#define ADC_Y_CHANNEL     1

#define BUTTON_FIRST_GPIO 2
#define BUTTON_COUNT      8

#define INVERT_X          false
#define INVERT_Y          false

#define DEADZONE          80      // in raw ADC counts (12-bit), around centre
#define ADC_SAMPLES       4       // oversampling to reduce noise
#define REPORT_PERIOD_MS  2       // ~500 Hz max

// ---------------------------------------------------------------- report
typedef struct __attribute__((packed)) {
    int16_t x;
    int16_t y;
    uint8_t buttons;
} joystick_report_t;

// ---------------------------------------------------------------- state
static uint16_t centre_x = 2048;
static uint16_t centre_y = 2048;

// ---------------------------------------------------------------- ADC
static uint16_t read_adc(uint channel) {
    adc_select_input(channel);
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += adc_read();
    }
    return (uint16_t)(sum / ADC_SAMPLES);   // 0..4095
}

// Map raw ADC to -32767..32767 around a calibrated centre with a deadzone.
static int16_t scale_axis(uint16_t raw, uint16_t centre, bool invert) {
    int32_t delta = (int32_t)raw - (int32_t)centre;

    if (delta > -DEADZONE && delta < DEADZONE) {
        delta = 0;
    } else if (delta > 0) {
        delta -= DEADZONE;
    } else {
        delta += DEADZONE;
    }

    int32_t span_pos = 4095 - centre - DEADZONE;
    int32_t span_neg = centre - DEADZONE;
    if (span_pos < 1) span_pos = 1;
    if (span_neg < 1) span_neg = 1;

    int32_t out = (delta >= 0) ? (delta * 32767) / span_pos
                               : (delta * 32767) / span_neg;

    if (out > 32767)  out = 32767;
    if (out < -32767) out = -32767;
    if (invert) out = -out;
    return (int16_t)out;
}

static void calibrate_centre(void) {
    // Assumes the stick is released at power-up.
    uint32_t sx = 0, sy = 0;
    const int n = 64;
    for (int i = 0; i < n; i++) {
        sx += read_adc(ADC_X_CHANNEL);
        sy += read_adc(ADC_Y_CHANNEL);
        sleep_ms(2);
    }
    centre_x = (uint16_t)(sx / n);
    centre_y = (uint16_t)(sy / n);
}

// ---------------------------------------------------------------- buttons
static void buttons_init(void) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        uint pin = BUTTON_FIRST_GPIO + i;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
}

static uint8_t buttons_read(void) {
    uint8_t mask = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!gpio_get(BUTTON_FIRST_GPIO + i)) {   // active low
            mask |= (1u << i);
        }
    }
    return mask;
}

// ---------------------------------------------------------------- HID task
static void hid_task(void) {
    static uint32_t last_ms = 0;
    static joystick_report_t last = {0};
    static bool first = true;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_ms < REPORT_PERIOD_MS) return;
    last_ms = now;

    if (!tud_hid_ready()) return;

    joystick_report_t r;
    r.x       = scale_axis(read_adc(ADC_X_CHANNEL), centre_x, INVERT_X);
    r.y       = scale_axis(read_adc(ADC_Y_CHANNEL), centre_y, INVERT_Y);
    r.buttons = buttons_read();

    // Only send when something changed
    if (first || memcmp(&r, &last, sizeof(r)) != 0) {
        if (tud_hid_report(0, &r, sizeof(r))) {
            last = r;
            first = false;
        }
    }
}

// ---------------------------------------------------------------- main
int main(void) {
    stdio_init_all();   // harmless; USB stdio is not enabled in CMake

    adc_init();
    adc_gpio_init(ADC_X_GPIO);
    adc_gpio_init(ADC_Y_GPIO);
    buttons_init();

    calibrate_centre();

    tusb_init();

    while (true) {
        tud_task();
        hid_task();
    }
}
