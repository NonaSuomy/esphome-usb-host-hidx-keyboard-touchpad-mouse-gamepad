# USB HIDX Component

The `usb_hidx` component allows you to connect USB HID (Human Interface Device) peripherals to your ESP32-S3 and expose them to Home Assistant. This includes keyboards, mice, gaming controllers, touchscreens, and USB-to-I2C bridges.

```yaml
# Example configuration entry
usb_host:
  enable_hubs: true

usb_hidx:
  id: usb_hid
  hub: true
```

## Component/Hub

**Configuration variables:**

- **id** (*Optional*): Manually specify the ID used for code generation.
- **hub** (*Optional*, boolean): Enable USB hub support for multiple devices. Defaults to `false`.

## Supported Devices

### Gaming Controllers

The component supports the following gaming controllers with full button mapping and rumble support:

- **PlayStation 4 / PlayStation 5** - DualShock 4 and DualSense controllers
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

Connect I2C sensors via USB using these bridges:

- **MCP2221A** - Microchip USB-to-I2C/UART bridge (fully supported)
- **CP2112** - Silicon Labs USB-to-I2C bridge
- **FT260** - FTDI USB-to-I2C bridge

See [USB I2C documentation](USB_I2C_DOCUMENTATION.md) for I2C bridge configuration.

## Keyboard

The keyboard platform exposes USB keyboard input as text sensors and binary sensors for individual keys.

```yaml
# Text sensor for typed characters
text_sensor:
  - platform: usb_hidx
    name: "Keyboard Input"
    type: keyboard

# Binary sensors for specific keys
binary_sensor:
  - platform: usb_hidx
    name: "Enter Key"
    type: keyboard
    key: 0x28  # Enter

  - platform: usb_hidx
    name: "ESC Key"
    type: keyboard
    key: 0x29  # Escape
```

**Configuration variables:**

- **type** (**Required**, string): Must be `keyboard`.
- **layout** (*Optional*, string): Keyboard layout. One of `us`, `uk`, `de`, `fr`, `es`. Defaults to `us`.
- **device_id** (*Optional*, string): Unique identifier for this keyboard.
- **key** (*Optional*, int): HID keycode for binary sensor (0x04-0xE7).

**Common Key Codes:**

- `0x28` - Enter
- `0x29` - Escape
- `0x2A` - Backspace
- `0x2B` - Tab
- `0x2C` - Space
- `0x04-0x1D` - Letters A-Z
- `0x1E-0x27` - Numbers 1-0

## Mouse

The mouse platform provides binary sensors for buttons and numeric sensors for movement.

```yaml
binary_sensor:
  - platform: usb_hidx
    name: "Mouse Left Button"
    type: mouse
    left_button: true

  - platform: usb_hidx
    name: "Mouse Right Button"
    type: mouse
    right_button: true

sensor:
  - platform: usb_hidx
    name: "Mouse X"
    type: mouse
    x_delta: true

  - platform: usb_hidx
    name: "Mouse Y"
    type: mouse
    y_delta: true

  - platform: usb_hidx
    name: "Mouse Wheel"
    type: mouse
    wheel: true
```

**Configuration variables:**

- **type** (**Required**, string): Must be `mouse`.
- **device_id** (*Optional*, string): Unique identifier for this mouse.
- **left_button** (*Optional*, boolean): Left button binary sensor.
- **right_button** (*Optional*, boolean): Right button binary sensor.
- **middle_button** (*Optional*, boolean): Middle button binary sensor.
- **x_delta** (*Optional*, boolean): X-axis movement sensor.
- **y_delta** (*Optional*, boolean): Y-axis movement sensor.
- **wheel** (*Optional*, boolean): Scroll wheel sensor.

## Gamepad

The gamepad platform supports various gaming controllers with automatic detection and button mapping.

```yaml
binary_sensor:
  # Generic gamepad buttons
  - platform: usb_hidx
    name: "A Button"
    type: gamepad
    button_a: true

  - platform: usb_hidx
    name: "B Button"
    type: gamepad
    button_b: true

  # D-pad
  - platform: usb_hidx
    name: "D-Pad Up"
    type: gamepad
    dpad_up: true

sensor:
  # Analog sticks
  - platform: usb_hidx
    name: "Left Stick X"
    type: gamepad
    left_x: true

  - platform: usb_hidx
    name: "Left Stick Y"
    type: gamepad
    left_y: true

  # Triggers
  - platform: usb_hidx
    name: "Left Trigger"
    type: gamepad
    left_trigger: true
```

**Configuration variables:**

- **type** (**Required**, string): Must be `gamepad`.
- **gamepad_type** (*Optional*, string): Controller type. One of `generic`, `xbox360`, `xboxone`, `ps4`, `ps5`, `switch`, `steam`, `stadia`. Defaults to `generic`.
- **device_id** (*Optional*, string): Unique identifier for this gamepad.

**Button options (binary sensors):**

- **button_a** / **button_cross** - A button (Xbox) / Cross (PlayStation)
- **button_b** / **button_circle** - B button (Xbox) / Circle (PlayStation)
- **button_x** / **button_square** - X button (Xbox) / Square (PlayStation)
- **button_y** / **button_triangle** - Y button (Xbox) / Triangle (PlayStation)
- **button_lb** / **button_l1** - Left bumper
- **button_rb** / **button_r1** - Right bumper
- **button_back** / **button_select** - Back/Select button
- **button_start** - Start button
- **button_guide** / **button_home** - Guide/Home button
- **button_l3** - Left stick click
- **button_r3** - Right stick click
- **dpad_up** - D-pad up
- **dpad_down** - D-pad down
- **dpad_left** - D-pad left
- **dpad_right** - D-pad right

