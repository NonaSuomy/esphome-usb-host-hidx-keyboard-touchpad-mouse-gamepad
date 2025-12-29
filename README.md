# USB HIDX Component for ESPHome

Comprehensive USB HID device support for ESP32-P4, ESP32-S3, and ESP32-S2 with USB host capability. Connect keyboards, mice, gaming controllers, touchscreens, and USB-to-I2C bridges to your ESPHome devices.

## Features

- **17 Device Drivers**: Support for keyboards, mice, and 15+ gaming controller types
- **Hot-Plug Support**: Automatic device detection and reconnection
- **Multi-Device**: Connect multiple USB devices via USB hub
- **Rumble Support**: Controller haptic feedback for Xbox, PlayStation, and Switch controllers
- **USB I2C Bridges**: Connect I2C sensors via USB (MCP2221A, CP2112, FT260)
- **Multiple Layouts**: Keyboard support for US, UK, DE, FR, ES layouts

## Documentation

- **[USB HIDX Documentation](USB_HIDX_DOCUMENTATION.md)** - Complete guide for keyboards, mice, gamepads, and touchscreens
- **[USB I2C Documentation](USB_I2C_DOCUMENTATION.md)** - USB-to-I2C bridge configuration and sensor integration

## Supported Devices

### Gaming Controllers
- **PlayStation 4 / PlayStation 5** - DualShock 4 and DualSense controllers with full rumble
- **Xbox One / Series X|S** - Modern Xbox controllers with impulse triggers
- **Xbox 360** - Classic Xbox 360 wired and wireless controllers
- **Nintendo Switch Pro Controller** - Full button and gyro support
- **Steam Controller** - Valve's Steam Controller
- **Google Stadia Controller** - Stadia gamepad
- **Thrustmaster** - Racing wheels and flight sticks
- **Generic Gamepads** - Standard HID gamepad support

### Input Devices
- **Keyboards** - Full keyboard support with multiple layouts (US, UK, DE, FR, ES)
- **Mice** - 3-button mice with scroll wheel and movement tracking
- **Touchscreens** - Multi-touch capacitive touchscreens
- **Logitech Unifying** - Logitech wireless receiver support
- **Wii Remote** - Nintendo Wii controller via Bluetooth

### USB-to-I2C Bridges
- **MCP2221A** - Microchip USB-to-I2C/UART bridge (fully supported)
- **CP2112** - Silicon Labs USB-to-I2C bridge (partial support)
- **FT260** - FTDI USB-to-I2C bridge (partial support)

## Hardware Requirements

- **ESP32-P4, ESP32-S3, or ESP32-S2** with USB OTG support
- **USB OTG cable** or USB-A adapter
- **External power** recommended for multiple devices or high-power devices
- **USB hub** (optional) for connecting multiple devices

## Installation

### Using External Components (Recommended)

```yaml
external_components:
  - source: github://NonaSuomy/esphome@hidx-testing-001
    components: [ usb_hidx, usb_i2c ]
```

### Manual Installation

1. Copy component files to your ESPHome `components/` directory
2. Configure in your YAML file
3. Upload to your ESP32 device

## Quick Start

### Keyboard Example

```yaml
esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_USB_HOST_ENABLE: y

usb_host:
  enable_hubs: true

usb_hidx:
  id: usb_hid
  hub: true

text_sensor:
  - platform: usb_hidx
    name: "Keyboard Input"
    type: keyboard
    layout: us

binary_sensor:
  - platform: usb_hidx
    name: "Enter Key"
    type: keyboard
    key: 0x28
```

### Gamepad Example

```yaml
binary_sensor:
  - platform: usb_hidx
    name: "A Button"
    type: gamepad
    gamepad_type: xbox360
    button_a: true
    on_press:
      - lambda: 'id(usb_hid).send_xbox360_rumble(255, 0);'
    on_release:
      - lambda: 'id(usb_hid).send_xbox360_rumble(0, 0);'

sensor:
  - platform: usb_hidx
    name: "Left Stick X"
    type: gamepad
    left_x: true
```

### USB I2C Bridge Example

