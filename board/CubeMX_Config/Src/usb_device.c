#include "usb_device.h"
#include "usbd_desc.h"
#include "usbd_customhid.h"
#include "usbd_custom_hid_if.h"

USBD_HandleTypeDef hUsbDeviceFS;

void MX_USB_DEVICE_Init(void)
{
    (void)USBD_Init(&hUsbDeviceFS, &HID_Desc, 0U);
    (void)USBD_RegisterClass(&hUsbDeviceFS, USBD_CUSTOM_HID_CLASS);
    (void)USBD_CUSTOM_HID_RegisterInterface(&hUsbDeviceFS, &USBD_CustomHID_fops_FS);
    (void)USBD_Start(&hUsbDeviceFS);
}
