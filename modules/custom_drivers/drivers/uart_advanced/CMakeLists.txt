# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
    src/uart_advanced.c
)

zephyr_library_sources_ifdef(CONFIG_UART_ADVANCED_SHELL
    src/uart_advanced_shell.c
)

zephyr_include_directories(include)
