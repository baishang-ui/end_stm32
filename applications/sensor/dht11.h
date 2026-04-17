#ifndef __DHT11_H__
#define __DHT11_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_int32_t temperature_x10;
    rt_int32_t humidity_x10;
} dht11_sample_t;

rt_err_t dht11_init(void);
rt_err_t dht11_read(dht11_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H__ */
