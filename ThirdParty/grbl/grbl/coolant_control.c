/*
  coolant_control.c - coolant control methods
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC

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

#include "grbl.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#define FLOOD_NODE DT_ALIAS(coolant)

#if DT_NODE_HAS_STATUS(FLOOD_NODE, okay)
static const struct gpio_dt_spec flood_gpio = GPIO_DT_SPEC_GET(FLOOD_NODE, gpios);
#define FLOOD_PIN_PRESENT
#endif

#define MIST_NODE DT_ALIAS(coolant_mist)
#if DT_NODE_HAS_STATUS(MIST_NODE, okay)
static const struct gpio_dt_spec mist_gpio = GPIO_DT_SPEC_GET(MIST_NODE, gpios);
#define MIST_PIN_PRESENT
#endif

void coolant_init()
{
#ifdef AVR_ARCH
  COOLANT_FLOOD_DDR |= (1 << COOLANT_FLOOD_BIT); // Configure as output pin
#ifdef ENABLE_M7
  COOLANT_MIST_DDR |= (1 << COOLANT_MIST_BIT);
#endif
#endif // AVR_ARCH

#ifdef ZEPHYR_ARCH
#ifdef FLOOD_PIN_PRESENT
  if (gpio_is_ready_dt(&flood_gpio))
  {
    gpio_pin_configure_dt(&flood_gpio, GPIO_OUTPUT_INACTIVE);
  }
#endif

#ifdef MIST_PIN_PRESENT
#ifdef ENABLE_M7
  if (gpio_is_ready_dt(&mist_gpio))
  {
    gpio_pin_configure_dt(&mist_gpio, GPIO_OUTPUT_INACTIVE);
  }
#endif
#endif
#endif // ZEPHYR_ARCH

  coolant_stop();
}

// Returns current coolant output state. Overrides may alter it from programmed state.
uint8_t coolant_get_state()
{
  uint8_t cl_state = COOLANT_STATE_DISABLE;
#ifdef AVR_ARCH
#ifdef INVERT_COOLANT_FLOOD_PIN
  if (bit_isfalse(COOLANT_FLOOD_PORT, (1 << COOLANT_FLOOD_BIT)))
  {
#else
  if (bit_istrue(COOLANT_FLOOD_PORT, (1 << COOLANT_FLOOD_BIT)))
  {
#endif
    cl_state |= COOLANT_STATE_FLOOD;
  }
#ifdef ENABLE_M7
#ifdef INVERT_COOLANT_MIST_PIN
  if (bit_isfalse(COOLANT_MIST_PORT, (1 << COOLANT_MIST_BIT)))
  {
#else
  if (bit_istrue(COOLANT_MIST_PORT, (1 << COOLANT_MIST_BIT)))
  {
#endif
    cl_state |= COOLANT_STATE_MIST;
  }
#endif
  return (cl_state);
#endif // AVR_ARCH

#ifdef ZEPHYR_ARCH
#ifdef FLOOD_PIN_PRESENT
  if (gpio_pin_get_dt(&flood_gpio))
  {
    cl_state |= COOLANT_STATE_FLOOD;
  }
#endif

#ifdef MIST_PIN_PRESENT
#ifdef ENABLE_M7
  if (gpio_pin_get_dt(&mist_gpio))
  {
    cl_state |= COOLANT_STATE_MIST;
  }
#endif
#endif

  return cl_state;
#endif // ZEPHYR_ARCH
}

// Directly called by coolant_init(), coolant_set_state(), and mc_reset(), which can be at
// an interrupt-level. No report flag set, but only called by routines that don't need it.
void coolant_stop()
{
#ifdef AVR_ARCH
#ifdef INVERT_COOLANT_FLOOD_PIN
  COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#else
  COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#endif
#ifdef ENABLE_M7
#ifdef INVERT_COOLANT_MIST_PIN
  COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#else
  COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#endif
#endif
#endif // AVR_ARCH

#ifdef ZEPHYR_ARCH
#ifdef FLOOD_PIN_PRESENT
  gpio_pin_set_dt(&flood_gpio, 0);
#endif

#ifdef MIST_PIN_PRESENT
#ifdef ENABLE_M7
  gpio_pin_set_dt(&mist_gpio, 0);
#endif
#endif
#endif // ZEPHYR_ARCH
}

// Main program only. Immediately sets flood coolant running state and also mist coolant,
// if enabled. Also sets a flag to report an update to a coolant state.
// Called by coolant toggle override, parking restore, parking retract, sleep mode, g-code
// parser program end, and g-code parser coolant_sync().
void coolant_set_state(uint8_t mode)
{
  if (sys.abort)
  {
    return;
  } // Block during abort.

#ifdef AVR_ARCH
  if (mode & COOLANT_FLOOD_ENABLE)
  {
#ifdef INVERT_COOLANT_FLOOD_PIN
    COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#else
    COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#endif
  }
  else
  {
#ifdef INVERT_COOLANT_FLOOD_PIN
    COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#else
    COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#endif
  }

#ifdef ENABLE_M7
  if (mode & COOLANT_MIST_ENABLE)
  {
#ifdef INVERT_COOLANT_MIST_PIN
    COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#else
    COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#endif
  }
  else
  {
#ifdef INVERT_COOLANT_MIST_PIN
    COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#else
    COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#endif
  }
#endif
#endif // AVR_ARCH

#ifdef ZEPHYR_ARCH
#ifdef FLOOD_PIN_PRESENT
  if (mode & COOLANT_FLOOD_ENABLE)
  {
    gpio_pin_set_dt(&flood_gpio, 1);
  }
  else
  {
    gpio_pin_set_dt(&flood_gpio, 0);
  }
#endif

#ifdef MIST_PIN_PRESENT
#ifdef ENABLE_M7
  if (mode & COOLANT_MIST_ENABLE)
  {
    gpio_pin_set_dt(&mist_gpio, 1);
  }
  else
  {
    gpio_pin_set_dt(&mist_gpio, 0);
  }
#endif
#endif

#endif // ZEPHYR_ARCH

  sys.report_ovr_counter = 0; // Set to report change immediately
}

// G-code parser entry-point for setting coolant state. Forces a planner buffer sync and bails
// if an abort or check-mode is active.
void coolant_sync(uint8_t mode)
{
  if (sys.state == STATE_CHECK_MODE)
  {
    return;
  }
  protocol_buffer_synchronize(); // Ensure coolant turns on when specified in program.
  coolant_set_state(mode);
}
