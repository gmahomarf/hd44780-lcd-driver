cmake_minimum_required(VERSION 4.2)

set(MODULE_VERSION 0.0.1)
set(MODULE_NAME "hd44780_lcd")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ARCH arm)
set(CROSS_COMPILE arm-linux-gnueabihf-)

set(CACHE{KERNEL_DIR}
        TYPE PATH
        HELP "Base directory for the linux headers (e.g., /lib/modules/$(uname -r)/build)"
        VALUE ""
)

add_definitions(-D__KERNEL__ -DMODULE -DKBUILD_MODNAME="${MODULE_NAME}")

if ("${KERNEL_DIR}" STREQUAL "")
    execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Kernel version: ${KERNEL_VERSION}")
    set(KERNEL_DIR "/lib/modules/${KERNEL_VERSION}/build")
endif ()

if (NOT IS_READABLE ${KERNEL_DIR})
    message(FATAL_ERROR "\nDirectory ${KERNEL_DIR} does not exist or is not readable")
endif ()

message(STATUS "Kernel dir: ${KERNEL_DIR}")

project(${MODULE_NAME}
        VERSION ${MODULE_VERSION}
        LANGUAGES C
)

set(DRIVER_BASE module/${MODULE_NAME})
set(DRIVER_FILE ${DRIVER_BASE}.ko)
set(KBUILD_CMD make
        $<IF:$<STREQUAL:${ARCH},>,,ARCH=${ARCH}>
        $<IF:$<STREQUAL:${CROSS_COMPILE},>,,CROSS_COMPILE=${CROSS_COMPILE}>
        -C "${KERNEL_DIR}"
        modules
        M=${CMAKE_CURRENT_SOURCE_DIR}
        MO=${CMAKE_CURRENT_BINARY_DIR}/module
)
set(DTC_BUILD_CMD
        dtc
        -@
        -I dts
        -O dtb
        -o src/lcd-gpio.dtbo
        src/lcd-gpio.dts
)

message(DEBUG "Kbuild cmd: ${KBUILD_CMD}")

add_custom_command(
        OUTPUT src/lcd-gpio.dtbo
        COMMAND ${DTC_BUILD_CMD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS src/lcd-gpio.dts
        VERBATIM
)

add_custom_command(
        OUTPUT src/${MODULE_NAME}-dtbo.c
        COMMAND sh -c "xxd -i -n lcd_dtbo src/lcd-gpio.dtbo | sed 's#\\[\\] =#[] __attribute__ ((aligned (8))) =#' > src/${MODULE_NAME}-dtbo.c"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS src/lcd-gpio.dtbo
        VERBATIM
)

set(DRIVER_SOURCES
        src/${MODULE_NAME}.c
        src/${MODULE_NAME}-cdev.c
        src/${MODULE_NAME}-ctrl.c
        src/${MODULE_NAME}-driver.c
        src/${MODULE_NAME}-dtbo.c
)

add_custom_command(
        OUTPUT
        ${DRIVER_FILE}
        ${DRIVER_BASE}.o
        ${DRIVER_BASE}.mod
        ${DRIVER_BASE}.mod.o
        module/.${MODULE_NAME}.ko.cmd
        module/.${MODULE_NAME}.o.cmd
        module/.${MODULE_NAME}.mod.cmd
        module/.${MODULE_NAME}.mod.o.cmd
        ${DRIVER_BASE}.mod.c
        COMMAND ${KBUILD_CMD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${DRIVER_SOURCES}
        VERBATIM
)

# Parse c files
add_library(dummy_target
        ${DRIVER_SOURCES}
)

add_custom_target(driver ALL
        DEPENDS
        ${DRIVER_FILE}
)

# Add header files
include_directories(
        "${KERNEL_DIR}/include"
        "${KERNEL_DIR}/arch/${ARCH}/include/generated"
        "${KERNEL_DIR}/arch/${ARCH}/include"
)

message(DEBUG "Sources: ${DRIVER_SOURCES}")
set(KBUILD_SOURCE "obj-m := ${MODULE_NAME}.o")
foreach (DRIVER_SOURCE ${DRIVER_SOURCES})
    get_filename_component(_BASENAME ${DRIVER_SOURCE} NAME_WE)
    get_filename_component(_DIRNAME ${DRIVER_SOURCE} DIRECTORY)

    set(KBUILD_SOURCE "${KBUILD_SOURCE}
${MODULE_NAME}-y += ${_DIRNAME}/${_BASENAME}.o"
    )
endforeach ()

message(DEBUG "KBUILD_SOURCE: ${KBUILD_SOURCE}")

# Generate the KBuild file through cmake.
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild
        ${KBUILD_SOURCE})
#file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild
#        "obj-m := ${MODULE_NAME}.o
#${MODULE_NAME}-y += src/${MODULE_NAME}.o
#${MODULE_NAME}-y += src/${MODULE_NAME}-cdev.o
#${MODULE_NAME}-y += src/${MODULE_NAME}-driver.o
#")