#include "grbl_stepper_controller.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stm32f7xx_ll_tim.h>
#include <stm32f7xx_ll_gpio.h>
#include <stm32f7xx_ll_bus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(grbl_stepper, CONFIG_GPIO_LOG_LEVEL);

#define DT_DRV_COMPAT grbl_stepper_controller

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#error "Stepper Driver is enabled without any devices"
#endif

/* define control flags */
#define STEPPER_FLAG_BUSY BIT(0)       // a lock to indicate that the driver is busy
#define STEPPER_FLAG_MOVING BIT(1)     // a flag to indicate that the timer is active and motors are moving
#define STEPPER_FLAG_UPDATE_REQ BIT(2) // a flag to signal that new step parameters need to be applied in the next interrupt

// lock to prevent concurrent access
#define __STEPPER_CONTROLLER_LOCK(dev)                    \
    do                                                    \
    {                                                     \
        struct stepper_controller_data *data = dev->data; \
        if (data->flags & STEPPER_FLAG_BUSY)              \
        {                                                 \
            return -EBUSY;                                \
        }                                                 \
        data->flags |= STEPPER_FLAG_BUSY;                 \
    } while (0)
#define __STEPPER_CONTROLLER_UNLOCK(dev)                  \
    do                                                    \
    {                                                     \
        struct stepper_controller_data *data = dev->data; \
        data->flags &= ~STEPPER_FLAG_BUSY;                \
    } while (0)

/* private function prototype */
static int stepper_controller_init(const struct device *dev);
static void stepper_timer_isr(void *arg);

int stepper_controller_set_period(const struct device *dev, uint32_t cycles);
void stepper_controller_set_steps(const struct device *dev, uint8_t step_mask);
void stepper_controller_reset_steps(const struct device *dev);
void stepper_controller_enable_interrupt(const struct device *dev);
void stepper_controller_disable_interrupt(const struct device *dev);

extern void stepper_driver_interrupt_handler(void); // function in stepper.c

/* data */
struct stepper_controller_data
{
    struct k_spinlock lock;
};

/* configuration */
struct stepper_controller_config
{
    TIM_TypeDef *timer_instance;
    const struct gpio_dt_spec step_gpios[3];
};

/* Interrupt Service Routine (ISR) for the Timer */
static void stepper_timer_isr(void *arg)
{
    // Retrieve the device configuration (to get the timer instance)
    const struct device *dev = (const struct device *)arg;
    const struct stepper_controller_config *cfg = dev->config;

    // Check if the interrupt was triggered by Channel 1 (CC1)
    if (LL_TIM_IsActiveFlag_CC1(cfg->timer_instance))
    {

        // Clear the interrupt flag immediately!
        // If we don't do this, the ISR will trigger infinitely and hang the CPU.
        LL_TIM_ClearFlag_CC1(cfg->timer_instance);

        // Call the "Brain" of Grbl to calculate the next step
        stepper_driver_interrupt_handler();
    }
}

/* Driver Initialization Function (Called automatically by Zephyr at boot) */
static int stepper_controller_init(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;

    for (int i = 0; i < 3; i++)
    {
        if (!gpio_is_ready_dt(&cfg->step_gpios[i]))
        {
            LOG_ERR("GPIO is not ready");
            return -ENODEV;
        }
        gpio_pin_configure_dt(&cfg->step_gpios[i], GPIO_OUTPUT_INACTIVE); // Configure pin as Output and set it to Inactive (Low) initially
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2); // Enable Timer Clock

    LL_TIM_SetPrescaler(cfg->timer_instance, 107); // Prescaler: 108 MHz / (107 + 1) = 1 MHz (1 tick = 1 microsecond)

    // Auto-Reload: Set to Max 32-bit value to prevent unwanted overflow resets
    LL_TIM_SetAutoReload(cfg->timer_instance, 0xFFFFFFFF);

    // Counter Mode: Up-counting (0, 1, 2...)
    LL_TIM_SetCounterMode(cfg->timer_instance, LL_TIM_COUNTERMODE_UP);

    // Initialize Counter value to 0
    LL_TIM_SetCounter(cfg->timer_instance, 0);

    // 4. Connect Interrupt (The most critical part!)
    // This tells Zephyr: "When TIM2 hardware interrupt fires, run stepper_timer_isr()"
    // TIM2_IRQn is the hardware signal number.
    // Priority '0' means Highest Priority (Grbl needs this!).
    IRQ_CONNECT(TIM2_IRQn, 0, stepper_timer_isr, DEVICE_DT_INST_GET(0), 0);

    // Enable the interrupt in the NVIC (Nested Vector Interrupt Controller)
    irq_enable(TIM2_IRQn);

    // 5. Start the Timer!
    // The timer starts counting now, but no interrupts will happen yet
    // because we haven't called enable_interrupt() (CC1IE) yet.
    LL_TIM_EnableCounter(cfg->timer_instance);

    return 0;
}

void stepper_controller_enable_interrupt(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;
    LL_TIM_EnableIT_CC1(cfg->timer_instance);
}

void stepper_controller_disable_interrupt(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;
    LL_TIM_DisableIT_CC1(cfg->timer_instance);
}

void stepper_controller_reset_steps(const struct device *dev)
{
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_10);
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_3 | LL_GPIO_PIN_0);
}

/* Set the delay (period) for the next timer interrupt */
int stepper_controller_set_period(const struct device *dev, uint32_t cycles)
{
    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    uint32_t current_cnt = LL_TIM_GetCounter(cfg->timer_instance); // Get Current Time
    // Note: We write to Capture/Compare Register 1 (CCR1).
    //       When CNT matches CCR1, the hardware triggers the interrupt.
    // Note: 32-bit overflow is automatically handled by the hardware logic.
    LL_TIM_OC_SetCompareCH1(cfg->timer_instance, current_cnt + cycles);

    k_spin_unlock(&data->lock, key);
    return 0;
}

/* Set the Step bits high for the axes specified in the mask */
void stepper_controller_set_steps(const struct device *dev, uint8_t step_mask)
{
    if (step_mask & (1 << 0)) // X Axis(Bit 0)
    {
        LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_10);
    }

    if (step_mask & (1 << 1)) // Y Axis (Bit 1)
    {
        LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_3);
    }

    if (step_mask & (1 << 2)) // Z Axis (Bit 2)
    {
        LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_0);
    }
}

/* helper macro to define a new driver instance */
#define STEPPER_DEFINE(inst)                                                        \
    static struct stepper_controller_data data_##inst;                              \
    static const struct stepper_controller_config config_##inst = {                 \
        .timer_instance = (TIM_TypeDef *)DT_REG_ADDR(DT_INST_PHANDLE(inst, timer)), \
        .step_gpios = {                                                             \
            GPIO_DT_SPEC_INST_GET(inst, x_pulse_gpios),                             \
            GPIO_DT_SPEC_INST_GET(inst, y_pulse_gpios),                             \
            GPIO_DT_SPEC_INST_GET(inst, z_pulse_gpios),                             \
        },                                                                          \
    };                                                                              \
                                                                                    \
    DEVICE_DT_INST_DEFINE(inst,                                                     \
                          stepper_controller_init,                                  \
                          NULL,                                                     \
                          &data_##inst,                                             \
                          &config_##inst,                                           \
                          POST_KERNEL,                                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,                       \
                          NULL);

/* Create the struct device for every status "okay" node in the devicetree */
DT_INST_FOREACH_STATUS_OKAY(STEPPER_DEFINE)