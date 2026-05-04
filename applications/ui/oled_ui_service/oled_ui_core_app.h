#ifndef __OLED_UI_CORE_APP_H__
#define __OLED_UI_CORE_APP_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 OLED UI 主线程。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int oled_ui_core_start(void);

/**
 * @brief 等待 OLED UI 主线程完成初始化。
 * @param timeout_ms 超时时间，单位毫秒；小于 0 表示一直等待。
 * @return UI 就绪返回 RT_EOK，超时返回 -RT_ETIMEOUT。
 */
rt_err_t oled_ui_core_wait_ready(rt_int32_t timeout_ms);

/**
 * @brief 初始化启动动画页面和 OLED 基础显示状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_boot_init(void);

/**
 * @brief 设置启动动画步骤总数和步骤名称。
 * @param total 启动步骤总数。
 * @param labels 步骤名称数组，可以为 RT_NULL。
 * @return 无。
 */
void oled_ui_boot_set_plan(rt_uint32_t total, const char * const *labels);

/**
 * @brief 推进启动动画到指定步骤并显示执行结果。
 * @param step 当前步骤索引，从 0 开始。
 * @param total 启动步骤总数。
 * @param label 当前步骤名称，可以为 RT_NULL。
 * @param result 当前步骤执行结果。
 * @return 无。
 */
void oled_ui_boot_step(rt_uint32_t step, rt_uint32_t total, const char *label, rt_err_t result);

/**
 * @brief 显示启动完成动画。
 * @param 无。
 * @return 无。
 */
void oled_ui_boot_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_UI_CORE_APP_H__ */
