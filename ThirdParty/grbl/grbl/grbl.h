/*
  grbl.h - main Grbl include file
  Part of Grbl

  Copyright (c) 2015-2016 Sungeun K. Jeon for Gnea Research LLC

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef grbl_h
#define grbl_h

// Grbl versioning system
#define GRBL_VERSION "1.1h"
#define GRBL_VERSION_BUILD "20190830"

// Define standard libraries used by Grbl.
#ifdef AVR_ARCH
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#endif // AVR_ARCH

#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef STM32F7XX_ARCH
#include "stm32f7xx_grbl.h"
#include "ethernet_if.h"
#define F_CPU 108000000
#endif // STM32F7XX_ARCH

#ifdef ZEPHYR_ARCH
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#define F_CPU 216000000
#define F_TIM2_CLK 108000000 // TIM2 and TIM5 clock frequency on STM32F7xx is APB1 clock frequency
#define F_TIM1_CLK 108000000 // TIM1 clock frequency on STM32F7xx is APB2 clock frequency
#endif // ZEPHYR_ARCH

/* type define */
#if defined(AVR_ARCH)
typedef uint8_t IO_TYPE;
#endif

#if defined(ZEPHYR_ARCH)
typedef uint16_t IO_TYPE;
#define PSTR(x) x //  AVR macro
#ifndef M_PI      // 缺少的數學常數 (Zephyr 的 math.h 有時需要手動開啟這個)
#define M_PI 3.14159265358979323846
#endif
#endif

#ifdef ZEPHYR_ARCH
static inline void delay_ms(uint32_t ms)
{
  k_msleep(ms);
}

static inline void delay_us(uint32_t us)
{
  k_busy_wait(us);
}

static inline void delay_sec(float sec, uint8_t mode)
{
  // GRBL 的 delay_sec 有第二個參數 mode，我們這裡暫時忽略它
  k_msleep((int32_t)(sec * 1000.0f));
}
extern uint32_t SystemCoreClock; // 要再確認
#endif

/////////////////////////////////////// 補被註解掉的encoder假函式宣告還有一些其他假函式宣告
#ifdef ZEPHYR_ARCH
typedef float encoder_degree_t[3];
typedef float encoder_position_t[3];

static inline void encoderReadPosition(encoder_position_t *pos)
{

  (*pos)[0] = 0.0f;
  (*pos)[1] = 0.0f;
  (*pos)[2] = 0.0f;
}
static inline void encoderReadInstantPosition(encoder_degree_t *deg)
{
  (*deg)[0] = 0.0f;
  (*deg)[1] = 0.0f;
  (*deg)[2] = 0.0f;
}
static inline void encoderResetCounter(uint8_t axis) {} // 空函式
#define NUM_DIMENSIONS 3
void flashMemcpyToEepromWithChecksum(uint32_t dest, char *source, uint32_t size);
void flashWriteVersion(uint8_t ver);
void startWWDG(void);

#endif
///////////////////////////////////////////////////////

// Define the Grbl system include files. NOTE: Do not alter organization.
#include "config.h"
#include "nuts_bolts.h"
#include "settings.h"
#include "system.h"
#include "defaults.h"
#include "cpu_map.h"

#ifdef ZEPHYR_ARCH
#include "step.h"
#include "io.h"
#endif // ZEPHYR_ARCH

#include "planner.h"
#include "coolant_control.h"
#include "eeprom.h"
#include "gcode.h"
#include "limits.h"
#include "motion_control.h"
#include "planner.h"
#include "print.h"
#include "probe.h"
#include "protocol.h"
#include "report.h"
#include "serial.h"
#include "spindle_control.h"
#include "stepper.h"
#include "jog.h"

/*#ifdef ZEPHYR_ARCH
void mainGRBL(void *pvParameters);
#endif // ZEPHYR_ARCH*/

#ifdef ZEPHYR_ARCH
void mainGRBL(void);
#endif // ZEPHYR_ARCH

