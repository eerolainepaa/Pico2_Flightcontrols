#ifndef JOYSTICK_REPORT_H
#define JOYSTICK_REPORT_H

#include <stdint.h>

// Field order must match the seven HID usages in usb_descriptor.c.
typedef struct {
    int16_t x;        // Cyclic roll: ADS1115 AIN0
    int16_t y;        // Cyclic pitch: ADS1115 AIN1
    int16_t z;        // Collective: ADS1115 AIN2
    int16_t rx;       // Left toe brake: Pico ADC1 / GP27
    int16_t ry;       // Right toe brake: Pico ADC2 / GP28
    int16_t rz;       // Rudder: Pico ADC0 / GP26
    int16_t slider;   // Throttle: ADS1115 AIN3
    uint16_t buttons; // Matrix buttons 1..12; bits 12..15 stay zero.
} joystick_report_t;

_Static_assert(sizeof(joystick_report_t) == 16, "HID report must be 16 bytes");

#endif
