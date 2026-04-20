#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#include "stm32f4xx_hal.h"
#include <rtthread.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES               2U
#define USBD_MAX_NUM_CONFIGURATION            1U
#define USBD_MAX_STR_DESC_SIZ                 0x100U
#define USBD_SUPPORT_USER_STRING_DESC         0U
#define USBD_SELF_POWERED                     0U
#define USBD_DEBUG_LEVEL                      0U

#define USBD_CUSTOMHID_OUTREPORT_BUF_SIZE     0x08U
#define USBD_CUSTOM_HID_REPORT_DESC_SIZE      0x60U

#define USBD_malloc                           rt_malloc
#define USBD_free                             rt_free
#define USBD_memset                           rt_memset
#define USBD_memcpy                           rt_memcpy

#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)                      rt_kprintf(__VA_ARGS__)
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)                      rt_kprintf(__VA_ARGS__)
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)                      rt_kprintf(__VA_ARGS__)
#else
#define USBD_DbgLog(...)
#endif

#endif
