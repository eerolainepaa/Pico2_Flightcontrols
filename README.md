# Pico2_Flightcontrols
DIY USB HID flight controls for helicopter and airplane simulators, built around the Raspberry Pi Pico 2 W (RP2350), written in C using the Raspberry Pi Pico SDK and TinyUSB.

The project includes a cyclic, collective with throttle, and pedals (with independent toe brakes for airplanes).

## WORK IN PROGRESS
This project is under active development. Firmware compilation and AI written software tests have passed, but physical hardware and simulator testing are still ongoing. Features, wiring, and implementation details may change.

The checked items below represent implemented firmware features, not fully validated hardware.

## Features

- [x] Cyclic X/Y axes
- [x] Collective axis
- [x] Collective throttle
- [x] Pedals and independent toe brakes
- [x] ADS1115 external ADC over I²C
- [x] Custom USB HID interface
- [x] Eight button matrix on collective
- [ ] Add buttons for cyclic
- [ ] Complete hardware and simulator testing
