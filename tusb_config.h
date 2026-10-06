#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_OS             OPT_OS_NONE
#define CFG_TUSB_DEBUG          0

// Device mode on root hub port 0
#define CFG_TUD_ENABLED         1
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)

#define CFG_TUD_ENDPOINT0_SIZE  64

// Only one class: HID (the joystick)
#define CFG_TUD_HID             1
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_HID_EP_BUFSIZE  16

#ifdef __cplusplus
}
#endif

#endif
