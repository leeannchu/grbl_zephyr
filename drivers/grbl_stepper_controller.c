#include "grbl_stepper_controller.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stm32f7xx_ll_tim.h>
#include <stm32f7xx_ll_gpio.h>
#include <stm32f7xx_ll_bus.h>
#include <zephyr/logging/log.h>
#include "grbl.h"

LOG_MODULE_REGISTER(grbl_stepper, CONFIG_GPIO_LOG_LEVEL);

#define DT_DRV_COMPAT grbl_stepper_controller
#define STEPPER_INST 0
#define STEPPER_TIMER_IRQN DT_IRQN(DT_INST_PHANDLE(STEPPER_INST, timer))
#define STEPPER_MIN_SCHEDULE_GUARD_CYCLES 128U

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

/* Wrap-safe timer compare: true when now has reached/passed target in modulo-2^32 time. */
static inline int tim_reached_or_passed(uint32_t now, uint32_t target)
{
    return (uint32_t)(now - target) < 0x80000000u;
}

int stepper_controller_set_period(const struct device *dev, uint32_t cycles);
void stepper_controller_set_steps(const struct device *dev, uint16_t step_mask);
void stepper_controller_clear_steps(const struct device *dev, uint16_t step_mask);
void stepper_controller_reset_steps(const struct device *dev);
void stepper_controller_enable_interrupt(const struct device *dev);
void stepper_controller_disable_interrupt(const struct device *dev);
void stepper_controller_set_pulse_width(const struct device *dev, uint32_t microseconds);

extern void stepper_driver_interrupt_handler(void); // function in stepper.c

/* data */
struct stepper_controller_data
{
    struct k_spinlock lock;
    uint32_t flags;
    uint32_t step_pulse_width_cycles;
    uint32_t last_cc1_fired;  // Timestamp of last CC1 interrupt (when CCR1 matched CNT)
    GPIO_TypeDef *step_ports[3];
    uint32_t step_pin_masks[3];
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
    struct stepper_controller_data *data = dev->data;

    // Check if the interrupt was triggered by Channel 1 (CC1)
    if (LL_TIM_IsActiveFlag_CC1(cfg->timer_instance))
    {
        // Capture the exact CC1 time (when CNT matched CCR1) FIRST before any other work
        // This is the reference point for the next period scheduling
        // Read CCR1 register directly from the timer instance
        uint32_t cc1_timestamp = cfg->timer_instance->CCR1;
        
        // Clear the CC1 interrupt flag
        LL_TIM_ClearFlag_CC1(cfg->timer_instance);
        
        // Store the CC1 fire time for period calculation (will be used in set_period)
        k_spinlock_key_t key = k_spin_lock(&data->lock);
        data->last_cc1_fired = cc1_timestamp;
        k_spin_unlock(&data->lock, key);

        // Call the "Brain" of Grbl to calculate the next step
        stepper_driver_interrupt_handler();
    }

    // Check if the interrupt was triggered by Channel 2 (CC2) for step pulse reset
    if (LL_TIM_IsActiveFlag_CC2(cfg->timer_instance))
    {
        LL_TIM_ClearFlag_CC2(cfg->timer_instance);
        
        // Reset the step pins to low after the pulse duration
        stepper_controller_reset_steps(dev);

        // Disable the CC2 interrupt until the next step
        LL_TIM_DisableIT_CC2(cfg->timer_instance);
    }
}

