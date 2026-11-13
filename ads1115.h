#ifndef ADS1115_H
#define ADS1115_H

#include <stdbool.h>
#include <stdint.h>

// I2C0 on GP4/GP5 is free beside the existing matrix on GP0..3 and GP6..8.
#define ADS1115_SDA_GPIO 4
#define ADS1115_SCL_GPIO 5
#define ADS1115_ADDRESS 0x48 // ADDR connected to GND

void ads1115_init(void);
// Call frequently. Returns one new, completed channel measurement when ready.
// Conversion waits are nonblocking; each I2C transaction has a bounded timeout.
bool ads1115_poll(uint8_t *channel, int16_t *raw);

#endif
