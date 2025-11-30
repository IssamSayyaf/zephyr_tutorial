# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
    src/ultrasonic.c
)

zephyr_library_sources_ifdef(CONFIG_ULTRASONIC_SHELL
    src/ultrasonic_shell.c
)

zephyr_include_directories(include)
