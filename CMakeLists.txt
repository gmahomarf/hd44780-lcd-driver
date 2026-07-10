cmake_minimum_required(VERSION 4.2)

set(MODULE_VERSION 0.0.1)
set(MODULE_NAME "hd44780_lcd")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_GENERATOR Ninja)

project(${MODULE_NAME}
        VERSION ${MODULE_VERSION}
        LANGUAGES C
)

###########################################################
#############     Project setup/config     ################
###########################################################

set(DRIVER_FILE ${MODULE_NAME}.ko)

set(DEVICE_TREE_SOURCE src/lcd-gpio.dts)
set(DEVICE_TREE_BLOB src/lcd-gpio.dtbo)
set(DEVICE_TREE_BLOB_C src/${MODULE_NAME}-dtbo.c)

### List all sources and headers. This ensures modification times are checked before building
set(DRIVER_SOURCES
        src/${MODULE_NAME}.c
        src/${MODULE_NAME}-cdev.c
        src/${MODULE_NAME}-ctrl.c
        src/${MODULE_NAME}-driver.c
        ${DEVICE_TREE_BLOB_C}
)
set(DRIVER_HEADERS
        src/${MODULE_NAME}-cdev.h
        src/${MODULE_NAME}-common.h
        src/${MODULE_NAME}-ctrl.h
        src/${MODULE_NAME}-driver.h
        src/${MODULE_NAME}-dtbo.h
)

set(CACHE{KERNEL_DIR}
        TYPE PATH
        HELP "Base directory for the linux headers (e.g., /lib/modules/$(uname -r)/build)"
        VALUE ""
)
set(CACHE{LCD_LINE_COUNT}
        TYPE STRING
        HELP "The amount of lines that the LCD can display"
        VALUE ""
)
set(CACHE{LCD_LINE_LENGTH}
        TYPE STRING
        HELP "The number of characters per line that the LCD can display"
        VALUE ""
)


###########################################################
#################      KBuild config      #################
###########################################################
set(ARCH arm)
set(CROSS_COMPILE arm-linux-gnueabihf-)

#[[
# This only makes sense when compiling on the board. I don't feel like doing that :)\
# Leaving it here anyways for possible future use
if ("${KERNEL_DIR}" STREQUAL "")
    execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Kernel version: ${KERNEL_VERSION}")
    set(KERNEL_DIR "/lib/modules/${KERNEL_VERSION}/build")
endif ()
]]
if ("${KERNEL_DIR}" STREQUAL "")
    message(FATAL_ERROR "\n Kernel directory not set. Please set `KERNEL_DIR`")
endif ()

if (NOT IS_READABLE ${KERNEL_DIR})
    message(FATAL_ERROR "\nDirectory '${KERNEL_DIR}' does not exist or is not readable")
endif ()

###########################################################
##########     KBuild file generation stuff     ###########
###########################################################
string(JOIN " \\\n\t" DRV_TMP ${DRIVER_SOURCES})
string(REPLACE ".c" ".o" DRV_TMP ${DRV_TMP})

set(KBUILD_SOURCE "obj-m := ${MODULE_NAME}.o
${MODULE_NAME}-y = " ${DRV_TMP})

file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild
        ${KBUILD_SOURCE})

###########################################################
#################     Custom targets     ##################
###########################################################

### Compile Device Tree source to blob
add_custom_target(dtb
        COMMAND
            dtc
            -@
            -I dts
            -O dtb
            -o ${DEVICE_TREE_BLOB}
            ${DEVICE_TREE_SOURCE}
        BYPRODUCTS ${DEVICE_TREE_BLOB}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES ${DEVICE_TREE_SOURCE}
        VERBATIM
)
set_target_properties(dtb
        PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${DEVICE_TREE_BLOB}
)

### Create DTB C source file
#[[
Here we leverage `xxd`s `-i` flag to generates a C file that contains the compiled Device Tree blob (DTB) as an
`unsigned char` array and an `unsigned int` length. The code looks something like this:

    unsigned char __some_name[] = {...};
    unsigned int _some_name_len = ...;

We pipe the output through sed to add a GCC attribute `__attribute__((aligned(8)))` to ensure the DTB is properly
aligned in memory (See https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html#index-aligned for more info)
Per https://github.com/torvalds/linux/blob/0e35b9b6ec0ffcc5e23cbdec09f5c622ad532b53/scripts/dtc/libfdt/fdt.c#L93-L95,
DTB must start at an 8-byte aligned address.

NB: Debugging why the DTB would sometimes pass the header checks and other times wouldn't was... let's say fun.
]]
add_custom_target(dtb_c
        COMMAND sh -c "xxd -i -n lcd_dtbo ${DEVICE_TREE_BLOB} | sed 's#\\[\\] =#[] __attribute__((aligned(8))) =#' > ${DEVICE_TREE_BLOB_C}"
        BYPRODUCTS ${DEVICE_TREE_BLOB_C}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES ${DEVICE_TREE_BLOB}
        VERBATIM
)
add_dependencies(dtb_c dtb)
set_target_properties(dtb_c
        PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${DEVICE_TREE_BLOB_C}
)

### Build the kernel module
set(KCPPFLAGS
        $<$<CONFIG:Debug>:-DDEBUG_LCD=1>
        $<$<NOT:$<STREQUAL:${LCD_LINE_COUNT},>>:-DLCD_LINE_COUNT=${LCD_LINE_COUNT}>
        $<$<NOT:$<STREQUAL:${LCD_LINE_LENGTH},>>:-DLCD_LINE_LENGTH=${LCD_LINE_LENGTH}>
)

add_custom_target(module
        BYPRODUCTS module/${DRIVER_FILE}
        COMMAND make
            ARCH=${ARCH}
            CROSS_COMPILE=${CROSS_COMPILE}
            -C "${KERNEL_DIR}"
            modules
            M=${CMAKE_CURRENT_SOURCE_DIR}
            MO=${CMAKE_CURRENT_BINARY_DIR}/module
            KCPPFLAGS="${KCPPFLAGS}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        SOURCES ${DRIVER_SOURCES} ${DRIVER_HEADERS}
)

add_dependencies(module dtb_c)

### Add single target to build everything
add_custom_target(driver ALL
        DEPENDS module
)

# Commands beyond this comment are only here to please IDEs (or their plugins) that either support CMake directly or
# support compile_commands.json
# Parse c files
add_library(dummy_target
        ${DRIVER_SOURCES}
)

# Add header files
include_directories(
        "${KERNEL_DIR}/include"
        "${KERNEL_DIR}/arch/${ARCH}/include"
        "${KERNEL_DIR}/arch/${ARCH}/include/"
)

# Add some definitions added by KBuild
add_compile_definitions(
        __KERNEL__
        MODULE
        KBUILD_MODNAME="${MODULE_NAME}"
)


