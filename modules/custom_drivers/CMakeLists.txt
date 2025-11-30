# SPDX-License-Identifier: Apache-2.0
# Custom Drivers Module CMakeLists.txt

if(CONFIG_UART_ADVANCED)
  add_subdirectory(drivers/uart_advanced)
endif()

if(CONFIG_SPI_ADVANCED)
  add_subdirectory(drivers/spi_advanced)
endif()

if(CONFIG_MPU6050_CUSTOM)
  add_subdirectory(drivers/mpu6050)
endif()

if(CONFIG_UBLOX_NEO_M8N)
  add_subdirectory(drivers/ublox_neo_m8n)
endif()

if(CONFIG_ULTRASONIC)
  add_subdirectory(drivers/ultrasonic)
endif()
