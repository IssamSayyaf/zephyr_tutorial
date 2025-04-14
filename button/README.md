# Button Project

This is a simple Zephyr RTOS application that demonstrates GPIO button input handling with optional LED output.

## Overview

The application:
- Configures a button input (sw0) with interrupt support
- Optionally configures an LED output (led0) if available
- Prints button press events to the console
- If an LED is available, it toggles the LED state to match the button state

## Hardware Requirements

- A board with at least one button (sw0)
- Optional: An LED (led0) for visual feedback

## Configuration

The project requires the following configuration:
- `CONFIG_GPIO=y` - Enables GPIO support

## Building and Running

1. Build the application:
```bash
west build -b <your_board> button
```

2. Flash the application:
```bash
west flash
```

## Expected Behavior

- When the button is pressed, a message will be printed to the console
- If an LED is available, it will turn on when the button is pressed and off when released

## Notes

- The button (sw0) is mandatory - the application will not build if it's not defined in the devicetree
- The LED (led0) is optional - the application will work without it
- Button debouncing is not implemented in this example. For production use, consider using the input subsystem instead 