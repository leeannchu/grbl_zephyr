
#ifndef __STM32F7XX_GRBL_H
#define __STM32F7XX_GRBL_H

/* type define */
/*typedef enum AxisEnum
{
    X_AXIS = 0,
    Y_AXIS,
    Z_AXIS,
    NUM_DIMENSIONS
} axis_t;*/


/* includes */
#include <stdint.h>
#include "main.h"
#include "encoder.h"

#define ENCODER_ENABLE


/* exported functions */
void vLoggingPrintf(const char *pcFormatString, ...);

#endif // __STM32F7XX_GRBL_H