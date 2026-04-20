#ifndef __USBD_CUSTOM_HID_IF_H
#define __USBD_CUSTOM_HID_IF_H

#include "usbd_customhid.h"

#define USB_CONSUMER_REPORT_SIZE                      1U
#define USB_KEYBOARD_REPORT_SIZE                      8U
#define USB_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE     29U
#define USB_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE     63U

extern USBD_CUSTOM_HID_ItfTypeDef USBD_CustomHID_fops_FS;

#endif
