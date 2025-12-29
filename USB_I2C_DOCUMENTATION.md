# USB I2C Bridge

The `usb_i2c` component allows you to use USB-to-I2C bridge devices to connect I2C sensors to your ESP32-P4, ESP32-S3, or ESP32-S2 via USB. This is useful when you need to connect I2C devices but don't have available I2C pins, or when you want to isolate sensors from the main board.

```yaml
# Example configuration entry
usb_host:
  enable_hubs: true

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

**Configuration variables:**

- **id** (*Optional*): Manually specify the ID for this I2C bus.
- **scan** (*Optional*, boolean): Scan for I2C devices on startup. Defaults to `true`.
- **frequency** (*Optional*, frequency): I2C clock frequency. One of `100kHz` (standard) or `400kHz` (fast mode). Defaults to `100kHz`.

## Supported Bridges

### MCP2221A

**Status**: Fully supported ✅

Microchip's MCP2221A is a USB-to-UART/I2C serial converter with GPIO.

- **VID/PID**: 04D8:00DD
- **I2C Speed**: 100kHz, 400kHz
- **Pull-ups**: Internal 4.7kΩ pull-ups available
- **Power**: 5V output up to 500mA
- **Price**: ~$2-3 USD

**Wiring:**

```
MCP2221A          I2C Device
--------          ----------
SCL          <->  SCL
SDA          <->  SDA
GND          <->  GND
VDD (3.3V)   <->  VCC (if 3.3V device)
5V           <->  VCC (if 5V device)
```

### CP2112

**Status**: Partial support ⏳

Silicon Labs CP2112 is a USB-to-I2C/SMBus bridge.

- **VID/PID**: 10C4:EA90
- **I2C Speed**: 10kHz to 1MHz
- **Pull-ups**: Configurable internal pull-ups
- **Price**: ~$3-4 USD

### FT260

**Status**: Partial support ⏳

FTDI's FT260 is a USB-to-I2C/UART bridge.

- **VID/PID**: 0403:6030
- **I2C Speed**: 100kHz, 400kHz, 1MHz
- **Pull-ups**: Configurable internal pull-ups
- **Price**: ~$4-5 USD

## Using with Sensors

Any ESPHome I2C sensor can be used with the USB I2C bus by specifying the `i2c_id`:

### BME280 Temperature/Humidity/Pressure

```yaml
sensor:
  - platform: bme280
    i2c_id: usb_i2c_bus
    temperature:
      name: "USB BME280 Temperature"
    pressure:
      name: "USB BME280 Pressure"
    humidity:
      name: "USB BME280 Humidity"
    address: 0x76
    update_interval: 60s
```

### BH1750 Light Sensor

```yaml
sensor:
  - platform: bh1750
    i2c_id: usb_i2c_bus
    name: "USB BH1750 Illuminance"
    address: 0x23
    update_interval: 60s
```

### ADS1115 ADC

```yaml
ads1115:
  - address: 0x48
    i2c_id: usb_i2c_bus

sensor:
  - platform: ads1115
    multiplexer: 'A0_GND'
    gain: 6.144
    name: "USB ADS1115 Channel 0"
```

### SHT3x Temperature/Humidity

```yaml
sensor:
  - platform: sht3xd
    i2c_id: usb_i2c_bus
    temperature:
      name: "USB SHT3x Temperature"
    humidity:
      name: "USB SHT3x Humidity"
    address: 0x44
    update_interval: 60s
```

### PCA9685 PWM Controller

```yaml
pca9685:
  - id: usb_pwm
    i2c_id: usb_i2c_bus
    address: 0x40
    frequency: 500

output:
  - platform: pca9685
    pca9685_id: usb_pwm
    channel: 0
    id: pwm_output
```

## Multiple Bridges

You can use multiple USB I2C bridges simultaneously:

```yaml
usb_i2c:
  - id: usb_i2c_1
    scan: true
    frequency: 100kHz

  - id: usb_i2c_2
    scan: true
    frequency: 400kHz

