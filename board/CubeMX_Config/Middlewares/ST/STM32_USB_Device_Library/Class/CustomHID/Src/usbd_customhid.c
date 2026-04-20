/**
  ******************************************************************************
  * @file    usbd_customhid.c
  * @brief   Composite HID class with separate consumer-control and keyboard
  *          interfaces on a single USB device.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <rtthread.h>

#include "usbd_customhid.h"
#include "usbd_ctlreq.h"

/* Private defines -----------------------------------------------------------*/
#define HID_REPORT_TYPE_INPUT                       0x01U
#define HID_REPORT_TYPE_OUTPUT                      0x02U

/* Private function prototypes -----------------------------------------------*/
static uint8_t USBD_CUSTOM_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CUSTOM_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CUSTOM_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t *USBD_CUSTOM_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetDeviceQualifierDesc(uint16_t *length);
static uint8_t USBD_CUSTOM_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CUSTOM_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CUSTOM_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);

static uint8_t USBD_CUSTOM_HID_GetInterfaceIndex(const USBD_SetupReqTypedef *req, uint8_t *if_idx);
static uint8_t *USBD_CUSTOM_HID_GetReportDesc(USBD_HandleTypeDef *pdev, uint8_t if_idx, uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetHidDesc(uint8_t if_idx, uint16_t *length);
static uint16_t USBD_CUSTOM_HID_GetReportLength(uint8_t if_idx, uint8_t report_type);
static uint8_t USBD_CUSTOM_HID_SendOnEndpoint(USBD_HandleTypeDef *pdev,
                                              CUSTOM_HID_StateTypeDef *state,
                                              uint8_t ep_addr,
                                              uint8_t *report,
                                              uint16_t len);

/* USB class callback table --------------------------------------------------*/
USBD_ClassTypeDef USBD_CUSTOM_HID =
{
  USBD_CUSTOM_HID_Init,
  USBD_CUSTOM_HID_DeInit,
  USBD_CUSTOM_HID_Setup,
  NULL,
  USBD_CUSTOM_HID_EP0_RxReady,
  USBD_CUSTOM_HID_DataIn,
  USBD_CUSTOM_HID_DataOut,
  NULL,
  NULL,
  NULL,
  USBD_CUSTOM_HID_GetHSCfgDesc,
  USBD_CUSTOM_HID_GetFSCfgDesc,
  USBD_CUSTOM_HID_GetOtherSpeedCfgDesc,
  USBD_CUSTOM_HID_GetDeviceQualifierDesc,
};

/* Consumer HID descriptor ---------------------------------------------------*/
__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_ConsumerDesc[USB_CUSTOM_HID_DESC_SIZ] __ALIGN_END =
{
  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),
};

/* Keyboard HID descriptor ---------------------------------------------------*/
__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_KeyboardDesc[USB_CUSTOM_HID_DESC_SIZ] __ALIGN_END =
{
  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),
};

/* FS configuration descriptor ----------------------------------------------*/
__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_CfgFSDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,
  USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  HIBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  USB_CUSTOM_HID_INTERFACE_COUNT,
  0x01,
  0x00,
  0x80,
  0x32,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_CONSUMER_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x00,
  0x00,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_CONSUMER_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  CUSTOM_HID_FS_BINTERVAL,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_KEYBOARD_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x01,
  0x01,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_KEYBOARD_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  CUSTOM_HID_FS_BINTERVAL,
};

/* HS and other-speed descriptors share the same layout on FS-only hardware --*/
__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_CfgHSDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,
  USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  HIBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  USB_CUSTOM_HID_INTERFACE_COUNT,
  0x01,
  0x00,
  0x80,
  0x32,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_CONSUMER_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x00,
  0x00,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_CONSUMER_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  CUSTOM_HID_HS_BINTERVAL,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_KEYBOARD_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x01,
  0x01,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_KEYBOARD_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  CUSTOM_HID_HS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_OtherSpeedCfgDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,
  USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  HIBYTE(USB_CUSTOM_HID_CONFIG_DESC_SIZ),
  USB_CUSTOM_HID_INTERFACE_COUNT,
  0x01,
  0x00,
  0x80,
  0x32,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_CONSUMER_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x00,
  0x00,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_CONSUMER_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_CONSUMER_EPIN_SIZE),
  CUSTOM_HID_FS_BINTERVAL,

  0x09,
  USB_DESC_TYPE_INTERFACE,
  CUSTOM_HID_KEYBOARD_INTERFACE,
  0x00,
  0x01,
  0x03,
  0x01,
  0x01,
  0x00,

  0x09,
  CUSTOM_HID_DESCRIPTOR_TYPE,
  0x11,
  0x01,
  0x00,
  0x01,
  0x22,
  LOBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),
  HIBYTE(USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE),

  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CUSTOM_HID_KEYBOARD_EPIN_ADDR,
  0x03,
  LOBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  HIBYTE(CUSTOM_HID_KEYBOARD_EPIN_SIZE),
  CUSTOM_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

