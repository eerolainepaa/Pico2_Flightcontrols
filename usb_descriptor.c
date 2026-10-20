#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"


// ================================================================
// DEVICE DESCRIPTOR
// ================================================================

static const tusb_desc_device_t desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),

    .bDescriptorType    = TUSB_DESC_DEVICE,

    .bcdUSB             = 0x0200,

    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    /*
     * TinyUSB example VID/PID.
     *
     * Fine for personal testing.
     */
    .idVendor           = 0xCafe,
    .idProduct          = 0x4010,

    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01,
};


uint8_t const *tud_descriptor_device_cb(void)
{
    return
        (uint8_t const *)&desc_device;
}


// ================================================================
// HID REPORT DESCRIPTOR
// ================================================================

/*
 * Must match joystick_report_t:
 *
 * int16_t  x        2 bytes
 * int16_t  y        2 bytes
 * int16_t  z        2 bytes
 * uint16_t buttons  2 bytes
 *
 * Total = 8 bytes
 *
 *
 * Current use:
 *
 * X = unused / centre
 * Y = unused / centre
 * Z = collective
 *
 * Buttons 1-11 = button matrix
 *
 * Buttons 12-16 reserved for expansion.
 */

static const uint8_t desc_hid_report[] =
{
    HID_USAGE_PAGE(
        HID_USAGE_PAGE_DESKTOP
    ),

    HID_USAGE(
        HID_USAGE_DESKTOP_JOYSTICK
    ),

    HID_COLLECTION(
        HID_COLLECTION_APPLICATION
    ),


        // ========================================================
        // X, Y, Z
        // ========================================================

        HID_USAGE_PAGE(
            HID_USAGE_PAGE_DESKTOP
        ),

        HID_USAGE(
            HID_USAGE_DESKTOP_X
        ),

        HID_USAGE(
            HID_USAGE_DESKTOP_Y
        ),

        HID_USAGE(
            HID_USAGE_DESKTOP_Z
        ),


        HID_LOGICAL_MIN_N(
            -32767,
            2
        ),

        HID_LOGICAL_MAX_N(
            32767,
            2
        ),


        HID_REPORT_SIZE(
            16
        ),

        HID_REPORT_COUNT(
            3
        ),


        HID_INPUT(
            HID_DATA |
            HID_VARIABLE |
            HID_ABSOLUTE
        ),


        // ========================================================
        // BUTTONS
        // ========================================================

        HID_USAGE_PAGE(
            HID_USAGE_PAGE_BUTTON
        ),

        HID_USAGE_MIN(
            1
        ),

        HID_USAGE_MAX(
            16
        ),


        HID_LOGICAL_MIN(
            0
        ),

        HID_LOGICAL_MAX(
            1
        ),


        HID_REPORT_SIZE(
            1
        ),

        HID_REPORT_COUNT(
            16
        ),


        HID_INPUT(
            HID_DATA |
            HID_VARIABLE |
            HID_ABSOLUTE
        ),


    HID_COLLECTION_END
};


uint8_t const *
tud_hid_descriptor_report_cb(
    uint8_t instance
)
{
    (void)instance;

    return desc_hid_report;
}


// ================================================================
// CONFIGURATION DESCRIPTOR
// ================================================================

enum
{
    ITF_NUM_HID,

    ITF_NUM_TOTAL
};


#define EPNUM_HID 0x81


#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)


static const uint8_t desc_configuration[] =
{
    /*
     * Configuration:
     *
     * number
     * interface count
     * string
     * total length
     * attributes
     * power
     */

    TUD_CONFIG_DESCRIPTOR(
        1,
        ITF_NUM_TOTAL,
        0,
        CONFIG_TOTAL_LEN,
        0x00,
        100
    ),


    /*
     * HID interface
     */

    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(desc_hid_report),
        EPNUM_HID,
        CFG_TUD_HID_EP_BUFSIZE,
        1
    ),
};


uint8_t const *
tud_descriptor_configuration_cb(
    uint8_t index
)
{
    (void)index;

    return desc_configuration;
}


// ================================================================
// STRING DESCRIPTORS
// ================================================================

enum
{
    STRID_LANGID = 0,

    STRID_MANUFACTURER,

    STRID_PRODUCT,

    STRID_SERIAL
};


static const char *string_desc_arr[] =
{
    /*
     * English 0x0409
     */
    (const char[])
    {
        0x09,
        0x04
    },


    /*
     * Manufacturer
     */
    "DIY",


    /*
     * Product
     */
    "Pico 2 W Flight Controller",


    /*
     * Filled dynamically
     */
    NULL
};


static uint16_t desc_str[32];


uint16_t const *
tud_descriptor_string_cb(
    uint8_t index,
    uint16_t langid
)
{
    (void)langid;


    uint8_t chr_count;


    if (index == STRID_LANGID)
    {
        memcpy(
            &desc_str[1],
            string_desc_arr[0],
            2
        );

        chr_count = 1;
    }
    else
    {
        const char *str;


        char serial[
            2 *
            PICO_UNIQUE_BOARD_ID_SIZE_BYTES
            + 1
        ];


        if (index == STRID_SERIAL)
        {
            pico_get_unique_board_id_string(
                serial,
                sizeof(serial)
            );

            str = serial;
        }
        else if (
            index <
            sizeof(string_desc_arr) /
            sizeof(string_desc_arr[0])
        )
        {
            str =
                string_desc_arr[index];
        }
        else
        {
            return NULL;
        }


        chr_count =
            (uint8_t)strlen(str);


        if (chr_count > 31)
        {
            chr_count = 31;
        }


        for (
            uint8_t i = 0;
            i < chr_count;
            i++
        )
        {
            desc_str[1 + i] =
                str[i];
        }
    }


    desc_str[0] =
        (uint16_t)(
            (TUSB_DESC_STRING << 8) |
            (2 * chr_count + 2)
        );


    return desc_str;
}


// ================================================================
// HID CALLBACKS
// ================================================================

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t reqlen
)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}


void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const *buffer,
    uint16_t bufsize
)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}