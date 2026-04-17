#include "oled_hal_input.h"

#define APP_ENC_PIN_A   GET_PIN(B, 12)
#define APP_ENC_PIN_B   GET_PIN(B, 13)
#define APP_KEY_ENTER   GET_PIN(B, 14)
#define APP_KEY_BACK    GET_PIN(A, 0)

typedef struct
{
    rt_base_t pin;
    rt_uint8_t stable_level;
    rt_uint8_t last_raw_level;
    rt_tick_t change_tick;
    rt_tick_t debounce_ticks;
} app_key_t;

typedef struct
{
    rt_base_t pin_a;
    rt_base_t pin_b;
    rt_uint8_t prev_ab;
    rt_int8_t acc;
    volatile rt_int16_t step_delta;
    rt_tick_t ab_last_tick;
    rt_tick_t step_last_tick;
    rt_tick_t ab_debounce_ticks;
    rt_tick_t step_lock_ticks;
    rt_bool_t enabled;
} app_encoder_t;

typedef struct
{
    app_encoder_t *enc;
} app_encoder_irq_ctx_t;

static app_key_t s_enter_key;
static app_key_t s_back_key;
static app_encoder_t s_encoder;
static app_encoder_irq_ctx_t s_enc_irq_a;
static app_encoder_irq_ctx_t s_enc_irq_b;

static void app_key_init(app_key_t *key, rt_base_t pin, rt_tick_t debounce_ticks)
{
    key->pin = pin;
    key->debounce_ticks = debounce_ticks;
    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
    key->stable_level = (rt_uint8_t)(rt_pin_read(pin) ? 1U : 0U);
    key->last_raw_level = key->stable_level;
    key->change_tick = rt_tick_get();
}

static int app_key_read(app_key_t *key)
{
    rt_uint8_t raw_level = (rt_uint8_t)(rt_pin_read(key->pin) ? 1U : 0U);
    rt_tick_t now = rt_tick_get();

    if (raw_level != key->last_raw_level)
    {
        key->last_raw_level = raw_level;
        key->change_tick = now;
    }
    else if ((key->stable_level != raw_level) &&
             ((now - key->change_tick) >= key->debounce_ticks))
    {
        key->stable_level = raw_level;
    }

    return key->stable_level;
}

static void app_encoder_handle_irq(app_encoder_t *enc)
{
    static const rt_int8_t qdec[16] =
    {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };
    rt_tick_t now = rt_tick_get();
    rt_uint8_t a;
    rt_uint8_t b;
    rt_uint8_t curr_ab;
    rt_uint8_t idx;

    if ((now - enc->ab_last_tick) < enc->ab_debounce_ticks)
    {
        return;
    }

    a = (rt_uint8_t)(rt_pin_read(enc->pin_a) ? 1U : 0U);
    b = (rt_uint8_t)(rt_pin_read(enc->pin_b) ? 1U : 0U);
    curr_ab = (rt_uint8_t)((a << 1) | b);

    if (curr_ab == enc->prev_ab)
    {
        return;
    }

    idx = (rt_uint8_t)((enc->prev_ab << 2) | curr_ab);
    enc->acc += qdec[idx];
    enc->prev_ab = curr_ab;
    enc->ab_last_tick = now;

    if (enc->acc >= 4)
    {
        if ((now - enc->step_last_tick) >= enc->step_lock_ticks)
        {
            enc->step_last_tick = now;
            if (enc->step_delta < 32)
            {
                enc->step_delta++;
            }
        }
        enc->acc = 0;
    }
    else if (enc->acc <= -4)
    {
        if ((now - enc->step_last_tick) >= enc->step_lock_ticks)
        {
            enc->step_last_tick = now;
            if (enc->step_delta > -32)
            {
                enc->step_delta--;
            }
        }
        enc->acc = 0;
    }
}

static void app_encoder_irq_handler(void *args)
{
    app_encoder_irq_ctx_t *ctx = (app_encoder_irq_ctx_t *)args;

    if ((ctx == RT_NULL) || (ctx->enc == RT_NULL) || (ctx->enc->enabled == RT_FALSE))
    {
        return;
    }

    app_encoder_handle_irq(ctx->enc);
}

void oled_ui_timer_init(void)
{
}

void oled_ui_key_init(void)
{
    app_key_init(&s_enter_key, APP_KEY_ENTER, rt_tick_from_millisecond(18));
    app_key_init(&s_back_key, APP_KEY_BACK, rt_tick_from_millisecond(18));
}

void oled_ui_encoder_init(void)
{
    rt_uint8_t a;
    rt_uint8_t b;

    s_encoder.pin_a = APP_ENC_PIN_A;
    s_encoder.pin_b = APP_ENC_PIN_B;
    s_encoder.acc = 0;
    s_encoder.step_delta = 0;
    s_encoder.ab_last_tick = 0;
    s_encoder.step_last_tick = 0;
    s_encoder.ab_debounce_ticks = rt_tick_from_millisecond(1);
    s_encoder.step_lock_ticks = rt_tick_from_millisecond(2);
    s_encoder.enabled = RT_TRUE;

    rt_pin_mode(APP_ENC_PIN_A, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(APP_ENC_PIN_B, PIN_MODE_INPUT_PULLUP);

    a = (rt_uint8_t)(rt_pin_read(APP_ENC_PIN_A) ? 1U : 0U);
    b = (rt_uint8_t)(rt_pin_read(APP_ENC_PIN_B) ? 1U : 0U);
    s_encoder.prev_ab = (rt_uint8_t)((a << 1) | b);

    s_enc_irq_a.enc = &s_encoder;
    s_enc_irq_b.enc = &s_encoder;

    rt_pin_attach_irq(APP_ENC_PIN_A, PIN_IRQ_MODE_RISING_FALLING, app_encoder_irq_handler, &s_enc_irq_a);
    rt_pin_attach_irq(APP_ENC_PIN_B, PIN_IRQ_MODE_RISING_FALLING, app_encoder_irq_handler, &s_enc_irq_b);
    rt_pin_irq_enable(APP_ENC_PIN_A, PIN_IRQ_ENABLE);
    rt_pin_irq_enable(APP_ENC_PIN_B, PIN_IRQ_ENABLE);
}

void oled_ui_encoder_enable(void)
{
    s_encoder.enabled = RT_TRUE;
}

void oled_ui_encoder_disable(void)
{
    s_encoder.enabled = RT_FALSE;
}

int16_t oled_ui_encoder_get_delta(void)
{
    rt_base_t level;
    rt_int16_t delta;

    if (s_encoder.enabled == RT_FALSE)
    {
        return 0;
    }

    level = rt_hw_interrupt_disable();
    delta = s_encoder.step_delta;
    s_encoder.step_delta = 0;
    rt_hw_interrupt_enable(level);

    return delta;
}

int oled_ui_read_enter_key(void)
{
    return app_key_read(&s_enter_key);
}

int oled_ui_read_back_key(void)
{
    return app_key_read(&s_back_key);
}

void oled_ui_delay_ms(uint32_t xms)
{
    HAL_Delay(xms);
}

void oled_ui_delay_s(uint32_t xs)
{
    while (xs--)
    {
        oled_ui_delay_ms(1000);
    }
}


