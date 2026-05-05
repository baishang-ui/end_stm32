#include "todo_snapshot.h"

#include <string.h>
#include <stdlib.h>

#define DBG_TAG    "todo.snap"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

static todo_snapshot_t g_snapshot;
static struct rt_mutex g_snapshot_lock;
static rt_bool_t g_snapshot_ready = RT_FALSE;

/**
 * @brief 将 RT-Thread tick 转换为毫秒。
 * @param ticks 需要转换的系统 tick 数。
 * @return 对应的毫秒数。
 */
static rt_uint64_t todo_ticks_to_ms(rt_tick_t ticks)
{
    return ((rt_uint64_t)ticks * 1000ULL) / (rt_uint64_t)RT_TICK_PER_SECOND;
}

/**
 * @brief 根据快照时间和当前 tick 生成待办变更时间戳。
 * @param item 当前被修改的待办项。
 * @param now_tick 当前系统 tick。
 * @return 单调递增的变更时间戳，单位毫秒。
 */
static rt_uint64_t todo_resolve_change_timestamp(const todo_item_t *item, rt_tick_t now_tick)
{
    rt_uint64_t candidate = 0ULL;
    rt_tick_t elapsed_ticks = 0U;

    if (g_snapshot.last_update_tick != 0U)
    {
        elapsed_ticks = now_tick - g_snapshot.last_update_tick;
    }

    if (g_snapshot.timestamp != 0ULL)
    {
        candidate = g_snapshot.timestamp + todo_ticks_to_ms(elapsed_ticks);
    }
    else if ((item != RT_NULL) && (item->updated_at != 0ULL))
    {
        candidate = item->updated_at + todo_ticks_to_ms(elapsed_ticks);
    }
    else
    {
        candidate = todo_ticks_to_ms(now_tick);
    }

    if ((item != RT_NULL) && (candidate <= item->updated_at))
    {
        candidate = item->updated_at + 1ULL;
    }

    if (candidate == 0ULL)
    {
        candidate = 1ULL;
    }

    return candidate;
}

/**
 * @brief 跳过 JSON 字符串中的空白字符。
 * @param p 当前解析指针。
 * @return 指向下一个非空白字符的指针。
 */
static const char *todo_skip_ws(const char *p)
{
    while ((p != RT_NULL) &&
           ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
    {
        p++;
    }

    return p;
}

/**
 * @brief 解析 JSON 字符串值。
 * @param cursor 输入输出参数，传入当前位置，返回解析后的新位置。
 * @param out 输出字符串缓冲区。
 * @param out_len out 缓冲区长度。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_json_string(const char **cursor, char *out, rt_size_t out_len)
{
    const char *p;
    rt_size_t used = 0;

    if ((cursor == RT_NULL) || (*cursor == RT_NULL) || (out == RT_NULL) || (out_len == 0U))
    {
        return -RT_EINVAL;
    }

    p = todo_skip_ws(*cursor);
    if (*p != '"')
    {
        return -RT_ERROR;
    }
    p++;

    while ((*p != '\0') && (*p != '"'))
    {
        char ch = *p++;

        if (ch == '\\')
        {
            if (*p == '\0')
            {
                return -RT_ERROR;
            }

            ch = *p++;
            switch (ch)
            {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'u':
                /* 轻量解析器不展开 \uXXXX，跳过编码并用 '?' 占位。 */
                if ((p[0] == '\0') || (p[1] == '\0') || (p[2] == '\0') || (p[3] == '\0'))
                {
                    return -RT_ERROR;
                }
                else
                {
                    p += 4;
                }
                ch = '?';
                break;
            default:
                break;
            }
        }

        if ((used + 1U) < out_len)
        {
            out[used++] = ch;
        }
    }

    if (*p != '"')
    {
        return -RT_ERROR;
    }

    out[used] = '\0';
    *cursor = p + 1;
    return RT_EOK;
}

