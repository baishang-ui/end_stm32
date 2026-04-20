#include "usbd_custom_hid_if.h"

static int8_t CustomHID_Init(void);
static int8_t CustomHID_DeInit(void);
static int8_t CustomHID_OutEvent(uint8_t event_idx, uint8_t state);

__ALIGN_BEGIN static uint8_t CustomHID_ReportDesc[USBD_CUSTOM_HID_REPORT_DESC_SIZE] __ALIGN_END =
{
    0x05, 0x0C,
    0x09, 0x01,
    0xA1, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,
    0xC0
};

USBD_CUSTOM_HID_ItfTypeDef USBD_CustomHID_fops_FS =
{
    CustomHID_ReportDesc,
    CustomHID_Init,
    CustomHID_DeInit,
    CustomHID_OutEvent,
};

static int8_t CustomHID_Init(void)
{
    return 0;
}

static int8_t CustomHID_DeInit(void)
{
    return 0;
}

static int8_t CustomHID_OutEvent(uint8_t event_idx, uint8_t state)
{
    (void)event_idx;
    (void)state;
    return 0;
}
