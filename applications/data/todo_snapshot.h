#ifndef __TODO_SNAPSHOT_H__
#define __TODO_SNAPSHOT_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TODO_SNAPSHOT_MAX_ITEMS   8
#define TODO_ID_MAX_LEN           32
#define TODO_TITLE_MAX_LEN        96
#define TODO_REASON_MAX_LEN       32
#define TODO_SOURCE_MAX_LEN       24

typedef struct
{
    char id[TODO_ID_MAX_LEN];
    char title[TODO_TITLE_MAX_LEN];
    char source[TODO_SOURCE_MAX_LEN];
    rt_bool_t completed;
    rt_uint64_t updated_at;
    rt_uint64_t created_at;
} todo_item_t;

typedef struct
{
    char type[24];
    char reason[TODO_REASON_MAX_LEN];
    rt_uint64_t timestamp;
    rt_uint32_t version;
    rt_uint32_t count;
    rt_uint32_t completed_count;
    rt_tick_t last_update_tick;
    todo_item_t items[TODO_SNAPSHOT_MAX_ITEMS];
} todo_snapshot_t;

typedef struct
{
    todo_item_t item;
    rt_uint64_t timestamp;
    rt_uint32_t version;
    rt_uint32_t count;
    rt_uint32_t completed_count;
} todo_snapshot_change_t;

/**
 * @brief 将内存中的待办快照重置为空的初始化状态。
 * @param 无。
 * @return 无。
 */
void todo_snapshot_init(void);

/**
 * @brief 解析后端 JSON 数据并更新本地待办快照。
 * @param json 后端返回的以空字符结尾的 JSON 字符串。
 * @return 解析成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t todo_snapshot_update_from_json(const char *json);

/**
 * @brief 复制当前待办快照，供 UI 或服务层读取。
 * @param snapshot 输出参数，用于接收一致性的快照副本。
 * @return 无。
 */
void todo_snapshot_get(todo_snapshot_t *snapshot);

/**
 * @brief 切换指定待办项的完成状态，并生成最小上报数据。
 * @param index 当前快照中的待办项索引，从 0 开始。
 * @param change 可选输出参数，用于接收被修改项的上报数据。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t todo_snapshot_toggle_completed(rt_uint32_t index, todo_snapshot_change_t *change);

#ifdef __cplusplus
}
#endif

#endif /* __TODO_SNAPSHOT_H__ */

