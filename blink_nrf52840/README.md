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


## Project Structure

```
.
├── CMakeLists.txt    # Build configuration
├── prj.conf         # Project configuration
├── README.md        # This file
└── src/
    └── main.c       # Main application code
```

