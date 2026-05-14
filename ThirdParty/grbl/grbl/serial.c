/*
  serial.c - Low level functions for sending and recieving bytes via the serial port
  Part of Grbl

  Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

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

#define RX_RING_BUFFER (RX_BUFFER_SIZE + 1)
#define TX_RING_BUFFER (TX_BUFFER_SIZE + 1)
#define NET_TX_BUFFER_SIZE 1024

uint8_t serial_rx_buffer[RX_RING_BUFFER];
volatile uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_RING_BUFFER];
volatile uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;

#if defined(ZEPHYR_ARCH)
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>
#include <limits.h>
#include "serial.h"

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

#define UART_DEVICE_NODE DT_NODELABEL(usart3)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

void serial_glue_callback(const struct device *dev, void *user_data); // Forward declaration for the glue callback function.

static uint8_t serial_net_tx_buffer[NET_TX_BUFFER_SIZE];
static volatile uint16_t serial_net_tx_head = 0;
static volatile uint16_t serial_net_tx_tail = 0;
K_SEM_DEFINE(serial_net_tx_sem, 0, 1);

void serial_tx_notify(void)
{
  k_sem_give(&serial_net_tx_sem);
}

void serial_net_tx_wait(void)
{
  k_sem_take(&serial_net_tx_sem, K_FOREVER);
}

int serial_net_tx_pop(unsigned char *data)
{
  uint16_t tail = serial_net_tx_tail;
  if (serial_net_tx_head == tail)
  {
    return 0;
  }

  *data = serial_net_tx_buffer[tail];
  tail++;
  if (tail == NET_TX_BUFFER_SIZE)
  {
    tail = 0;
  }
  serial_net_tx_tail = tail;
  return 1;
}

#endif

// Returns the number of bytes available in the RX serial buffer.
uint8_t serial_get_rx_buffer_available()
{
  uint8_t rtail = serial_rx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_rx_buffer_head >= rtail)
  {
    return (RX_BUFFER_SIZE - (serial_rx_buffer_head - rtail));
  }
  return ((rtail - serial_rx_buffer_head - 1));
}

// Returns the number of bytes used in the RX serial buffer.
// NOTE: Deprecated. Not used unless classic status reports are enabled in config.h.
uint8_t serial_get_rx_buffer_count()
{
  uint8_t rtail = serial_rx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_rx_buffer_head >= rtail)
  {
    return (serial_rx_buffer_head - rtail);
  }
  return (RX_BUFFER_SIZE - (rtail - serial_rx_buffer_head));
}

// Returns the number of bytes used in the TX serial buffer.
// NOTE: Not used except for debugging and ensuring no TX bottlenecks.
uint8_t serial_get_tx_buffer_count()
{
  uint8_t ttail = serial_tx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_tx_buffer_head >= ttail)
  {
    return (serial_tx_buffer_head - ttail);
  }
  return (TX_RING_BUFFER - (ttail - serial_tx_buffer_head));
}

void serial_init()
{
#if defined(AVR_ARCH)
// Set baud rate
#if BAUD_RATE < 57600
  uint16_t UBRR0_value = ((F_CPU / (8L * BAUD_RATE)) - 1) / 2;
  UCSR0A &= ~(1 << U2X0); // baud doubler off  - Only needed on Uno XXX
#else
  uint16_t UBRR0_value = ((F_CPU / (4L * BAUD_RATE)) - 1) / 2;
  UCSR0A |= (1 << U2X0); // baud doubler on for high baud rates, i.e. 115200
#endif
  UBRR0H = UBRR0_value >> 8;
  UBRR0L = UBRR0_value;

  // enable rx, tx, and interrupt on complete reception of a byte
  UCSR0B |= (1 << RXEN0 | 1 << TXEN0 | 1 << RXCIE0);

  // defaults to 8-bit, no parity, 1 stop bit
#elif defined(ZEPHYR_ARCH)
  // clear ring buffer
  serial_rx_buffer_head = 0;
  serial_rx_buffer_tail = 0;
  serial_tx_buffer_head = 0;
  serial_tx_buffer_tail = 0;

  if (!device_is_ready(uart_dev))
  {
    return;
  }

  // Set callback and enable reception
  uart_irq_callback_user_data_set(uart_dev, serial_glue_callback, NULL);
  uart_irq_rx_enable(uart_dev);
#endif // ZEPHYR_ARCH
}

// Writes one byte to the TX serial buffer. Called by main program.
void serial_write(uint8_t data)
{
  /*if (serialTaskHandle == NULL)
  {
    return;
  } // Serial task not initialized yet*/

  // Calculate next head
  uint8_t next_head = serial_tx_buffer_head + 1;
  if (next_head == TX_RING_BUFFER)
  {
    next_head = 0;
  }

  // Wait until there is space in the buffer
  while (next_head == serial_tx_buffer_tail)
  {
    // TODO: Restructure st_prep_buffer() calls to be executed here during a long print.
    if (sys_rt_exec_state & EXEC_RESET)
    {
      return;
    } // Only check for abort to avoid an endless loop.
  }

  // Store data and advance head
  serial_tx_buffer[serial_tx_buffer_head] = data;
  serial_tx_buffer_head = next_head;

