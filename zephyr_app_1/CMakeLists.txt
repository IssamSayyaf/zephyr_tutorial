# SPDX-License-Identifier: Apache-2.0

# Specify minimum CMake version required
cmake_minimum_required(VERSION 3.20.0)

# Set the target board to Raspberry Pi Pico
set(BOARD rpi_pico)
# Specify the Device Tree overlay file that contains custom board configurations
set(DTC_OVERLAY_FILE "board/app.overlay")

# Find and include the Zephyr package which provides build system integration
# REQUIRED ensures CMake will error if Zephyr isn't found
# HINTS tells CMake where to look for Zephyr using the ZEPHYR_BASE environment variable
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Define the project name
project(ZEPHYR_APP_1)

# Add the main application source file
target_sources(app PRIVATE src/main.c)

# Add the button driver source files
target_sources(app PRIVATE
    drivers/button/src/button_driver.c
)

# Add the driver include directory
target_include_directories(app PRIVATE drivers/button/include)
