# GRBL v1.1 on Zephyr (STM32F7)

## Project Summary
This project ports **GRBL v1.1** from its original **FreeRTOS-based** implementation to **Zephyr RTOS**, and runs it on an **STM32F767ZI** target.

Original project reference:
- https://github.com/haymanmk/stm32f7xx_grbl

## Environment
- Zephyr OS: v4.4 (v3.7 showed performance limitations)
- Zephyr SDK: v1.0.1

## Prerequisites
- MCU Configuration: Ensure the STM32 flash is set to **Dual Bank Mode** (via `nDBANK` Option Byte).
- Rationale: This enables 16KB sector sizes (depending on bank), allowing the NVS storage partition to function correctly with the provided DeviceTree overlay.

## Build
Build the project with:

```bash
west build -p always -b nucleo_f767zi
```

The compiled binary is located in `build/zephyr/zephyr.elf`.

## Debug
Use STM32CubeIDE to load and debug the binary on the target board.

## Project Structure

```text
grbl_zephyr/
|-- Core/                                   # STM32 HAL-ported application layer
|   |-- Inc/                                # Headers for app glue, step, encoder, timer extension
|   `-- Src/
|       |-- main.c                          # Zephyr app entry, creates GRBL/TCP/encoder threads
|       |-- step.c                          # Step/dir control logic (ported from STM32 HAL)
|       |-- encoder.c                       # Encoder polling and position conversion
|       |-- tcp_rx.c                        # TCP receive path for GRBL commands
|       |-- tcp_tx.c                        # TCP transmit path for GRBL responses
|       `-- stm32f7xx_timer_extension.c     # STM32F7 timer extension utilities (ported from STM32 HAL)
|
|-- drivers/                                # Custom Zephyr driver layer
|   |-- grbl_stepper_controller.c           # Stepper device driver implementation (Zephyr-native)
|   `-- grbl_stepper_controller.h           # Driver API/header
|
|-- ThirdParty/
|   `-- grbl/
|       `-- grbl/                           # GRBL v1.1 core sources, adapted in multiple files for Zephyr integration
|
|-- boards/
|   `-- nucleo_f767zi.overlay               # Device Tree overlay: pin aliases and board wiring (Zephyr-native)
|
|-- dts/
|   `-- bindings/                           # Custom Device Tree bindings
|
|-- CMakeLists.txt                          # Zephyr build entry and source wiring (Zephyr-native)
|-- prj.conf                                # Zephyr Kconfig project options (Zephyr-native)
`-- README.md
```

### Reserved / Planned Structure
- TCP communication is now integrated in `Core/Src/tcp_rx.c` and `Core/Src/tcp_tx.c`.
- Encoder is still under validation. Current code in `Core/Src/encoder.c` uses a polling-based approach and needs hardware verification for reverse/count behavior.
- Follow-up plan: confirm encoder direction/counting under real motion and which parts have not yet been migrated.

### Documentation Plan
- This README focuses on the current architecture and module responsibilities.
- File-by-file implementation changes and migration notes will be documented separately in technical supplements.