/* Helpers -------------------------------------------------------------------*/
static uint8_t USBD_CUSTOM_HID_GetInterfaceIndex(const USBD_SetupReqTypedef *req, uint8_t *if_idx)
{
  uint8_t idx;

  if ((req == NULL) || (if_idx == NULL))
  {
    return USBD_FAIL;
  }

  idx = (uint8_t)(req->wIndex & 0x00FFU);
  if (idx >= USB_CUSTOM_HID_INTERFACE_COUNT)
  {
    return USBD_FAIL;
  }

  *if_idx = idx;
  return USBD_OK;
}

static uint8_t *USBD_CUSTOM_HID_GetReportDesc(USBD_HandleTypeDef *pdev, uint8_t if_idx, uint16_t *length)
{
  USBD_CUSTOM_HID_ItfTypeDef *fops = (USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData;

  if ((fops == NULL) || (length == NULL))
  {
    return NULL;
  }

  if (if_idx == CUSTOM_HID_CONSUMER_INTERFACE)
  {
    *length = USBD_CUSTOM_HID_CONSUMER_REPORT_DESC_SIZE;
    return fops->pConsumerReport;
  }

  *length = USBD_CUSTOM_HID_KEYBOARD_REPORT_DESC_SIZE;
  return fops->pKeyboardReport;
}

static uint8_t *USBD_CUSTOM_HID_GetHidDesc(uint8_t if_idx, uint16_t *length)
{
  if (length == NULL)
  {
    return NULL;
  }

  *length = USB_CUSTOM_HID_DESC_SIZ;
  if (if_idx == CUSTOM_HID_CONSUMER_INTERFACE)
  {
    return USBD_CUSTOM_HID_ConsumerDesc;
  }

  return USBD_CUSTOM_HID_KeyboardDesc;
}

static uint16_t USBD_CUSTOM_HID_GetReportLength(uint8_t if_idx, uint8_t report_type)
{
  if (if_idx == CUSTOM_HID_KEYBOARD_INTERFACE)
  {
    if (report_type == HID_REPORT_TYPE_OUTPUT)
    {
      return 1U;
    }
    return CUSTOM_HID_KEYBOARD_EPIN_SIZE;
  }

  return CUSTOM_HID_CONSUMER_EPIN_SIZE;
}

static uint8_t USBD_CUSTOM_HID_SendOnEndpoint(USBD_HandleTypeDef *pdev,
                                              CUSTOM_HID_StateTypeDef *state,
                                              uint8_t ep_addr,
                                              uint8_t *report,
                                              uint16_t len)
{
  if ((pdev == NULL) || (state == NULL) || (report == NULL))
  {
    return USBD_FAIL;
  }

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    return USBD_BUSY;
  }

  if (*state != CUSTOM_HID_IDLE)
  {
    return USBD_BUSY;
  }

  *state = CUSTOM_HID_BUSY;
  USBD_LL_Transmit(pdev, ep_addr, report, len);
  return USBD_OK;
}