/**
 * @brief 解析 JSON 布尔值。
 * @param cursor 输入输出参数，传入当前位置，返回解析后的新位置。
 * @param value 输出参数，用于接收布尔值。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_json_bool(const char **cursor, rt_bool_t *value)
{
    const char *p;

    if ((cursor == RT_NULL) || (*cursor == RT_NULL) || (value == RT_NULL))
    {
        return -RT_EINVAL;
    }

    p = todo_skip_ws(*cursor);
    if (strncmp(p, "true", 4) == 0)
    {
        *value = RT_TRUE;
        *cursor = p + 4;
        return RT_EOK;
    }
    if (strncmp(p, "false", 5) == 0)
    {
        *value = RT_FALSE;
        *cursor = p + 5;
        return RT_EOK;
    }

    return -RT_ERROR;
}

/**
 * @brief 解析无符号 64 位 JSON 整数。
 * @param cursor 输入输出参数，传入当前位置，返回解析后的新位置。
 * @param value 输出参数，用于接收整数值。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_json_u64(const char **cursor, rt_uint64_t *value)
{
    const char *p;
    rt_uint64_t num = 0;

    if ((cursor == RT_NULL) || (*cursor == RT_NULL) || (value == RT_NULL))
    {
        return -RT_EINVAL;
    }

    p = todo_skip_ws(*cursor);
    if ((*p < '0') || (*p > '9'))
    {
        return -RT_ERROR;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        num = num * 10ULL + (rt_uint64_t)(*p - '0');
        p++;
    }

    *value = num;
    *cursor = p;
    return RT_EOK;
}

static rt_err_t todo_skip_json_value(const char **cursor);

/**
 * @brief 跳过一个完整 JSON 对象。
 * @param cursor 输入输出参数，传入对象起始位置，返回对象结束后的新位置。
 * @return 跳过成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_skip_json_object(const char **cursor)
{
    const char *p;

    p = todo_skip_ws(*cursor);
    if (*p != '{')
    {
        return -RT_ERROR;
    }
    p++;

    while (1)
    {
        char key[32];
        rt_err_t ret;

        p = todo_skip_ws(p);
        if (*p == '}')
        {
            *cursor = p + 1;
            return RT_EOK;
        }

        ret = todo_parse_json_string(&p, key, sizeof(key));
        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p != ':')
        {
            return -RT_ERROR;
        }
        p++;

        ret = todo_skip_json_value(&p);
        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == '}')
        {
            *cursor = p + 1;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 跳过一个完整 JSON 数组。
 * @param cursor 输入输出参数，传入数组起始位置，返回数组结束后的新位置。
 * @return 跳过成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_skip_json_array(const char **cursor)
{
    const char *p;

    p = todo_skip_ws(*cursor);
    if (*p != '[')
    {
        return -RT_ERROR;
    }
    p++;

    while (1)
    {
        rt_err_t ret;

        p = todo_skip_ws(p);
        if (*p == ']')
        {
            *cursor = p + 1;
            return RT_EOK;
        }

        ret = todo_skip_json_value(&p);
        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == ']')
        {
            *cursor = p + 1;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 跳过任意 JSON 值。
 * @param cursor 输入输出参数，传入值起始位置，返回值结束后的新位置。
 * @return 跳过成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_skip_json_value(const char **cursor)
{
    const char *p;
    rt_err_t ret;

    p = todo_skip_ws(*cursor);
    switch (*p)
    {
    case '"':
    {
        char dummy[4];
        ret = todo_parse_json_string(&p, dummy, sizeof(dummy));
        if (ret == RT_EOK)
        {
            *cursor = p;
        }
        return ret;
    }
    case '{':
        *cursor = p;
        return todo_skip_json_object(cursor);
    case '[':
        *cursor = p;
        return todo_skip_json_array(cursor);
    default:
        if (((*p >= '0') && (*p <= '9')) || (*p == '-'))
        {
            p++;
            while (((*p >= '0') && (*p <= '9')) || (*p == '.') ||
                   (*p == 'e') || (*p == 'E') || (*p == '+') || (*p == '-'))
            {
                p++;
            }
            *cursor = p;
            return RT_EOK;
        }
        if (strncmp(p, "true", 4) == 0)
        {
            *cursor = p + 4;
            return RT_EOK;
        }
        if (strncmp(p, "false", 5) == 0)
        {
            *cursor = p + 5;
            return RT_EOK;
        }
        if (strncmp(p, "null", 4) == 0)
        {
            *cursor = p + 4;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 解析单个待办项对象。
 * @param cursor 输入输出参数，传入对象起始位置，返回对象结束后的新位置。
 * @param item 输出参数，用于接收待办项数据。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_item(const char **cursor, todo_item_t *item)
{
    const char *p;

    if ((cursor == RT_NULL) || (*cursor == RT_NULL) || (item == RT_NULL))
    {
        return -RT_EINVAL;
    }

    memset(item, 0, sizeof(*item));
    p = todo_skip_ws(*cursor);
    if (*p != '{')
    {
        return -RT_ERROR;
    }
    p++;

    while (1)
    {
        char key[24];
        rt_err_t ret;

        p = todo_skip_ws(p);
        if (*p == '}')
        {
            *cursor = p + 1;
            return RT_EOK;
        }

        ret = todo_parse_json_string(&p, key, sizeof(key));
        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p != ':')
        {
            return -RT_ERROR;
        }
        p++;

        if (rt_strcmp(key, "id") == 0)
        {
            ret = todo_parse_json_string(&p, item->id, sizeof(item->id));
        }
        else if (rt_strcmp(key, "title") == 0)
        {
            ret = todo_parse_json_string(&p, item->title, sizeof(item->title));
        }
        else if (rt_strcmp(key, "source") == 0)
        {
            ret = todo_parse_json_string(&p, item->source, sizeof(item->source));
        }
        else if (rt_strcmp(key, "completed") == 0)
        {
            ret = todo_parse_json_bool(&p, &item->completed);
        }
        else if (rt_strcmp(key, "updatedAt") == 0)
        {
            ret = todo_parse_json_u64(&p, &item->updated_at);
        }
        else if (rt_strcmp(key, "createdAt") == 0)
        {
            ret = todo_parse_json_u64(&p, &item->created_at);
        }
        else
        {
            ret = todo_skip_json_value(&p);
        }

        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == '}')
        {
            *cursor = p + 1;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 解析待办项数组并写入快照对象。
 * @param cursor 输入输出参数，传入数组起始位置，返回数组结束后的新位置。
 * @param snapshot 输出快照对象。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_items(const char **cursor, todo_snapshot_t *snapshot)
{
    const char *p;

    if ((cursor == RT_NULL) || (*cursor == RT_NULL) || (snapshot == RT_NULL))
    {
        return -RT_EINVAL;
    }

    p = todo_skip_ws(*cursor);
    if (*p != '[')
    {
        return -RT_ERROR;
    }
    p++;

    while (1)
    {
        todo_item_t item;
        rt_err_t ret;

        p = todo_skip_ws(p);
        if (*p == ']')
        {
            *cursor = p + 1;
            return RT_EOK;
        }

        ret = todo_parse_item(&p, &item);
        if (ret != RT_EOK)
        {
            return ret;
        }

        if (snapshot->count < TODO_SNAPSHOT_MAX_ITEMS)
        {
            snapshot->items[snapshot->count] = item;
            if (item.completed)
            {
                snapshot->completed_count++;
            }
            snapshot->count++;
        }

        p = todo_skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == ']')
        {
            *cursor = p + 1;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 解析完整待办快照 JSON。
 * @param json 以空字符结尾的 JSON 字符串。
 * @param snapshot 输出快照对象。
 * @return 解析成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t todo_parse_snapshot(const char *json, todo_snapshot_t *snapshot)
{
    const char *p;

    if ((json == RT_NULL) || (snapshot == RT_NULL))
    {
        return -RT_EINVAL;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    p = todo_skip_ws(json);
    if (*p != '{')
    {
        return -RT_ERROR;
    }
    p++;

    while (1)
    {
        char key[24];
        rt_err_t ret;

        p = todo_skip_ws(p);
        if (*p == '}')
        {
            return RT_EOK;
        }

        ret = todo_parse_json_string(&p, key, sizeof(key));
        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p != ':')
        {
            return -RT_ERROR;
        }
        p++;

        if (rt_strcmp(key, "type") == 0)
        {
            ret = todo_parse_json_string(&p, snapshot->type, sizeof(snapshot->type));
        }
        else if (rt_strcmp(key, "reason") == 0)
        {
            ret = todo_parse_json_string(&p, snapshot->reason, sizeof(snapshot->reason));
        }
        else if (rt_strcmp(key, "timestamp") == 0)
        {
            ret = todo_parse_json_u64(&p, &snapshot->timestamp);
        }
        else if (rt_strcmp(key, "todos") == 0)
        {
            ret = todo_parse_items(&p, snapshot);
        }
        else
        {
            ret = todo_skip_json_value(&p);
        }

        if (ret != RT_EOK)
        {
            return ret;
        }

        p = todo_skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == '}')
        {
            return RT_EOK;
        }
        return -RT_ERROR;
    }
}

/**
 * @brief 初始化或重置待办快照缓存。
 * @param 无。
 * @return 无。
 */
