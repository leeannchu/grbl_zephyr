# GRBL v1.1 on Zephyr (STM32F7)

## Project Summary
This project ports **GRBL v1.1** from its original **FreeRTOS-based** implementation to **Zephyr RTOS**, and runs it on an **STM32F767ZI** target.

Original project reference:
- https://github.com/haymanmk/stm32f7xx_grbl

## Environment
- Zephyr OS: v4.4 (v3.7 showed performance limitations)
- Zephyr SDK: v1.0.1

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
|       |-- main.c                          # Zephyr app entry, creates/synchronizes GRBL thread (Zephyr-native integration)
|       |-- step.c                          # Step/dir control logic (ported from STM32 HAL)
|       |-- encoder.c                       # Encoder logic (currently HAL-style; Zephyr migration pending)
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
- TCP communication stack is not integrated yet. Reserve a module path such as `app/comms/tcp/` (or `Core/Src/comms_tcp.c`) for host communication.
- Encoder is not migrated to a Zephyr-native driver/API yet. Current code remains HAL-style in `Core/Src/encoder.c`; plan to move toward Zephyr GPIO/Counter-based implementation.

### Documentation Plan
- This README focuses on architecture and module responsibilities.
- File-by-file implementation changes will be documented separately in a technical supplement.
