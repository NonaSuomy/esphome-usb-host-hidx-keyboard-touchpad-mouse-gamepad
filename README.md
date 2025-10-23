# USB HID Device Support for ESPHome

This configuration enables USB HID device support (keyboards, mice, touchpads, gamepads) on ESP32 devices with USB host capability.

## Supported Devices

### Keyboards
- Boot protocol keyboards (standard USB keyboards)
- Keyboard with integrated touchpad
- LED control (Caps Lock, Num Lock, Scroll Lock)
- Full ASCII character support including special keys
- QR/Barcode Scanners

### Mice
- Boot protocol mice
- Left/Right/Middle button detection
- Movement tracking

### Touchpads
- Relative movement tracking (X/Y coordinates)
- Click detection (left/right buttons)
- Works with keyboard+touchpad combo devices

### Gamepads
- Nintendo Switch Pro Controller (official and third-party)
- Button detection (A, B, Home, etc.)
- Rumble support (official controllers only)
- Analog stick tracking

## Hardware Requirements

- ESP32 variant with USB host support (e.g., ESP32-P4, ESP32-S2, ESP32-S3)
- USB host configuration in ESPHome

## Installation

1. Copy `usb_hidx.h` to your ESPHome config directory
2. Copy `usb-hidx-test.yaml` as a template
3. Customize the YAML for your needs (remove unused sensors)
4. Upload to your ESP32 device

## Configuration

### Minimal Setup

```yaml
esphome:
  includes:
    - usb_hidx.h
  on_boot:
    then:
      - lambda: setup_usb_keyboard();

usb_host:
  enable_hubs: true

interval:
  - interval: 10ms
    then:
      - lambda: |-
          extern void process_usb_events();
          process_usb_events();
```

### Available Sensors

**Keyboard:**
- `keyboard_input` (text) - Accumulated keyboard input
- `keyboard_enter_sensor` (binary) - Enter key state
- `keyboard_esc_sensor` (binary) - ESC key state

**Mouse:**
- `mouse_left_sensor` (binary) - Left button state
- `mouse_right_sensor` (binary) - Right button state

**Touchpad:**
- `touchpad_click_sensor` (binary) - Click state
- `touchpad_x_sensor` (numeric) - X coordinate
- `touchpad_y_sensor` (numeric) - Y coordinate

**Gamepad:**
- `gamepad_a_sensor` (binary) - A button state
- `gamepad_b_sensor` (binary) - B button state
- `gamepad_home_sensor` (binary) - Home button state

## Supported Device Types

### Keyboards
- VID:PID varies by manufacturer
- Boot protocol (bInterfaceProtocol = 0x01)
- Endpoint 0x81 for keyboard data
- Endpoint 0x82 for media keys (if available)

### Touchpads
- Usually part of keyboard combo devices
- Endpoint 0x82 with Report ID 0x00/0x01/0x02
- Relative movement (not absolute positioning)

### Gamepads
- Nintendo Switch Pro: VID:057E PID:2009
- Generic HID gamepads with protocol 0x00

## Troubleshooting

### Device Not Detected
- Check USB host is enabled in YAML
- Verify device is USB HID class (0x03)
- Check logs for "New USB device detected"

### Keys Not Working
- Ensure keyboard is boot protocol compatible
- Check logs for "Key detected: 0x??"
- Verify globals and sensors are defined

### Touchpad Not Responding
- Touchpad must be on interface 1 or 2
- Check logs for "Media keys monitoring on 0x82"
- Some touchpads may use different report formats

### STALL Errors
- Normal when hot-swapping devices
- Can be suppressed with `USBH: WARN` in logger config
- Indicates device doesn't support a command

## Limitations

- Only one USB device at a time (no multi-device support)
- Touchpad coordinates are relative (accumulate from 0)
- Some exotic HID devices may not work
- Media keys depend on device support

## License

This code is provided as-is for ESPHome projects.
