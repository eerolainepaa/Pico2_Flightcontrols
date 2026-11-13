#include "ads1115.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define ADS_BUS i2c0
#define ADS_BAUDRATE 400000
#define I2C_TIMEOUT_US 1000
#define FIRST_POLL_US 1200
#define BUSY_POLL_US 200
#define CONVERSION_TIMEOUT_US 10000
#define RETRY_US 100000

#define REG_CONVERSION 0x00
#define REG_CONFIG 0x01
#define CONFIG_OS 0x8000
#define CONFIG_PGA_4V096 0x0200
#define CONFIG_SINGLE_SHOT 0x0100
#define CONFIG_860_SPS 0x00E0
#define CONFIG_COMPARATOR_OFF 0x0003

static uint8_t current_channel;
static uint16_t requested_config;
static bool conversion_pending;
static uint64_t next_action_us;
static uint64_t conversion_started_us;

static bool write_register(uint8_t reg, uint16_t value)
{
    const uint8_t bytes[] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_write_timeout_us(ADS_BUS, ADS1115_ADDRESS, bytes, sizeof(bytes),
                                false, I2C_TIMEOUT_US) == (int)sizeof(bytes);
}

static bool read_register(uint8_t reg, uint16_t *value)
{
    uint8_t bytes[2];
    if (i2c_write_timeout_us(ADS_BUS, ADS1115_ADDRESS, &reg, 1,
                             true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(ADS_BUS, ADS1115_ADDRESS, bytes, sizeof(bytes),
                            false, I2C_TIMEOUT_US) != (int)sizeof(bytes)) return false;
    *value = (uint16_t)((uint16_t)bytes[0] << 8) | bytes[1];
    return true;
}

static void schedule_retry(void)
{
    conversion_pending = false;
    // Clear the Pico peripheral's state after a failed repeated-start/timeout.
    // A physically stuck bus still needs its wiring/device fault corrected.
    i2c_deinit(ADS_BUS);
    i2c_init(ADS_BUS, ADS_BAUDRATE);
    next_action_us = time_us_64() + RETRY_US;
}

static void start_conversion(void)
{
    // MUX 100..111 selects AIN0..AIN3 relative to GND.
    requested_config = CONFIG_OS | ((uint16_t)(4u + current_channel) << 12) |
                       CONFIG_PGA_4V096 | CONFIG_SINGLE_SHOT |
                       CONFIG_860_SPS | CONFIG_COMPARATOR_OFF;
    if (!write_register(REG_CONFIG, requested_config)) {
        schedule_retry();
        return;
    }
    conversion_pending = true;
    conversion_started_us = time_us_64();
    next_action_us = conversion_started_us + FIRST_POLL_US;
}

void ads1115_init(void)
{
    i2c_init(ADS_BUS, ADS_BAUDRATE);
    gpio_set_function(ADS1115_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(ADS1115_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(ADS1115_SDA_GPIO);
    gpio_pull_up(ADS1115_SCL_GPIO);
    // External SDA/SCL pull-ups to 3.3 V are required (often on the module).
    current_channel = 0;
    conversion_pending = false;
    next_action_us = 0;
    conversion_started_us = 0;
}

bool ads1115_poll(uint8_t *channel, int16_t *raw)
{
    if (time_us_64() < next_action_us) return false;
    if (!conversion_pending) {
        start_conversion();
        return false;
    }

    uint16_t config;
    if (!read_register(REG_CONFIG, &config) ||
        (config & 0x7FFFu) != (requested_config & 0x7FFFu)) {
        // Reject samples from an unexpected MUX/gain after a reset or bus error.
        schedule_retry();
        return false;
    }
    if (!(config & CONFIG_OS)) {
        if (time_us_64() - conversion_started_us >= CONVERSION_TIMEOUT_US)
            schedule_retry();
        else
            next_action_us = time_us_64() + BUSY_POLL_US;
        return false;
    }

    uint16_t value;
    if (!read_register(REG_CONVERSION, &value)) {
        schedule_retry();
        return false;
    }
    *channel = current_channel;
    // Decode two's complement without treating a small negative offset as full-scale.
    *raw = (int16_t)((value & 0x8000u) ? (int32_t)value - 65536 : (int32_t)value);
    current_channel = (uint8_t)((current_channel + 1u) % 4u);
    start_conversion();
    return true;
}
