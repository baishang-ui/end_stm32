# Applications Layout

This folder contains only project-level application code. Board support,
CubeMX output, RT-Thread kernel code, and third-party packages stay outside
this directory.

## Directory Roles

- `system/`: boot flow and application entry point.
- `services/`: long-running application services and cross-module coordinators.
- `usb/`: USB HID application layer used by PC control features.
- `cloud/`: cloud protocol and Aliyun MQTT integration.
- `network/`: ESP-01S AT command driver and Wi-Fi state.
- `sensor/`: physical sensor drivers.
- `data/`: local application data models and snapshots.
- `ui/oled_ui_core/`: generic OLED UI engine, graphics, fonts, input, and display HAL.
- `ui/oled_ui_app/`: product-specific pages, icons, labels, and UI runtime settings.
- `ui/oled_ui_service/`: RT-Thread UI service thread and boot-screen integration.

## Include Style

Use paths relative to `applications/` for cross-module includes, for example:

```c
#include "services/device_service.h"
#include "usb/usb_hid.h"
#include "ui/oled_ui_service/oled_ui_core_app.h"
```

Files may still include their own local headers directly when the header lives
in the same directory.