sensor:
  - platform: bme280
    i2c_id: usb_i2c_1
    temperature:
      name: "Bridge 1 Temperature"
    address: 0x76

  - platform: bme280
    i2c_id: usb_i2c_2
    temperature:
      name: "Bridge 2 Temperature"
    address: 0x76
```

## Complete Example

```yaml
esphome:
  name: usb-i2c-hub
  friendly_name: USB I2C Sensor Hub

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_USB_HOST_ENABLE: y

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

# USB I2C Bridge
usb_i2c:
  id: usb_i2c_bus
  scan: true
  frequency: 100kHz

# Multiple sensors on USB I2C bus
sensor:
  # BME280 - Temperature, Humidity, Pressure
  - platform: bme280
    i2c_id: usb_i2c_bus
    temperature:
      name: "Temperature"
    pressure:
      name: "Pressure"
    humidity:
      name: "Humidity"
    address: 0x76
    update_interval: 60s

  # BH1750 - Light sensor
  - platform: bh1750
    i2c_id: usb_i2c_bus
    name: "Illuminance"
    address: 0x23
    update_interval: 60s

  # SHT3x - Temperature and Humidity
  - platform: sht3xd
    i2c_id: usb_i2c_bus
    temperature:
      name: "SHT3x Temperature"
    humidity:
      name: "SHT3x Humidity"
    address: 0x44
    update_interval: 60s
```

## Hardware Requirements

- **ESP32-P4, ESP32-S3, or ESP32-S2** with USB OTG support (required)
- **USB-to-I2C bridge** (MCP2221A recommended)
- **USB OTG cable** or USB-A adapter
- **I2C sensors** with 3.3V or 5V support

The MCP2221A provides both 3.3V and 5V outputs, making it compatible with most I2C sensors.

## Wiring Example

```
ESP32-S3 <-> MCP2221A <-> BME280 Sensor

ESP32-S3          MCP2221A
--------          --------
GPIO19 (D-)  <->  D-
GPIO20 (D+)  <->  D+
GND          <->  GND
5V           <->  VBUS

MCP2221A          BME280
--------          ------
SCL          <->  SCL
SDA          <->  SDA
GND          <->  GND
3.3V         <->  VCC
```

## Performance

USB I2C bridges add latency compared to native I2C:

- **Native I2C**: ~1ms per transaction
- **USB I2C**: ~2-5ms per transaction
- **Recommended update_interval**: 10s or longer

For high-speed or real-time applications, use native I2C pins instead.

## Limitations

- **Single transaction**: One I2C operation at a time
- **No clock stretching**: Some sensors may not work
- **USB overhead**: Slower than native I2C
- **No multi-master**: Only one I2C master per bridge
- **Timeout**: 1000ms maximum per operation

## Troubleshooting

### Bridge Not Detected

- Check USB connection and cable
- Verify VID/PID in logs (04D8:00DD for MCP2221A)
- Try different USB port or hub
- Ensure `usb_host` component is configured

### I2C Scan Shows No Devices

- Check I2C wiring (SDA, SCL, GND, VCC)
- Verify sensor I2C address (use I2C scanner)
- Check pull-up resistors (MCP2221A has internal)
- Try lower frequency (100kHz)
- Verify sensor power supply voltage

### Sensor Timeouts

- Reduce I2C frequency to 100kHz
- Increase `update_interval` to 60s or more
- Check for loose connections
- Verify sensor power supply is stable
- Try different USB cable

### Communication Errors

- Check I2C address conflicts (use `scan: true`)
- Verify sensor is compatible with I2C (not SPI)
- Check sensor datasheet for timing requirements
- Try different sensor or bridge

### Compilation Errors

- Ensure `usb_i2c` in external_components
- Check ESP-IDF framework is selected
- Verify ESP32 variant is specified
- Update ESPHome to latest version

## See Also

- [USB HIDX Component](USB_HIDX_DOCUMENTATION.md)
