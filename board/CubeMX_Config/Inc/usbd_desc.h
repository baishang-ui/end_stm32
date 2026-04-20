#ifndef __USBD_DESC_H
#define __USBD_DESC_H

#include "usbd_def.h"
#include "stm32f4xx.h"

#define DEVICE_ID1                            (UID_BASE)
#define DEVICE_ID2                            (UID_BASE + 4U)
#define DEVICE_ID3                            (UID_BASE + 8U)
#define USB_SIZ_STRING_SERIAL                 0x1AU

extern USBD_DescriptorsTypeDef HID_Desc;

#endif
