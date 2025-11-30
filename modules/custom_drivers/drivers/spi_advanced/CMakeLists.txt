# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
    src/spi_advanced.c
)

zephyr_library_sources_ifdef(CONFIG_SPI_ADVANCED_SHELL
    src/spi_advanced_shell.c
)

zephyr_include_directories(include)
