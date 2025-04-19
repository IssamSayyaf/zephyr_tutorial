# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BUTTON_DRIVER)
  zephyr_library()
  zephyr_library_sources(src/button_driver.c)
  zephyr_library_include_directories(include)
endif() 