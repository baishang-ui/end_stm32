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

void todo_snapshot_init(void);
rt_err_t todo_snapshot_update_from_json(const char *json);
void todo_snapshot_get(todo_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __TODO_SNAPSHOT_H__ */