**Analog options (sensors):**

- **left_x** - Left stick X-axis (-32768 to 32767)
- **left_y** - Left stick Y-axis (-32768 to 32767)
- **right_x** - Right stick X-axis (-32768 to 32767)
- **right_y** - Right stick Y-axis (-32768 to 32767)
- **left_trigger** - Left trigger (0 to 255)
- **right_trigger** - Right trigger (0 to 255)

### Rumble Support

Controllers with rumble support can be controlled via Lambda actions:

```yaml
binary_sensor:
  - platform: usb_hidx
    name: "A Button"
    type: gamepad
    button_a: true
    on_press:
      - lambda: |-
          id(usb_hid).send_xbox360_rumble(255, 0);  // Left motor full
    on_release:
      - lambda: |-
          id(usb_hid).send_xbox360_rumble(0, 0);    // Stop rumble
```

**Available rumble methods:**

- `send_xbox360_rumble(left, right)` - Xbox 360/One controllers (0-255)
- `send_ps4_rumble(small, large)` - PlayStation 4 controllers (0-255)
- `send_ps5_rumble(left, right)` - PlayStation 5 controllers (0-255)

## Touchscreen

Multi-touch touchscreen support for capacitive USB touch panels.

```yaml
binary_sensor:
  - platform: usb_hidx
    name: "Touch Active"
    type: touchscreen
    touch_active: true

sensor:
  - platform: usb_hidx
    name: "Touch X"
    type: touchscreen
    touch_x: true

  - platform: usb_hidx
    name: "Touch Y"
    type: touchscreen
    touch_y: true
```

**Configuration variables:**

- **type** (**Required**, string): Must be `touchscreen`.
- **touch_active** (*Optional*, boolean): Touch detection binary sensor.
- **touch_x** (*Optional*, boolean): X-coordinate sensor.
- **touch_y** (*Optional*, boolean): Y-coordinate sensor.
- **touch_id** (*Optional*, int): Touch point ID for multi-touch (0-9).

## Complete Example

```yaml
# Complete configuration with multiple devices
esphome:
  name: usb-hid-hub
  friendly_name: USB HID Hub

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_USB_HOST_ENABLE: y
      CONFIG_USB_HOST_HCD_DWC_NUM_CHANNELS: "8"

logger:
  level: DEBUG

api:
ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# USB Host
usb_host:
  enable_hubs: true

# USB HIDX Component
usb_hidx:
  id: usb_hid
  hub: true

# Keyboard
text_sensor:
  - platform: usb_hidx
    name: "Keyboard Input"
    type: keyboard
    layout: us

binary_sensor:
  # Keyboard keys
  - platform: usb_hidx
    name: "Enter Key"
    type: keyboard
    key: 0x28

  # Mouse buttons
  - platform: usb_hidx
    name: "Mouse Left"
    type: mouse
    left_button: true

  # Xbox controller
  - platform: usb_hidx
    name: "Xbox A Button"
    type: gamepad
    gamepad_type: xbox360
    button_a: true
    on_press:
      - lambda: 'id(usb_hid).send_xbox360_rumble(200, 200);'
    on_release:
      - lambda: 'id(usb_hid).send_xbox360_rumble(0, 0);'

  # PlayStation controller
  - platform: usb_hidx
    name: "PS Cross Button"
    type: gamepad
    gamepad_type: ps4
    button_cross: true

sensor:
  # Mouse movement
  - platform: usb_hidx
    name: "Mouse X"
    type: mouse
    x_delta: true

  # Controller analog sticks
  - platform: usb_hidx
    name: "Left Stick X"
    type: gamepad
    left_x: true

  - platform: usb_hidx
    name: "Left Trigger"
    type: gamepad
    left_trigger: true
```

## Hardware Requirements

- **ESP32-S3** with USB OTG support (required)
- **USB OTG cable** or USB-A adapter
- **External power** recommended for multiple devices or high-power devices
- **USB hub** (optional) for connecting multiple devices

The ESP32-S3 USB port can provide up to 500mA. For devices requiring more power (gaming controllers with rumble, RGB keyboards), use a powered USB hub.

## Wiring

```
ESP32-S3          USB Device
--------          ----------
GPIO19 (D-)  <->  D-
GPIO20 (D+)  <->  D+
GND          <->  GND
5V           <->  VBUS (if self-powered)
```

For USB-C ESP32-S3 boards, use a USB-C to USB-A adapter or USB-C OTG cable.

## Troubleshooting

### Device Not Detected

- Verify ESP32-S3 variant in configuration
- Check USB cable supports data (not charge-only)
- Try different USB port or hub
- Check logs for VID/PID detection
- Ensure `CONFIG_USB_HOST_ENABLE: y` in sdkconfig

### Controller Not Working

- Verify controller type matches configuration
- Some controllers require pairing (PlayStation, Switch)
- Wireless controllers need USB dongle
- Check power supply (controllers draw 100-500mA)

### Keyboard Layout Wrong

- Set correct layout: `layout: us` / `uk` / `de` / `fr` / `es`
- Some keys may not map correctly on non-US layouts
- Check HID keycode in logs

### Performance Issues

- Reduce logger level to `INFO` or `WARN`
- Disable unused device types
- Use powered USB hub for multiple devices
- Check USB cable quality

## See Also

- [USB I2C Bridge Documentation](USB_I2C_DOCUMENTATION.md)
- [Complete Driver List](COMPLETE_DRIVERS.md)