#if defined(AVR_ARCH)
  // Enable Data Register Empty Interrupt to make sure tx-streaming is running
  UCSR0B |= (1 << UDRIE0);
#elif defined(ZEPHYR_ARCH)
  uart_irq_tx_enable(uart_dev); // Start sending data from the buffer

  // Mirror the data to the network TX buffer for the TCP TX thread to send back to the client.
  uint16_t next_net_head = serial_net_tx_head + 1;
  if (next_net_head == NET_TX_BUFFER_SIZE)
  {
    next_net_head = 0;
  }
  if (next_net_head != serial_net_tx_tail)
  {
    serial_net_tx_buffer[serial_net_tx_head] = data;
    serial_net_tx_head = next_net_head;
    serial_tx_notify();
  }
#endif // AVR_ARCH
}

#if defined(AVR_ARCH)
// Data Register Empty Interrupt handler
ISR(SERIAL_UDRE)
{
#elif defined(ZEPHYR_ARCH)
uint8_t serial_tx_irq()
{
  uint8_t UDR0;
#endif // AVR_ARCH

  uint8_t tail = serial_tx_buffer_tail; // Temporary serial_tx_buffer_tail (to optimize for volatile)

  // Send a byte from the buffer
  UDR0 = serial_tx_buffer[tail];

  // Update tail position
  tail++;
  if (tail == TX_RING_BUFFER)
  {
    tail = 0;
  }
  serial_tx_buffer_tail = tail;

#if defined(AVR_ARCH)
  // Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
  if (tail == serial_tx_buffer_head)
  {
    UCSR0B &= ~(1 << UDRIE0);
  }
#elif defined(ZEPHYR_ARCH)
  return UDR0;
#endif // AVR_ARCH
}

// Fetches the first byte in the serial read buffer. Called by main program.
uint8_t serial_read()
{
  uint8_t tail = serial_rx_buffer_tail; // Temporary serial_rx_buffer_tail (to optimize for volatile)
  if (serial_rx_buffer_head == tail)
  {
    return SERIAL_NO_DATA;
  }
  else
  {
    uint8_t data = serial_rx_buffer[tail];

    tail++;
    if (tail == RX_RING_BUFFER)
    {
      tail = 0;
    }
    serial_rx_buffer_tail = tail;

    return data;
  }
}

#if defined(AVR_ARCH)
ISR(SERIAL_RX)
{
  uint8_t data = UDR0;
#elif defined(ZEPHYR_ARCH)
void serial_rx_irq(uint8_t data)
{
#endif
  uint8_t next_head;

  // Pick off realtime command characters directly from the serial stream. These characters are
  // not passed into the main buffer, but these set system state flag bits for realtime execution.
  switch (data)
  {
  case CMD_RESET:
    mc_reset();
    break; // Call motion control reset routine.
  case CMD_STATUS_REPORT:
    system_set_exec_state_flag(EXEC_STATUS_REPORT);
    break; // Set as true
  case CMD_CYCLE_START:
    system_set_exec_state_flag(EXEC_CYCLE_START);
    break; // Set as true
  case CMD_FEED_HOLD:
    system_set_exec_state_flag(EXEC_FEED_HOLD);
    break; // Set as true
  default:
    if (data > 0x7F)
    { // Real-time control characters are extended ACSII only.
      switch (data)
      {
      case CMD_SAFETY_DOOR:
        system_set_exec_state_flag(EXEC_SAFETY_DOOR);
        break; // Set as true
      case CMD_JOG_CANCEL:
        if (sys.state & STATE_JOG)
        { // Block all other states from invoking motion cancel.
          system_set_exec_state_flag(EXEC_MOTION_CANCEL);
        }
        break;
#if defined(ZEPHYR_ARCH)
      case CMD_IO_STATUS_REPORT:
        system_set_exec_user_defined_flag(EXEC_IO_STATUS_REPORT);
        break;
#endif
#ifdef DEBUG
#if defined(AVR_ARCH)
      case CMD_DEBUG_REPORT:
      {
        uint8_t sreg = SREG;
        cli();
        bit_true(sys_rt_exec_debug, EXEC_DEBUG_REPORT);
        SREG = sreg;
      }
      break;
#elif defined(ZEPHYR_ARCH)
      case CMD_DEBUG_REPORT:
      {
        __disable_irq();
        bit_true(sys_rt_exec_debug, EXEC_DEBUG_REPORT);
        __enable_irq();
      }
      break;
#endif
#endif
      case CMD_FEED_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_RESET);
        break;
      case CMD_FEED_OVR_COARSE_PLUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS);
        break;
      case CMD_FEED_OVR_COARSE_MINUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS);
        break;
      case CMD_FEED_OVR_FINE_PLUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_PLUS);
        break;
      case CMD_FEED_OVR_FINE_MINUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_MINUS);
        break;
      case CMD_RAPID_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_RESET);
        break;
      case CMD_RAPID_OVR_MEDIUM:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_MEDIUM);
        break;
      case CMD_RAPID_OVR_LOW:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_LOW);
        break;
      case CMD_SPINDLE_OVR_RESET:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_RESET);
        break;
      case CMD_SPINDLE_OVR_COARSE_PLUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_PLUS);
        break;
      case CMD_SPINDLE_OVR_COARSE_MINUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_MINUS);
        break;
      case CMD_SPINDLE_OVR_FINE_PLUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_PLUS);
        break;
      case CMD_SPINDLE_OVR_FINE_MINUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_MINUS);
        break;
      case CMD_SPINDLE_OVR_STOP:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_STOP);
        break;
      case CMD_COOLANT_FLOOD_OVR_TOGGLE:
        system_set_exec_accessory_override_flag(EXEC_COOLANT_FLOOD_OVR_TOGGLE);
        break;
