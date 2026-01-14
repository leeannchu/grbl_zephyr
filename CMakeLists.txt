cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f767zi.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(grbl_zephyr)

target_include_directories(app PRIVATE
    Core/Inc               
    ThirdParty/grbl/grbl   
)

file(GLOB GRBL_SOURCES "ThirdParty/grbl/grbl/*.c")

list(REMOVE_ITEM GRBL_SOURCES
    #"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/grbl/grbl/stepper.c"          
    #"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/grbl/grbl/spindle_control.c" 
    #"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/grbl/grbl/eeprom.c"         
)

target_sources(app PRIVATE 
  Core/Src/main.c
  ${GRBL_SOURCES}
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