/* Driver Initialization Function (Called automatically by Zephyr at boot) */
static int stepper_controller_init(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;
    data->flags = 0;
    data->step_pulse_width_cycles = (10 * (F_TIM2_CLK / 1000000)); // Default 10 microseconds

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
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM2); // Reset Timer
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM2); // Release Reset

    LL_TIM_SetPrescaler(cfg->timer_instance, 0); // No prescaling, timer runs at full speed (System Core Clock)
    LL_TIM_SetAutoReload(cfg->timer_instance, 0xFFFFFFFF); // Max 32-bit
    LL_TIM_SetCounterMode(cfg->timer_instance, LL_TIM_COUNTERMODE_UP); // Upcounting mode

    LL_TIM_SetCounter(cfg->timer_instance, 0); // Set Counter to 0
    LL_TIM_OC_SetMode(cfg->timer_instance, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_FROZEN); // Output Compare Mode: Frozen
    LL_TIM_OC_SetMode(cfg->timer_instance, LL_TIM_CHANNEL_CH2, LL_TIM_OCMODE_FROZEN); // Output Compare Mode: Frozen
    
    LL_TIM_OC_SetCompareCH1(cfg->timer_instance, 0); // Initial Compare Value
    LL_TIM_OC_SetCompareCH2(cfg->timer_instance, 0);
    
    data->last_cc1_fired = 0; // Initialize CC1 fire timestamp

    IRQ_CONNECT(STEPPER_TIMER_IRQN, 0, stepper_timer_isr, DEVICE_DT_INST_GET(STEPPER_INST), 0); // Connect the timer interrupt to the ISR
    LL_TIM_DisableIT_CC1(cfg->timer_instance);
    LL_TIM_DisableIT_CC2(cfg->timer_instance);
    LL_TIM_ClearFlag_CC1(cfg->timer_instance);
    LL_TIM_ClearFlag_CC2(cfg->timer_instance);
    irq_enable(STEPPER_TIMER_IRQN);
    LL_TIM_EnableCounter(cfg->timer_instance); // Start Timer

    // Cache LL GPIO port/mask values in writable driver data.
    // From overlay: x-pulse-gpios = <&gpiob 10>, y-pulse-gpios = <&gpioa 3>, z-pulse-gpios = <&gpioa 0>
    data->step_ports[0] = GPIOB;
    data->step_pin_masks[0] = LL_GPIO_PIN_10;

    data->step_ports[1] = GPIOA;
    data->step_pin_masks[1] = LL_GPIO_PIN_3;

    data->step_ports[2] = GPIOA;
    data->step_pin_masks[2] = LL_GPIO_PIN_0;

    return 0;
}

// Enable the stepper controller interrupt
void stepper_controller_enable_interrupt(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->last_cc1_fired = LL_TIM_GetCounter(cfg->timer_instance); // Initialize to current time on enable
    k_spin_unlock(&data->lock, key);

    LL_TIM_EnableIT_CC1(cfg->timer_instance);
}

// Disable the stepper controller interrupt
void stepper_controller_disable_interrupt(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;

    LL_TIM_DisableIT_CC1(cfg->timer_instance);
    LL_TIM_DisableIT_CC2(cfg->timer_instance);
    LL_TIM_ClearFlag_CC2(cfg->timer_instance);

    // Ensure outputs are not left high when motion stops.
    gpio_pin_set_dt(&cfg->step_gpios[0], 0);
    gpio_pin_set_dt(&cfg->step_gpios[1], 0);
    gpio_pin_set_dt(&cfg->step_gpios[2], 0);
}

// Reset the Step bits to low for all axes
void stepper_controller_reset_steps(const struct device *dev)
{
    const struct stepper_controller_config *cfg = dev->config;
    gpio_pin_set_dt(&cfg->step_gpios[0], 0);
    gpio_pin_set_dt(&cfg->step_gpios[1], 0);
    gpio_pin_set_dt(&cfg->step_gpios[2], 0);
}

// Set the step pulse width in microseconds
void stepper_controller_set_pulse_width(const struct device *dev, uint32_t microseconds)
{
    struct stepper_controller_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->step_pulse_width_cycles = microseconds * (F_TIM2_CLK / 1000000);
    k_spin_unlock(&data->lock, key);
}

