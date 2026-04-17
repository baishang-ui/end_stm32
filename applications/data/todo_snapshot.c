#include "todo_snapshot.h"

#include <string.h>
#include <stdlib.h>

#define DBG_TAG    "todo.snap"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

static todo_snapshot_t g_snapshot;
static struct rt_mutex g_snapshot_lock;
static rt_bool_t g_snapshot_ready = RT_FALSE;

static const char *todo_skip_ws(const char *p)
{
    while ((p != RT_NULL) &&
           ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
    {
        p++;
    }

    return p;
}

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
                /* Keep parser tiny: skip \uXXXX, store '?' as placeholder. */
                if ((p[0] != '\0') && (p[1] != '\0') && (p[2] != '\0') && (p[3] != '\0'))
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

rt_err_t todo_snapshot_toggle_completed(rt_uint32_t index, todo_snapshot_change_t *change)
{
    todo_item_t *item;
    rt_tick_t now_tick;

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
    if (item->updated_at == 0U)
    {
        item->updated_at = (rt_uint64_t)now_tick;
    }
    else
    {
        item->updated_at += 1U;
    }

    g_snapshot.version += 1U;
    g_snapshot.timestamp = item->updated_at;
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

