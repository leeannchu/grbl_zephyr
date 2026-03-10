#ifndef STEPPER_DRIVER_H
#define STEPPER_DRIVER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdint.h>

/* Set the delay (period) for the next timer interrupt */
int stepper_controller_set_period(const struct device *dev, uint32_t cycles);

/* Set the Step bits high for the axes specified in the mask */
void stepper_controller_set_steps(const struct device *dev, uint8_t step_mask);

void stepper_controller_reset_steps(const struct device *dev);

void stepper_controller_enable_interrupt(const struct device *dev);

void stepper_controller_disable_interrupt(const struct device *dev);

void stepper_controller_set_pulse_width(const struct device *dev, uint32_t microseconds);

#endif // STEPPER_DRIVER_H