void todo_snapshot_init(void)
{
    if (!g_snapshot_ready)
    {
        rt_mutex_init(&g_snapshot_lock, "todo", RT_IPC_FLAG_PRIO);
        g_snapshot_ready = RT_TRUE;
    }

    rt_mutex_take(&g_snapshot_lock, RT_WAITING_FOREVER);
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    rt_mutex_release(&g_snapshot_lock);
}

/**
 * @brief 使用后端 JSON 更新本地待办快照。
 * @param json 后端返回的待办快照 JSON。
 * @return 更新成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t todo_snapshot_update_from_json(const char *json)
{
    todo_snapshot_t parsed;
    rt_err_t ret;

    if (!g_snapshot_ready)
    {
        todo_snapshot_init();
    }

    ret = todo_parse_snapshot(json, &parsed);
    if (ret != RT_EOK)
    {
        LOG_E("todo json parse failed: %d, raw=%.160s", ret, json);
        return ret;
    }

    if (rt_strcmp(parsed.type, "todo.snapshot") != 0)
    {
        LOG_E("todo type mismatch: %s", parsed.type);
        return -RT_ERROR;
    }

    rt_mutex_take(&g_snapshot_lock, RT_WAITING_FOREVER);
    parsed.version = g_snapshot.version + 1U;
    parsed.last_update_tick = rt_tick_get();
    g_snapshot = parsed;
    rt_mutex_release(&g_snapshot_lock);

    LOG_I("todo snapshot updated: count=%lu completed=%lu",
          (unsigned long)parsed.count,
          (unsigned long)parsed.completed_count);
    return RT_EOK;
}

/**
 * @brief 获取当前待办快照副本。
 * @param snapshot 输出参数，用于接收快照副本。
 * @return 无。
 */
