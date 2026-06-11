#include <stdlib.h>
#include "stm32f7xx_grbl.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include "encoder.h"
#include "grbl.h"

#define FULL_COUNTER 0xFFFF
#define HALF_COUNTER 0x7FFF
#define FULL_16BIT_RANGE 65536
#define HALF_16BIT_RANGE 32768

// resolution of degree after decimal point
#define DEGREE_RESOLUTION 100        // 2 decimal points
#define PULSE_PER_REVOLUTION 2000    // 2000 pulses per revolution
#define CALCULATE_RPM_PERIOD_MS 1000 // 1 second = 1000 ms

/* prototypes declaration */
void printHeapStatus(void);

/* Type define */
typedef struct
{
    const struct device *dev;
    volatile int32_t counter32Bit;
    volatile uint16_t prevCounter;
    int16_t revolution;
} encoder_param_t;

static encoder_param_t encoder_param[NUM_DIMENSIONS];

volatile uint32_t previousTicks = 0;  // store previous ticks
volatile uint32_t previousDegree = 0; // store previous degree value
volatile float speedRPM = 0;          // store speed in RPM

void encoderInit()
{
    // Initialize encoder parameters
    encoder_param[X_AXIS].dev = DEVICE_DT_GET(DT_ALIAS(encoder_x));
    encoder_param[Y_AXIS].dev = DEVICE_DT_GET(DT_ALIAS(encoder_y));
    encoder_param[Z_AXIS].dev = DEVICE_DT_GET(DT_ALIAS(encoder_z));

    
    for (int i = 0; i < NUM_DIMENSIONS; i++)
    {
        if (device_is_ready(encoder_param[i].dev))
        {
            encoderResetCounter(i);
        }
        else
        {
            printk("Encoder device for axis %d is not ready\n", i);
        }
    }
}

void encoderInterruptHandler()
{
    struct sensor_value val;
    uint16_t currentCounterArr[NUM_DIMENSIONS] = {0};

    // read encoder counter value of each axis at once in order to get simultaneous value
    for (uint8_t i = 0; i < NUM_DIMENSIONS; i++)
    {
        encoder_param_t *encoder = &encoder_param[i];
        if (!encoder->dev)
            continue;

        // fetch the latest counter value from the sensor API
        sensor_sample_fetch(encoder->dev);
        sensor_channel_get(encoder->dev, SENSOR_CHAN_ENCODER_COUNT, &val);

        currentCounterArr[i] = (uint16_t)val.val1;

        // calculate the difference between current counter and previous counter
        int32_t diff = (int32_t)currentCounterArr[i] - (int32_t)encoder->prevCounter;

        // handle overflow and underflow
        if (diff > HALF_16BIT_RANGE)
        {
            diff -= FULL_16BIT_RANGE;
        }
        else if (diff < -HALF_16BIT_RANGE)
        {
            diff += FULL_16BIT_RANGE;
        }

        encoder->counter32Bit += diff;
        encoder->prevCounter = currentCounterArr[i];
    }
}

void encoderResetCounter(axis_t axis)
{
    struct sensor_value val;
    encoder_param_t *encoder = &encoder_param[axis];

    if (!encoder->dev)
        return;

    encoder->revolution = 0;
    encoder->counter32Bit = 0;

    sensor_sample_fetch(encoder->dev);
    sensor_channel_get(encoder->dev, SENSOR_CHAN_ENCODER_COUNT, &val);
    encoder->prevCounter = (uint16_t)val.val1;
}

/**
 * @brief Read the degree value from the encoder
 * @return void
 */
void encoderReadDegree(encoder_degree_t *degree)
{
    for (uint8_t i = 0; i < NUM_DIMENSIONS; i++)
    {
        encoder_param_t *encoder = &encoder_param[i];
        ((float *)degree)[i] = (float)(encoder->counter32Bit * (360.0 / PULSE_PER_REVOLUTION));
    }
}

/**
 * @brief Read the degree value from current timer's counter instead of reading from the recorded value
 *        which will get more accurate value.
 */
void encoderReadInstantDegree(encoder_degree_t *degree)
{
    // update counter value
    // encoderInterruptHandler();

    // read degree value
    encoderReadDegree(degree);
}

void encoderReadPosition(encoder_position_t *position)
{
    encoder_degree_t deg;
    encoderReadDegree(&deg);

    for (uint8_t i = 0; i < NUM_DIMENSIONS; i++)
    {
        ((float *)position)[i] = settings.mm_per_rev[i] * (deg[i] / 360.0f);
    }
}

void encoderReadInstantPosition(encoder_position_t *position)
{
    encoder_degree_t deg;
    encoderReadInstantDegree(&deg);

    for (uint8_t i = 0; i < NUM_DIMENSIONS; i++)
    {
        ((float *)position)[i] = settings.mm_per_rev[i] * (deg[i] / 360.0f);
    }
}

// ===> Debugging purpose
void printHeapStatus(void)
{
    // Get the current free heap size
    // size_t freeHeapSize = xPortGetFreeHeapSize();

    // Get the minimum free heap size ever recorded
    // size_t minEverFreeHeapSize = xPortGetMinimumEverFreeHeapSize();

    // Print the heap sizes
    // vLoggingPrintf("Current free heap size: %u bytes\n", (unsigned int)freeHeapSize);
    // vLoggingPrintf("Minimum ever free heap size: %u bytes\n", (unsigned int)minEverFreeHeapSize);
}

/**
 * TODO: RPM calculation
 */
/*
void encoderCalculateRPM()
{
    // get current ticks in milliseconds
    uint32_t currentTicks = HAL_GetTick();
    uint32_t diffTicks = currentTicks - previousTicks;

    // if time period is less than 1 second, return
    if (diffTicks < CALCULATE_RPM_PERIOD_MS)
        return;

    // update previous ticks
    previousTicks = currentTicks;

    // get current degree
    int32_t currentDegree = encoderReadDegree();
    int32_t diffDegree = currentDegree - previousDegree;
    // update previous degree
    previousDegree = currentDegree;

    // calculate RPM
    speedRPM = ((float)diffDegree / (360.0 * (float)DEGREE_RESOLUTION)) * (60000.0 / (float)diffTicks);
}

float encoderReadRPM()
{
    return speedRPM;
}


uint32_t encoderGetCounter(axis_t axis)
{
    return encoder_param[axis].counter32Bit;
}


*/