#ifdef ENABLE_M7
      case CMD_COOLANT_MIST_OVR_TOGGLE:
        system_set_exec_accessory_override_flag(EXEC_COOLANT_MIST_OVR_TOGGLE);
        break;
#endif
      }
      // Throw away any unfound extended-ASCII character by not passing it to the serial buffer.
    }
    else
    { // Write character to buffer
      next_head = serial_rx_buffer_head + 1;
      if (next_head == RX_RING_BUFFER)
      {
        next_head = 0;
      }

      // Write data to buffer unless it is full.
      if (next_head != serial_rx_buffer_tail)
      {
        serial_rx_buffer[serial_rx_buffer_head] = data;
        serial_rx_buffer_head = next_head;
      }
    }
  }
}

void serial_reset_read_buffer()
{
  serial_rx_buffer_tail = serial_rx_buffer_head;
}

// Glue callback for the Zephyr UART driver.
// This function is called by the UART ISR.
#if defined(ZEPHYR_ARCH)
void serial_glue_callback(const struct device *dev, void *user_data)
{
  uart_irq_update(dev);

  // Handle incoming data (RX).
  while (1)
  {
    uint8_t rx_temp_buf[64];
    // Read from the UART FIFO into a temporary buffer.
    int len = uart_fifo_read(dev, rx_temp_buf, sizeof(rx_temp_buf));

    if (len > 0)
    {
      // Process each received byte.
      for (int i = 0; i < len; i++)
      {
        serial_rx_irq(rx_temp_buf[i]);
      }
    }
    else
    {
      break;
    }
  }

  // Handle outgoing data (TX) if the transmitter is ready.
  if (uart_irq_tx_ready(dev))
  {
    // Transmit data from the software buffer until it's empty or the FIFO is full.
    while (serial_tx_buffer_head != serial_tx_buffer_tail)
    {
      uint8_t tail = serial_tx_buffer_tail;
      uint8_t data = serial_tx_buffer[tail];

      // Fill the UART FIFO with one byte.
      int sent = uart_fifo_fill(dev, &data, 1);

      if (sent > 0)
      {
        tail++;
        if (tail == TX_RING_BUFFER)
        {
          tail = 0;
        }
        serial_tx_buffer_tail = tail;
      }
      else
      {
        break;
      }
    }

    if (serial_tx_buffer_head == serial_tx_buffer_tail) // If the buffer is empty, disable the TX interrupt to prevent it from firing constantly.
    {
      uart_irq_tx_disable(dev);
    }
  }
}
#endif