// ---------------------------------------------------------------------------------------
// COMPILE-TIME ERROR CHECKING OF DEFINE VALUES:

#ifndef HOMING_CYCLE_0
#error "Required HOMING_CYCLE_0 not defined."
#endif

#if defined(USE_SPINDLE_DIR_AS_ENABLE_PIN) && !defined(VARIABLE_SPINDLE)
#error "USE_SPINDLE_DIR_AS_ENABLE_PIN may only be used with VARIABLE_SPINDLE enabled"
#endif

#if defined(USE_SPINDLE_DIR_AS_ENABLE_PIN) && !defined(CPU_MAP_ATMEGA328P)
#error "USE_SPINDLE_DIR_AS_ENABLE_PIN may only be used with a 328p processor"
#endif

#if !defined(USE_SPINDLE_DIR_AS_ENABLE_PIN) && defined(SPINDLE_ENABLE_OFF_WITH_ZERO_SPEED)
#error "SPINDLE_ENABLE_OFF_WITH_ZERO_SPEED may only be used with USE_SPINDLE_DIR_AS_ENABLE_PIN enabled"
#endif

#if defined(PARKING_ENABLE)
#if defined(HOMING_FORCE_SET_ORIGIN)
#error "HOMING_FORCE_SET_ORIGIN is not supported with PARKING_ENABLE at this time."
#endif
#endif

#if defined(ENABLE_PARKING_OVERRIDE_CONTROL)
#if !defined(PARKING_ENABLE)
#error "ENABLE_PARKING_OVERRIDE_CONTROL must be enabled with PARKING_ENABLE."
#endif
#endif

#if defined(SPINDLE_PWM_MIN_VALUE)
#if !(SPINDLE_PWM_MIN_VALUE > 0)
#error "SPINDLE_PWM_MIN_VALUE must be greater than zero."
#endif
#endif

#if (REPORT_WCO_REFRESH_BUSY_COUNT < REPORT_WCO_REFRESH_IDLE_COUNT)
#error "WCO busy refresh is less than idle refresh."
#endif
#if (REPORT_OVR_REFRESH_BUSY_COUNT < REPORT_OVR_REFRESH_IDLE_COUNT)
#error "Override busy refresh is less than idle refresh."
#endif
#if (REPORT_WCO_REFRESH_IDLE_COUNT < 2)
#error "WCO refresh must be greater than one."
#endif
#if (REPORT_OVR_REFRESH_IDLE_COUNT < 1)
#error "Override refresh must be greater than zero."
#endif

#if defined(ENABLE_DUAL_AXIS)
#if !((DUAL_AXIS_SELECT == X_AXIS) || (DUAL_AXIS_SELECT == Y_AXIS))
#error "Dual axis currently supports X or Y axes only."
#endif
#if defined(DUAL_AXIS_CONFIG_CNC_SHIELD_CLONE) && defined(VARIABLE_SPINDLE)
#error "VARIABLE_SPINDLE not supported with DUAL_AXIS_CNC_SHIELD_CLONE."
#endif
#if defined(DUAL_AXIS_CONFIG_CNC_SHIELD_CLONE) && defined(DUAL_AXIS_CONFIG_PROTONEER_V3_51)
#error "More than one dual axis configuration found. Select one."
#endif
#if !defined(DUAL_AXIS_CONFIG_CNC_SHIELD_CLONE) && !defined(DUAL_AXIS_CONFIG_PROTONEER_V3_51)
#error "No supported dual axis configuration found. Select one."
#endif
#if defined(COREXY)
#error "CORE XY not supported with dual axis feature."
#endif
#if defined(USE_SPINDLE_DIR_AS_ENABLE_PIN)
#error "USE_SPINDLE_DIR_AS_ENABLE_PIN not supported with dual axis feature."
#endif
#if defined(ENABLE_M7)
#error "ENABLE_M7 not supported with dual axis feature."
#endif
#endif

// ---------------------------------------------------------------------------------------

#endif
