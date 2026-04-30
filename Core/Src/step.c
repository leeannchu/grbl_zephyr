//delete old DMA code and replace with stepBlockAxis function, which is called by limits.c when a limit switch is triggered during homing, to immediately block the step pin for that axis, which is the same behavior as the legacy FORCE_OC_OUTPUT_LOW feature. 
//This is needed to prevent the step pin from being pulsed after a limit switch is triggered, which can cause the machine to crash into the limit switch and cause damage.

#ifdef ZEPHYR_ARCH

#include "grbl.h"
#include "grbl_stepper_controller.h"

static const struct device *stepper_dev = DEVICE_DT_GET(DT_NODELABEL(stepper_controller));

void stepBlockAxis(uint8_t axis)
{
    if (axis >= N_AXIS)
    {
        return;
    }

    // During homing, immediately mask out this axis step pin.
    sys.homing_axis_lock &= ~get_step_pin_mask(axis);

    // Mirror legacy FORCE_OC_OUTPUT_LOW behavior for this axis.
    if (device_is_ready(stepper_dev))
    {
        stepper_controller_clear_steps(stepper_dev, get_step_pin_mask(axis));
    }
}

#endif // ZEPHYR_ARCH