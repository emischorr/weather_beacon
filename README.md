# Weather Beacon

A low-power BLE environment sensor based on the Seeed XIAO nRF52840 and a BME280. It wakes every 15 minutes, reads temperature, humidity and pressure, sends one unencrypted **BTHome v2** advertisement and goes back to sleep. An OpenMQTTGateway node picks up the beacon and publishes the readings to MQTT / Home Assistant.

📖 **Full project reference:** <https://emischorr.github.io/weather_beacon/>. It covers the BOM, wiring diagram, battery estimates, BTHome payload layout, commissioning and optional light / soil-moisture upgrades.

## Hardware

- Seeed XIAO nRF52840 (the non-Sense variant is preferred)
- BME280 breakout (I²C)
- 2× AAA lithium cells (e.g. Energizer L92) on the `BAT+` / `BAT-` pads

| BME280 | XIAO |
|--------|------|
| VCC    | 3V3  |
| GND    | GND  |
| SDA    | D9   |
| SCL    | D10  |

> ⚠️ **Remove the batteries before connecting USB.** The onboard charger will try to charge whatever is on the BAT pads.

## Prerequisites

- [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension (VS Code suggests it when you open the folder), **or**
- the PlatformIO CLI: `pip install platformio`

The platform, board and libraries are all declared in `platformio.ini` and download on the first build.

## Build & flash

```sh
git clone https://github.com/emischorr/weather_beacon.git
cd weather_beacon

# 1. Bench test: Serial logging, short cycle
pio run -e debug -t upload
pio device monitor            # 115200 baud

# 2. Deployment build
pio run -e release -t upload
```

In VS Code, pick the environment (`env:debug` / `env:release`) in the status bar and use the PlatformIO Upload / Monitor buttons.

| Environment | Interval | Adv burst | Serial |
|-------------|----------|-----------|--------|
| `release`   | 15 min   | 300 ms    | off    |
| `debug`     | 10 s     | 1000 ms   | BME280 init status, BLE address, readings each cycle |

The debug build is for the bench only. Flash `release` before the device goes onto batteries.

If the upload port doesn't appear, **double-tap RESET** to enter the UF2 bootloader.

## Configuration

All settings are in the `USER CONFIG` block at the top of [`src/main.cpp`](src/main.cpp):

- `BME280_I2C_ADDR`: `0x76` (generic breakouts) or `0x77` (Adafruit)
- `TX_POWER_DBM`: raise a step if the gateway is out of range
- `VBAT_DIVIDER`: calibrate once against a multimeter

For the payload format, power budget and the OpenMQTTGateway setup, see the [project reference](https://emischorr.github.io/weather_beacon/).

## License

[MIT](LICENSE). The fonts bundled in `docs/fonts/` are under the [SIL Open Font License 1.1](docs/fonts/OFL.txt).