/* Set the delay (period) for the next timer interrupt */
int stepper_controller_set_period(const struct device *dev, uint32_t cycles)
{
    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);

    if (cycles == 0U)
    {
        k_spin_unlock(&data->lock, key);
        return -EINVAL;
    }
    
    // Base the next compare point on the last CC1 fire time, not the current counter
    uint32_t next_cc1 = data->last_cc1_fired + cycles;
    uint32_t current_cnt = LL_TIM_GetCounter(cfg->timer_instance);
    
    // If next deadline is already in the past, catch up by whole periods and keep phase continuity.
    if (tim_reached_or_passed(current_cnt, next_cc1))
    {
        uint32_t lag = current_cnt - next_cc1;
        uint32_t skip = (lag / cycles) + 1U;
        next_cc1 += skip * cycles;
    }

    // Guard against preemption window between computing next_cc1 and programming CCR1.
    // Keep phase continuity by advancing in whole periods until target is safely in the future.
    {
        uint32_t now_before_write = LL_TIM_GetCounter(cfg->timer_instance);
        uint32_t guard_target = now_before_write + STEPPER_MIN_SCHEDULE_GUARD_CYCLES;
        if (tim_reached_or_passed(guard_target, next_cc1))
        {
            uint32_t gap = guard_target - next_cc1;
            uint32_t skip = (gap / cycles) + 1U;
            next_cc1 += skip * cycles;
        }
    }
    
    LL_TIM_OC_SetCompareCH1(cfg->timer_instance, next_cc1);

    // Final safety net: if an ISR preempted us and we still missed the compare point, force the next period from "now" to avoid waiting until counter wrap.
    {
        uint32_t now_after_write = LL_TIM_GetCounter(cfg->timer_instance);
        if (tim_reached_or_passed(now_after_write, next_cc1))
        {
            LL_TIM_OC_SetCompareCH1(cfg->timer_instance, now_after_write + cycles);
        }
    }
    
    k_spin_unlock(&data->lock, key);
    return 0;
}

/* Set the Step bits high for the axes specified in the mask */
void stepper_controller_set_steps(const struct device *dev, uint16_t step_mask)
{
    if (step_mask == 0) {
        return; // No steps to set
    }

    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock); // Lock to prevent concurrent access to timer and GPIOs

    // Use LL_GPIO for performance
    if (step_mask & (1U << X_STEP_BIT))   // X
    {
        LL_GPIO_SetOutputPin(data->step_ports[0], data->step_pin_masks[0]);
    }

    if (step_mask & (1U << Y_STEP_BIT))   // Y
    {
        LL_GPIO_SetOutputPin(data->step_ports[1], data->step_pin_masks[1]);
    }

    if (step_mask & (1U << Z_STEP_BIT))   // Z
    {
        LL_GPIO_SetOutputPin(data->step_ports[2], data->step_pin_masks[2]);
    }

    // Schedule the reset of the step pins after the pulse width duration
    uint32_t current_cnt = LL_TIM_GetCounter(cfg->timer_instance);
    uint32_t target_cnt = current_cnt + data->step_pulse_width_cycles;
    
    // Disable CC2 interrupt first in case it's still active from previous step
    LL_TIM_DisableIT_CC2(cfg->timer_instance);
    LL_TIM_ClearFlag_CC2(cfg->timer_instance);
    
    // Set the compare value for CC2
    LL_TIM_OC_SetCompareCH2(cfg->timer_instance, target_cnt);
    
    // Check if we already passed the target time
    uint32_t check_cnt = LL_TIM_GetCounter(cfg->timer_instance);
    if (tim_reached_or_passed(check_cnt, target_cnt))
    {
        // Already passed the target time, reset GPIO immediately
        LOG_WRN("Pulse width setting too short for interrupt-based control, using direct reset. Missed by %u cycles", 
                check_cnt - target_cnt);
        gpio_pin_set_dt(&cfg->step_gpios[0], 0);
        gpio_pin_set_dt(&cfg->step_gpios[1], 0);
        gpio_pin_set_dt(&cfg->step_gpios[2], 0);
    }
    else
    {
        // Haven't passed yet, enable CC2 interrupt to reset later
        LL_TIM_EnableIT_CC2(cfg->timer_instance);
    }
    
    k_spin_unlock(&data->lock, key);
}

void stepper_controller_clear_steps(const struct device *dev, uint16_t step_mask)
{
    if (step_mask == 0)
    {
        return;
    }

    const struct stepper_controller_config *cfg = dev->config;
    struct stepper_controller_data *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock);

    if (step_mask & (1U << X_STEP_BIT))   // X
    {
        gpio_pin_set_dt(&cfg->step_gpios[0], 0);
    }

    if (step_mask & (1U << Y_STEP_BIT))   // Y
    {
        gpio_pin_set_dt(&cfg->step_gpios[1], 0);
    }

    if (step_mask & (1U << Z_STEP_BIT))   // Z
    {
        gpio_pin_set_dt(&cfg->step_gpios[2], 0);
    }

    k_spin_unlock(&data->lock, key);
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