void todo_snapshot_get(todo_snapshot_t *snapshot)
{
    if ((snapshot == RT_NULL) || !g_snapshot_ready)
    {
        return;
    }

    rt_mutex_take(&g_snapshot_lock, RT_WAITING_FOREVER);
    *snapshot = g_snapshot;
    rt_mutex_release(&g_snapshot_lock);
}

/**
 * @brief 切换指定待办项完成状态，并生成最小上报数据。
 * @param index 待办项索引。
 * @param change 输出参数，用于接收变更上报数据，可以为 RT_NULL。
 * @return 修改成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t todo_snapshot_toggle_completed(rt_uint32_t index, todo_snapshot_change_t *change)
{
    todo_item_t *item;
    rt_tick_t now_tick;
    rt_uint64_t change_timestamp;

    if (!g_snapshot_ready)
    {
        todo_snapshot_init();
    }

    rt_mutex_take(&g_snapshot_lock, RT_WAITING_FOREVER);

    if (index >= g_snapshot.count)
    {
        rt_mutex_release(&g_snapshot_lock);
        return -RT_EINVAL;
    }

    item = &g_snapshot.items[index];
    item->completed = (item->completed == RT_TRUE) ? RT_FALSE : RT_TRUE;

    if (item->completed == RT_TRUE)
    {
        g_snapshot.completed_count++;
    }
    else if (g_snapshot.completed_count > 0U)
    {
        g_snapshot.completed_count--;
    }

    now_tick = rt_tick_get();
    change_timestamp = todo_resolve_change_timestamp(item, now_tick);
    item->updated_at = change_timestamp;

    g_snapshot.version += 1U;
    g_snapshot.timestamp = change_timestamp;
    g_snapshot.last_update_tick = now_tick;

    if (change != RT_NULL)
    {
        memset(change, 0, sizeof(*change));
        change->item = *item;
        change->timestamp = g_snapshot.timestamp;
        change->version = g_snapshot.version;
        change->count = g_snapshot.count;
        change->completed_count = g_snapshot.completed_count;
    }

    rt_mutex_release(&g_snapshot_lock);
    return RT_EOK;
}

