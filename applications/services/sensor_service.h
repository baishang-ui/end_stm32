#ifndef __SENSOR_SERVICE_H__
#define __SENSOR_SERVICE_H__

#include <rtthread.h>

#include "sensor/dht11.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_bool_t started;
    rt_bool_t has_sample;
    rt_err_t last_error;
    rt_tick_t sample_tick;
    rt_tick_t last_attempt_tick;
    dht11_sample_t sample;
} sensor_service_status_t;

int sensor_service_start(void);
void sensor_service_get_status(sensor_service_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_SERVICE_H__ */