/* Class callbacks -----------------------------------------------------------*/
static uint8_t USBD_CUSTOM_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid;

  (void)cfgidx;

  USBD_LL_OpenEP(pdev, CUSTOM_HID_CONSUMER_EPIN_ADDR, USBD_EP_TYPE_INTR, CUSTOM_HID_CONSUMER_EPIN_SIZE);
  pdev->ep_in[CUSTOM_HID_CONSUMER_EPIN_ADDR & 0x0FU].is_used = 1U;

  USBD_LL_OpenEP(pdev, CUSTOM_HID_KEYBOARD_EPIN_ADDR, USBD_EP_TYPE_INTR, CUSTOM_HID_KEYBOARD_EPIN_SIZE);
  pdev->ep_in[CUSTOM_HID_KEYBOARD_EPIN_ADDR & 0x0FU].is_used = 1U;

  pdev->pClassData = USBD_malloc(sizeof(USBD_CUSTOM_HID_HandleTypeDef));
  if (pdev->pClassData == NULL)
  {
    return USBD_FAIL;
  }

  hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;
  memset(hhid, 0, sizeof(USBD_CUSTOM_HID_HandleTypeDef));
  hhid->ConsumerState = CUSTOM_HID_IDLE;
  hhid->KeyboardState = CUSTOM_HID_IDLE;

  rt_kprintf("[USB HID] composite init cfg=%u\n", (unsigned int)cfgidx);
  ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData)->Init();
  return USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;

  USBD_LL_CloseEP(pdev, CUSTOM_HID_CONSUMER_EPIN_ADDR);
  pdev->ep_in[CUSTOM_HID_CONSUMER_EPIN_ADDR & 0x0FU].is_used = 0U;

  USBD_LL_CloseEP(pdev, CUSTOM_HID_KEYBOARD_EPIN_ADDR);
  pdev->ep_in[CUSTOM_HID_KEYBOARD_EPIN_ADDR & 0x0FU].is_used = 0U;

  if (pdev->pClassData != NULL)
  {
    ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData)->DeInit();
    USBD_free(pdev->pClassData);
    pdev->pClassData = NULL;
  }

  return USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;
  uint8_t *pbuf = NULL;
  uint16_t len = 0U;
  uint16_t status_info = 0U;
  static uint8_t report_reply[USBD_CUSTOMHID_OUTREPORT_BUF_SIZE];
  static uint8_t zero_value = 0U;
  uint8_t if_idx = 0U;
  uint8_t ret = USBD_OK;

  rt_kprintf("[USB HID] setup bm=%02X req=%02X val=%04X idx=%04X len=%u state=%u\n",
             (unsigned int)req->bmRequest,
             (unsigned int)req->bRequest,
             (unsigned int)req->wValue,
             (unsigned int)req->wIndex,
             (unsigned int)req->wLength,
             (unsigned int)pdev->dev_state);

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      if (USBD_CUSTOM_HID_GetInterfaceIndex(req, &if_idx) != USBD_OK)
      {
        USBD_CtlError(pdev, req);
        return USBD_FAIL;
      }

      switch (req->bRequest)
      {
        case CUSTOM_HID_REQ_SET_PROTOCOL:
          if (hhid != NULL)
          {
            hhid->Protocol[if_idx] = (uint8_t)(req->wValue);
          }
          break;

        case CUSTOM_HID_REQ_GET_PROTOCOL:
          if (hhid != NULL)
          {
            USBD_CtlSendData(pdev, (uint8_t *)(void *)&hhid->Protocol[if_idx], 1U);
          }
          else
          {
            USBD_CtlSendData(pdev, &zero_value, 1U);
          }
          break;

        case CUSTOM_HID_REQ_SET_IDLE:
          if (hhid != NULL)
          {
            hhid->IdleState[if_idx] = (uint8_t)(req->wValue >> 8);
          }
          break;

        case CUSTOM_HID_REQ_GET_IDLE:
          if (hhid != NULL)
          {
            USBD_CtlSendData(pdev, (uint8_t *)(void *)&hhid->IdleState[if_idx], 1U);
          }
          else
          {
            USBD_CtlSendData(pdev, &zero_value, 1U);
          }
          break;

        case CUSTOM_HID_REQ_SET_REPORT:
          if (hhid == NULL)
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          else
          {
            hhid->IsReportAvailable = 1U;
            hhid->Report_if = if_idx;
            USBD_CtlPrepareRx(pdev, hhid->Report_buf, MIN(req->wLength, USBD_CUSTOMHID_OUTREPORT_BUF_SIZE));
          }
          break;

        case CUSTOM_HID_REQ_GET_REPORT:
          memset(report_reply, 0, sizeof(report_reply));
          len = MIN(req->wLength,
                    USBD_CUSTOM_HID_GetReportLength(if_idx, (uint8_t)(req->wValue >> 8)));
          USBD_CtlSendData(pdev, report_reply, len);
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            USBD_CtlSendData(pdev, (uint8_t *)(void *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          if (USBD_CUSTOM_HID_GetInterfaceIndex(req, &if_idx) != USBD_OK)
          {
            USBD_CtlError(pdev, req);
            return USBD_FAIL;
          }

          if ((req->wValue >> 8) == CUSTOM_HID_REPORT_DESC)
          {
            pbuf = USBD_CUSTOM_HID_GetReportDesc(pdev, if_idx, &len);
            len = MIN(len, req->wLength);
          }
          else if ((req->wValue >> 8) == CUSTOM_HID_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_CUSTOM_HID_GetHidDesc(if_idx, &len);
            len = MIN(len, req->wLength);
          }

          if (pbuf == NULL)
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          else
          {
            USBD_CtlSendData(pdev, pbuf, len);
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if ((pdev->dev_state == USBD_STATE_CONFIGURED) &&
              (USBD_CUSTOM_HID_GetInterfaceIndex(req, &if_idx) == USBD_OK) &&
              (hhid != NULL))
          {
            USBD_CtlSendData(pdev, (uint8_t *)(void *)&hhid->AltSetting[if_idx], 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if ((pdev->dev_state == USBD_STATE_CONFIGURED) &&
              (USBD_CUSTOM_HID_GetInterfaceIndex(req, &if_idx) == USBD_OK) &&
              (hhid != NULL))
          {
            hhid->AltSetting[if_idx] = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return ret;
}

uint8_t USBD_CUSTOM_HID_SendConsumerReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;

  if (hhid == NULL)
  {
    return USBD_FAIL;
  }

  return USBD_CUSTOM_HID_SendOnEndpoint(pdev,
                                        &hhid->ConsumerState,
                                        CUSTOM_HID_CONSUMER_EPIN_ADDR,
                                        report,
                                        len);
}

uint8_t USBD_CUSTOM_HID_SendKeyboardReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;

  if (hhid == NULL)
  {
    return USBD_FAIL;
  }

  return USBD_CUSTOM_HID_SendOnEndpoint(pdev,
                                        &hhid->KeyboardState,
                                        CUSTOM_HID_KEYBOARD_EPIN_ADDR,
                                        report,
                                        len);
}

static uint8_t *USBD_CUSTOM_HID_GetFSCfgDesc(uint16_t *length)
{
  *length = sizeof(USBD_CUSTOM_HID_CfgFSDesc);
  return USBD_CUSTOM_HID_CfgFSDesc;
}

static uint8_t *USBD_CUSTOM_HID_GetHSCfgDesc(uint16_t *length)
{
  *length = sizeof(USBD_CUSTOM_HID_CfgHSDesc);
  return USBD_CUSTOM_HID_CfgHSDesc;
}

static uint8_t *USBD_CUSTOM_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = sizeof(USBD_CUSTOM_HID_OtherSpeedCfgDesc);
  return USBD_CUSTOM_HID_OtherSpeedCfgDesc;
}

static uint8_t USBD_CUSTOM_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;

  if (hhid == NULL)
  {
    return USBD_FAIL;
  }

  if (epnum == (CUSTOM_HID_CONSUMER_EPIN_ADDR & 0x7FU))
  {
    hhid->ConsumerState = CUSTOM_HID_IDLE;
  }
  else if (epnum == (CUSTOM_HID_KEYBOARD_EPIN_ADDR & 0x7FU))
  {
    hhid->KeyboardState = CUSTOM_HID_IDLE;
  }

  return USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  (void)pdev;
  (void)epnum;
  return USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData;

  if ((hhid != NULL) && (hhid->IsReportAvailable == 1U))
  {
    ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData)->OutEvent(hhid->Report_if,
                                                              hhid->Report_buf[0],
                                                              (USBD_CUSTOMHID_OUTREPORT_BUF_SIZE > 1U) ? hhid->Report_buf[1] : 0U);
    hhid->IsReportAvailable = 0U;
  }

  return USBD_OK;
}

static uint8_t *USBD_CUSTOM_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = sizeof(USBD_CUSTOM_HID_DeviceQualifierDesc);
  return USBD_CUSTOM_HID_DeviceQualifierDesc;
}

uint8_t USBD_CUSTOM_HID_RegisterInterface(USBD_HandleTypeDef *pdev,
                                          USBD_CUSTOM_HID_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return USBD_FAIL;
  }

  pdev->pUserData = fops;
  return USBD_OK;
}
