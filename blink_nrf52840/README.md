# nRF52840 DK Blinky Project

A simple blinky project for the nRF52840 Development Kit using Zephyr RTOS.

## Prerequisites

- Zephyr RTOS environment set up
- nRF52840 DK board
- JLink or nrfjprog for flashing
- Serial monitor for debugging

## Building and Flashing

1. Build the project:
```bash
west build -b nrf52840dk/nrf52840
```

2. Flash the board using one of these methods:

### Using JLink
```bash
west flash --runner jlink
```

### Using nrfjprog
```bash
west flash --runner nrfjprog
```

## Serial Monitor Setup

To monitor the serial output:

1. Install a serial monitor extension in Cursor:
   - Press `Ctrl+Shift+X` to open Extensions
   - Search for "Serial Monitor"
   - Install your preferred extension

2. Connect to the serial port:
   - Press `Ctrl+Shift+P` to open command palette
   - Type "Serial Monitor" and select it
   - Select your device (likely `/dev/ttyACM0` or `/dev/ttyACM1`)
   - Set baud rate to 115200
   - Click "Connect"

## Project Structure

```
.
├── CMakeLists.txt    # Build configuration
├── prj.conf         # Project configuration
├── README.md        # This file
└── src/
    └── main.c       # Main application code
```

## Troubleshooting

1. If flashing fails:
   - Ensure the board is properly connected
   - Check if JLink or nrfjprog is installed
   - Verify the board is powered on

2. If serial monitor doesn't work:
   - Check if the correct port is selected
   - Verify the baud rate is set to 115200
   - Ensure no other program is using the serial port

## License

This project is licensed under the Apache-2.0 license. 