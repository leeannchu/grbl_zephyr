## Overview

This document summarizes what was changed in GRBL when moving to Zephyr RTOS, mainly in hardware control code, interrupt handling, and delay behavior.

---

## File-by-File Changes

### 1. serial.c
- Previous FreeRTOS/HAL: UART RX/TX handled through HAL IRQ or DMA flow.
- Current Zephyr: UART RX/TX handled through `uart_irq_callback_user_data_set()`, `uart_irq_rx_enable()`, `uart_irq_tx_enable()`, and FIFO read/write APIs.
- Main change: serial path is now driver-callback based and configured through device tree.

### 2. eeprom.c
- Previous FreeRTOS/HAL: persistence was handled by project-level flash access/emulation code, with write timing and data layout managed in application logic.
- Current Zephyr: `check_init()` lazily mounts NVS on first access, loads one full 1 KB image (`NVS_ID_1`) into `eeprom_ram`, serves reads from RAM (`eeprom_get_char`), and writes back the full image through `nvs_write()` when data changes.
- Main change: storage behavior is now a RAM-mirror plus NVS snapshot model (single record image), replacing custom flash-management flow with Zephyr NVS APIs and built-in wear leveling.

### 3. motion_control.c
- Previous FreeRTOS/HAL: dwell used a software timer flow (`xTimerCreate`, `xTimerStart`, timer callback) plus `EXEC_DWELL` user-defined flags to mark dwell state and completion.
- Current Zephyr: dwell is handled directly inside `mc_dwell()` using `k_uptime_get()` for end-time calculation, `protocol_execute_realtime()` in-loop, and `k_msleep(1)` for cooperative waiting.
- Main change: timer-callback/flag-based dwell control was removed and replaced by a deterministic 1 ms scheduler-friendly loop that keeps realtime command processing active during dwell.

### 4. limits.c
- Previous FreeRTOS/HAL: limit events were received through HAL EXTI callback flow, then passed into GRBL alarm/homing logic.
- Current Zephyr: limit pins are registered through `gpio_pin_interrupt_configure_dt()` and `gpio_callback`; when an edge is triggered, `limits_callback()` raises the hard-limit/alarm path and updates axis-trigger state used during homing.
- Main change: the limit path is now hardware-triggered at GPIO interrupt level and immediately feeds the GRBL homing protection path, instead of relying on task-level polling behavior.
- Detail reference: this hardware-triggered limit flow and homing safety integration are documented in [docs/stepper-architecture.md](docs/stepper-architecture.md), section "Hardware-Triggered Limits and Homing Safety".
- Related modified files for this path: [ThirdParty/grbl/grbl/limits.c](ThirdParty/grbl/grbl/limits.c) and [Core/Src/step.c](Core/Src/step.c).

### 5. probe.c
- Previous FreeRTOS/HAL: probe pin setup/read used HAL GPIO init and `HAL_GPIO_ReadPin()` style access.
- Current Zephyr: probe pin setup/read uses `gpio_pin_configure_dt()` and `gpio_pin_get_dt()`.
- Main change: probe pin control is now device-tree driven with Zephyr GPIO APIs.

### 6. coolant_control.c
- Previous FreeRTOS/HAL: coolant output pins controlled through HAL GPIO write calls with board-specific polarity handling.
- Current Zephyr: coolant output pins controlled through `gpio_pin_set_dt()` with polarity defined in device tree.
- Main change: output control logic is simplified and board polarity is moved into DTS configuration.

### 7. io.c
- Previous FreeRTOS/HAL: IO pin mapping and init were tied to HAL pin definitions and manual per-pin setup.
- Current Zephyr: IO pins are declared as `gpio_dt_spec` entries and configured by `gpio_pin_configure_dt()`.
- Main change: pin mapping is centralized in device tree and reused by one API style.

### 8. system.c
- Previous FreeRTOS/HAL: control inputs (reset/feed-hold/cycle-start) were managed by HAL EXTI configuration and callbacks.
- Current Zephyr: control inputs use per-pin `gpio_callback`, `gpio_init_callback()`, `gpio_add_callback()`, and Zephyr interrupt configuration.
- Main change: control signal interrupts are now managed uniformly by Zephyr GPIO callback flow.
- Additional change in the same file: realtime shared-state protection moved from FreeRTOS/IRQ masking style critical sections to `irq_lock()` / `irq_unlock()` for Zephyr-consistent locking.

### 9. main.c
- Previous FreeRTOS/HAL: startup sequence integrated with legacy RTOS application entry and init flow.
- Current Zephyr: GRBL entry is adapted to Zephyr application flow (`mainGRBL`) with Zephyr-side peripheral integration hooks.
- Main change: boot and integration path now follows Zephyr runtime model.

### Cross-Document Detail Notes
- Detailed step pulse architecture and ISR pipeline changes for [ThirdParty/grbl/grbl/stepper.c](ThirdParty/grbl/grbl/stepper.c) are documented in [docs/stepper-architecture.md](docs/stepper-architecture.md).
- Detailed spindle-control related migration notes for [ThirdParty/grbl/grbl/spindle_control.c](ThirdParty/grbl/grbl/spindle_control.c) are also referenced in [docs/stepper-architecture.md](docs/stepper-architecture.md).

---

## Key Files Modified

- [serial.c](../ThirdParty/grbl/grbl/serial.c)
- [eeprom.c](../ThirdParty/grbl/grbl/eeprom.c)
- [motion_control.c](../ThirdParty/grbl/grbl/motion_control.c)
- [limits.c](../ThirdParty/grbl/grbl/limits.c)
- [probe.c](../ThirdParty/grbl/grbl/probe.c)
- [coolant_control.c](../ThirdParty/grbl/grbl/coolant_control.c)
- [io.c](../ThirdParty/grbl/grbl/io.c)
- [system.c](../ThirdParty/grbl/grbl/system.c)
- [main.c](../ThirdParty/grbl/grbl/main.c)
- [stepper.c](../ThirdParty/grbl/grbl/stepper.c) (detailed notes in [docs/stepper-architecture.md](docs/stepper-architecture.md))
- [spindle_control.c](../ThirdParty/grbl/grbl/spindle_control.c) (detailed notes in [docs/stepper-architecture.md](docs/stepper-architecture.md))

---

## Notable Decisions

1. **RAM Caching for EEPROM**: Trade RAM (1KB buffer) for performance (no polling waits)
2. **NVS Atomicity**: Entire buffer written on any change (vs. per-byte AVR mode)
3. **Device Tree Config**: Eliminates conditional compilation for hardware variations
4. **Per-Pin Callbacks**: Finer interrupt control than monolithic ISR approach
5. **Non-Blocking Dwell**: Enables concurrent serial/encoder handling during delays
