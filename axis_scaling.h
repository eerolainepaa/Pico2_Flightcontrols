#ifndef AXIS_SCALING_H
#define AXIS_SCALING_H

#include <stdbool.h>
#include <stdint.h>

// Fixed electrical scaling only: no learned center, endpoint calibration,
// response curve, or deadzone. Configure those in the simulator/host software.
// ADS1115 at +/-4.096 V has 125 uV/count: 3.3 V = 26400 counts.
#define ADS1115_COUNTS_AT_3V3 26400
#define PICO_ADC_FULL_SCALE 4095

static inline int16_t scale_axis(int32_t raw, int32_t full_scale, bool invert)
{
    if (raw < 0) raw = 0; // Single-ended ADS readings may be slightly negative.
    if (raw > full_scale) raw = full_scale;
    int32_t output = (raw * 65534L / full_scale) - 32767L;
    return (int16_t)(invert ? -output : output);
}

#endif
