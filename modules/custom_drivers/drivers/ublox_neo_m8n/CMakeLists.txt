# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
    src/ublox_neo_m8n.c
    src/nmea_parser.c
)

zephyr_library_sources_ifdef(CONFIG_UBLOX_NEO_M8N_SHELL
    src/ublox_neo_m8n_shell.c
)

zephyr_include_directories(include)
