# applications 目录结构

该目录只放项目级应用代码。板级支持、CubeMX 生成代码、RT-Thread 内核代码和第三方软件包保持在目录外，避免业务逻辑和底层工程文件混在一起。

## 目录职责

- `system/`：启动流程和应用入口。
- `services/`：长期运行的应用服务，以及跨模块协调逻辑。
- `usb/`：电脑控制功能使用的 USB HID 应用层。
- `cloud/`：云端协议和阿里云 MQTT 对接。
- `network/`：ESP-01S AT 指令驱动和 Wi-Fi 状态管理。
- `sensor/`：物理传感器驱动。
- `data/`：本地应用数据模型和快照。
- `ui/oled_ui_core/`：通用 OLED UI 引擎、绘图、字体、输入和显示 HAL。
- `ui/oled_ui_app/`：产品页面、图标、文案和 UI 运行时设置。
- `ui/oled_ui_service/`：RT-Thread UI 服务线程和启动页集成。

## include 风格

跨模块 include 使用相对 `applications/` 的路径，例如：

```c
#include "services/device_service.h"
#include "usb/usb_hid.h"
#include "ui/oled_ui_service/oled_ui_core_app.h"
```

同目录内的私有头文件可以直接使用文件名 include。

## 注释风格

函数注释统一使用 Doxygen 风格，并使用中文描述函数职责、参数和返回值：

```c
/**
 * @brief 启动后台服务线程。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
```

实现文件中的关键静态函数同样需要说明用途，尤其是线程入口、状态更新、队列处理、硬件时序和协议封装函数。