```yaml
usb_i2c:
  id: usb_i2c_bus
  scan: true
  frequency: 100kHz

sensor:
  - platform: bme280
    i2c_id: usb_i2c_bus
    temperature:
      name: "Temperature"
    pressure:
      name: "Pressure"
    humidity:
      name: "Humidity"
    address: 0x76
```

## Component Architecture

### Driver Registry

The component uses an automatic driver registration system that detects and loads appropriate drivers based on USB device VID/PID:

- **Keyboard Driver** - HID boot protocol keyboards
- **Mouse Driver** - HID boot protocol mice
- **Touchscreen Driver** - Multi-touch USB touchscreens
- **Xbox 360 Driver** - Microsoft Xbox 360 controllers
- **Xbox One Driver** - Microsoft Xbox One/Series controllers
- **PlayStation 4 Driver** - Sony DualShock 4 controllers
- **PlayStation 5 Driver** - Sony DualSense controllers
- **Switch Pro Driver** - Nintendo Switch Pro controllers
- **Steam Controller Driver** - Valve Steam controllers
- **Stadia Controller Driver** - Google Stadia controllers
- **Thrustmaster Driver** - Thrustmaster racing wheels and flight sticks
- **Logitech Unifying Driver** - Logitech wireless receivers
- **Wii Remote Driver** - Nintendo Wii controllers
- **Generic Gamepad Driver** - Standard HID gamepads
- **MCP2221A Driver** - USB-to-I2C bridge
- **CP2112 Driver** - USB-to-I2C bridge (partial)
- **FT260 Driver** - USB-to-I2C bridge (partial)

## Advanced Features

### Rumble Control

Controllers with rumble support can be controlled via Lambda actions:

```yaml
- lambda: 'id(usb_hid).send_xbox360_rumble(255, 255);'  // Full rumble
- lambda: 'id(usb_hid).send_ps4_rumble(128, 128);'      // Half rumble
- lambda: 'id(usb_hid).send_ps5_rumble(0, 0);'          // Stop rumble
```

### Multiple USB Devices

Connect multiple devices using a USB hub:

```yaml
usb_host:
  enable_hubs: true

usb_hidx:
  id: usb_hid
  hub: true
```

### Multiple I2C Bridges

Use multiple USB I2C bridges simultaneously:

```yaml
usb_i2c:
  - id: usb_i2c_1
    frequency: 100kHz
  - id: usb_i2c_2
    frequency: 400kHz
```

## Troubleshooting

See the detailed documentation for comprehensive troubleshooting:

- [USB HIDX Troubleshooting](USB_HIDX_DOCUMENTATION.md#troubleshooting)
- [USB I2C Troubleshooting](USB_I2C_DOCUMENTATION.md#troubleshooting)

### Common Issues

**Device Not Detected:**
- Verify ESP32 variant supports USB OTG
- Check `CONFIG_USB_HOST_ENABLE: y` in sdkconfig
- Try different USB cable (not charge-only)
- Check logs for VID/PID detection

**Controller Not Working:**
- Verify controller type matches configuration
- Some controllers require pairing
- Wireless controllers need USB dongle
- Check power supply (controllers draw 100-500mA)

**I2C Bridge Issues:**
- Verify VID/PID in logs (04D8:00DD for MCP2221A)
- Check I2C wiring and pull-up resistors
- Try lower frequency (100kHz)
- Increase sensor update_interval

## Repository Structure

```
.
├── README.md                      # This file
├── USB_HIDX_DOCUMENTATION.md      # Complete USB HIDX documentation
├── USB_HIDX_DOCUMENTATION.rst     # RST format for ESPHome docs
├── USB_I2C_DOCUMENTATION.md       # Complete USB I2C documentation
├── USB_I2C_DOCUMENTATION.rst      # RST format for ESPHome docs
├── usb-hidx-test.yaml             # Example configuration
└── backup/                        # Legacy files
    ├── usb_hidx.h                 # Original header file
    └── usb-hidx-test.yaml         # Original test config
```

## Contributing

Contributions are welcome! Please submit issues and pull requests to:
- Component Code: https://github.com/NonaSuomy/esphome (hidx-testing-001 branch)
- Documentation: https://github.com/NonaSuomy/esphome-usb-host-hidx-keyboard-touchpad-mouse-gamepad

## License

This code is provided as-is for ESPHome projects.
