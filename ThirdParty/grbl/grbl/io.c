
#include "grbl.h"

#ifdef ZEPHYR_ARCH
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define STEP_DISABLE_NODE DT_ALIAS(step_disable)
#define COOLANT_NODE DT_ALIAS(coolant)
#define SPINDLE_EN_NODE DT_ALIAS(spindle_enable)
#define SPINDLE_DIR_NODE DT_ALIAS(spindle_dir)

#define USER_OUT_0_NODE DT_ALIAS(user_out_0)
#define USER_OUT_1_NODE DT_ALIAS(user_out_1)
#define USER_OUT_2_NODE DT_ALIAS(user_out_2)
#define USER_OUT_3_NODE DT_ALIAS(user_out_3)

#define X_DIR_NODE DT_ALIAS(x_dir)
#define Y_DIR_NODE DT_ALIAS(y_dir)
#define Z_DIR_NODE DT_ALIAS(z_dir)
#define EN_LV_SHIFT_NODE DT_ALIAS(en_lv_shift)

static const struct gpio_dt_spec step_disable = GPIO_DT_SPEC_GET(STEP_DISABLE_NODE, gpios);
static const struct gpio_dt_spec coolant = GPIO_DT_SPEC_GET(COOLANT_NODE, gpios);
static const struct gpio_dt_spec spindle_en = GPIO_DT_SPEC_GET(SPINDLE_EN_NODE, gpios);
static const struct gpio_dt_spec spindle_dir = GPIO_DT_SPEC_GET(SPINDLE_DIR_NODE, gpios);

static const struct gpio_dt_spec user_out_0 = GPIO_DT_SPEC_GET(USER_OUT_0_NODE, gpios);
static const struct gpio_dt_spec user_out_1 = GPIO_DT_SPEC_GET(USER_OUT_1_NODE, gpios);
static const struct gpio_dt_spec user_out_2 = GPIO_DT_SPEC_GET(USER_OUT_2_NODE, gpios);
static const struct gpio_dt_spec user_out_3 = GPIO_DT_SPEC_GET(USER_OUT_3_NODE, gpios);

static const struct gpio_dt_spec x_dir = GPIO_DT_SPEC_GET(X_DIR_NODE, gpios);
static const struct gpio_dt_spec y_dir = GPIO_DT_SPEC_GET(Y_DIR_NODE, gpios);
static const struct gpio_dt_spec z_dir = GPIO_DT_SPEC_GET(Z_DIR_NODE, gpios);
static const struct gpio_dt_spec en_lv_shift = GPIO_DT_SPEC_GET(EN_LV_SHIFT_NODE, gpios);

void io_init(void)
{
#define CONFIG_PIN(spec)                                      \
    if (gpio_is_ready_dt(&(spec)))                            \
    {                                                         \
        gpio_pin_configure_dt(&(spec), GPIO_OUTPUT_INACTIVE); \
    }

    CONFIG_PIN(step_disable);
    CONFIG_PIN(coolant);
    CONFIG_PIN(spindle_en);
    CONFIG_PIN(spindle_dir);

    CONFIG_PIN(user_out_0);
    CONFIG_PIN(user_out_1);
    CONFIG_PIN(user_out_2);
    CONFIG_PIN(user_out_3);

    CONFIG_PIN(x_dir);
    CONFIG_PIN(y_dir);
    CONFIG_PIN(z_dir);
    CONFIG_PIN(en_lv_shift);

    gpio_pin_set_dt(&en_lv_shift, 1); // Enable level shifter (ACTIVE_LOW: logical 1 = physical LOW = enabled)
}

void io_output_sync(uint8_t pin, uint8_t state)
{
    if (sys.state == STATE_CHECK_MODE)
    {
        return;
    }

    protocol_buffer_synchronize();

    int val = (state) ? 1 : 0;

    switch (pin)
    {
    case PIN_STEPPERS_DISABLE:
        gpio_pin_set_dt(&step_disable, val);
        break;

    case PIN_COOLANT_FLOOD:
        gpio_pin_set_dt(&coolant, val);
        break;

    case PIN_SPINDLE_ENABLE:
        gpio_pin_set_dt(&spindle_en, val);
        break;

    case PIN_SPINDLE_DIRECTION:
        gpio_pin_set_dt(&spindle_dir, val);
        break;

    case PIN_USER_OUTPUT_0:
        gpio_pin_set_dt(&user_out_0, val);
        break;

    case PIN_USER_OUTPUT_1:
        gpio_pin_set_dt(&user_out_1, val);
        break;

    case PIN_USER_OUTPUT_2:
        gpio_pin_set_dt(&user_out_2, val);
        break;

    case PIN_USER_OUTPUT_3:
        gpio_pin_set_dt(&user_out_3, val);
        break;

    default:
        break;
    }
}

#elif defined(AVR_ARCH)
/**
 * @brief Initialize the I/O pins.
 */
/*
void io_init()
{
    // reset the output pins

}
*/

/**
 * @brief Set the state of an output pin.
 */
void io_output_sync(uint8_t pin, uint8_t state)
{
    // set the state of the pin
    if (sys.state == STATE_CHECK_MODE)
    {
        return;
    }
    protocol_buffer_synchronize(); // Ensure coolant turns on when specified in program.

    switch (pin)
    {
    case PIN_STEPPERS_DISABLE:
        if (state)
        {
            STEPPERS_DISABLE_PORT |= (1 << STEPPERS_DISABLE_BIT);
        }
        else
        {
            STEPPERS_DISABLE_PORT &= ~(1 << STEPPERS_DISABLE_BIT);
        }
        break;
    case PIN_COOLANT_FLOOD:
        if (state)
        {
            COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
        }
        else
        {
            COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
        }
        break;
    case PIN_SPINDLE_ENABLE:
        if (state)
        {
            SPINDLE_ENABLE_PORT |= (1 << SPINDLE_ENABLE_BIT);
        }
        else
        {
            SPINDLE_ENABLE_PORT &= ~(1 << SPINDLE_ENABLE_BIT);
        }
        break;
    case PIN_SPINDLE_DIRECTION:
        if (state)
        {
            SPINDLE_DIRECTION_PORT |= (1 << SPINDLE_DIRECTION_BIT);
        }
        else
        {
            SPINDLE_DIRECTION_PORT &= ~(1 << SPINDLE_DIRECTION_BIT);
        }
        break;
    case PIN_USER_OUTPUT_0:
        if (state)
        {
            USER_OUTPUT_PORT &= ~(1 << USER_OUTPUT_0_BIT);
        }
        else
        {
            USER_OUTPUT_PORT |= (1 << USER_OUTPUT_0_BIT);
        }
        break;
    case PIN_USER_OUTPUT_1:
        if (state)
        {
            USER_OUTPUT_PORT &= ~(1 << USER_OUTPUT_1_BIT);
        }
        else
        {
            USER_OUTPUT_PORT |= (1 << USER_OUTPUT_1_BIT);
        }
        break;
    case PIN_USER_OUTPUT_2:
        if (state)
        {
            USER_OUTPUT_PORT &= ~(1 << USER_OUTPUT_2_BIT);
        }
        else
        {
            USER_OUTPUT_PORT |= (1 << USER_OUTPUT_2_BIT);
        }
        break;
    case PIN_USER_OUTPUT_3:
        if (state)
        {
            USER_OUTPUT_PORT &= ~(1 << USER_OUTPUT_3_BIT);
        }
        else
        {
            USER_OUTPUT_PORT |= (1 << USER_OUTPUT_3_BIT);
        }
        break;
    }
}
#endif