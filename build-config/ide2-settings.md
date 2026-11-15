# Arduino IDE 2 — nastavenia pre tento projekt
#
# IDE 2.x si pamata nastavenia v ~/.arduinoIDE/settings.json
# a per-projekt v <sketch>/sketch.json
#
# Tento subor je referencia odporucanych hodnot.

## Globalne (File -> Preferences)

```
Additional boards manager URLs:
  https://espressif.github.io/arduino-esp32/package_esp32_index.json

Sketchbook location:
  ~/Arduino

Show verbose output during: compile [ ] upload [ ]
Compiler warnings: None
```

## Board manager

```
esp32 by Espressif Systems  ->  verzia 3.3.12
```

## Tools menu (PER PROJEKT — ulozi sa do sketch.json)

| Polozka | Hodnota |
|---|---|
| Board | **ESP32S3 Dev Module** |
| USB CDC On Boot | **Enabled** |
| CPU Frequency | **240MHz (WiFi)** |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled |
| Events Run On | Core 1 |
| Flash Mode | **QIO 80MHz** |
| Flash Size | **16MB (128Mb)** |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | **OPI PSRAM** |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | **921600** |
| USB Mode | Hardware CDC and JTAG |
| Port | /dev/ttyACM0 (USB-OTG) |

## Port

| Konektor | Linux port | Poznamka |
|---|---|---|
| USB-OTG (pravy) | `/dev/ttyACM0` | **pouzivaj tento** |
| USB-UART (lavy) | `/dev/ttyACM0` alebo `/dev/ttyUSB0` | CH343, nestabilny |

## Serial Monitor

```
Baud rate: 115200
Line ending: Newline
```

## Bez Flatpaku (AppImage/deb)

Ak ides cez `.deb` alebo AppImage, cesta k portom je rovnaka.
Problem byva len s Flatpak sandboxom — treba `flatpak override --user
--device=all cc.arduino.IDE2` aby IDE videlo `/dev/ttyACM0`.
