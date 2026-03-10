cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(grbl_zephyr)

set(GRBL_DIR "ThirdParty/grbl/grbl")
set(CORE_DIR "Core")

target_include_directories(app PRIVATE
    ${CORE_DIR}/Inc               
    ${GRBL_DIR}
    drivers   
)

target_sources(app PRIVATE
    ${CORE_DIR}/Src/main.c
    ${CORE_DIR}/Src/step.c
    drivers/grbl_stepper_controller.c

    ${GRBL_DIR}/coolant_control.c
    ${GRBL_DIR}/eeprom.c
    ${GRBL_DIR}/gcode.c
    ${GRBL_DIR}/io.c
    ${GRBL_DIR}/jog.c 
    ${GRBL_DIR}/limits.c
    ${GRBL_DIR}/main.c
    ${GRBL_DIR}/motion_control.c 
    ${GRBL_DIR}/nuts_bolts.c
    ${GRBL_DIR}/planner.c
    ${GRBL_DIR}/print.c  
    ${GRBL_DIR}/probe.c   
    ${GRBL_DIR}/protocol.c 
    ${GRBL_DIR}/report.c  
    ${GRBL_DIR}/serial.c  
    ${GRBL_DIR}/settings.c 
    ${GRBL_DIR}/spindle_control.c  
    ${GRBL_DIR}/stepper.c  
    ${GRBL_DIR}/system.c     
)

target_compile_options(app PRIVATE
    -Wno-unused-result
    -Wno-pointer-sign
    -Wno-double-promotion
    -Wno-error=double-promotion
    -Wno-implicit-int
    -Wno-implicit-function-declaration
)

target_compile_definitions(app PRIVATE
    ZEPHYR_ARCH=1
)