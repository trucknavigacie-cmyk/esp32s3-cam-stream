# Firmware — hotové binárky

Hotový skompilovaný firmware. **Nepotrebuješ Arduino IDE ani toolchain** —
stačí esptool a USB kábel.

## Súbory

| Súbor | Offset | Veľkosť | Popis |
|---|---|---|---|
| `ESP32S3_CAM_WEB.ino.merged.bin` | **0x0** | 16 MB | **Kompletný obraz** — všetko v jednom |
| `ESP32S3_CAM_WEB.ino.bootloader.bin` | 0x0 | 20 KB | Bootloader |
| `ESP32S3_CAM_WEB.ino.partitions.bin` | 0x8000 | 3 KB | Partition table |
| `boot_app0.bin` | 0xE000 | 8 KB | OTA data |
| `ESP32S3_CAM_WEB.ino.bin` | 0x10000 | 988 KB | Aplikácia |

## Metóda 1 — jedným súborom (najjednoduchšie)

```bash
pip install esptool

esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 ESP32S3_CAM_WEB.ino.merged.bin
```

Alebo s novším esptool:

```bash
esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 \
  write-flash 0x0 ESP32S3_CAM_WEB.ino.merged.bin
```

> `merged.bin` prepíše **celú flash** vrátane nastavení. Čistý štart.

## Metóda 2 — po častiach (zachová NVS)

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0     ESP32S3_CAM_WEB.ino.bootloader.bin \
  0x8000  ESP32S3_CAM_WEB.ino.partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 ESP32S3_CAM_WEB.ino.bin
```

## Metóda 3 — esptool cez Arduino CLI

```bash
arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn "esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,MSCOnBoot=default,DFUOnBoot=default" \
  --input-dir . \
  ESP32S3_CAM_WEB.ino
```

## Port

| Systém | USB-OTG (pravý) | USB-UART (ľavý) |
|---|---|---|
| Linux | `/dev/ttyACM0` | `/dev/ttyACM0` (jadro 6.10+) alebo `/dev/ttyUSB0` |
| macOS | `/dev/cu.usbmodem*` | `/dev/cu.wchusbserial*` |
| Windows | `COMx` | `COMx` |

> **Použi USB-OTG (pravý konektor).** CH343 vľavo na tejto doske padá.

## Po nahratí

1. Doska vytvorí WiFi **`ESP32-S3-CAM`**, heslo **`12345678`**
2. Pripoj sa telefónom/PC
3. Prehliadač → **`http://192.168.4.1/`**

## Ak nahrávanie zlyhá

```
Wrong boot mode detected (0x8)
```

→ fyzicky prejdi do download mode: **podrž BOOT → klikni RST → drž 3 s → pusti BOOT**

```
Invalid head of packet (0x45)
```

→ vymeň kábel na USB-OTG, alebo použi manuálny download mode

Detailne: [../docs/TROUBLESHOOTING.md](../docs/TROUBLESHOOTING.md)

## Konfigurácia v tomto builde

| Parameter | Hodnota |
|---|---|
| CPU | 240 MHz |
| Flash | 16 MB, QIO 80 MHz |
| PSRAM | OPI 8 MB |
| Partition | 3MB APP / 9.9MB FATFS |
| Rozlíšenie | QVGA 320×240 |
| JPEG kvalita | 12 |
| XCLK kamery | 16 MHz |
| FPS cieľ | 15 (regulovateľné 5–30) |
| WiFi TX | 8.5 dBm |
| AP kanál | 6 (krajina SK) |

## Prebuild info

- Arduino-ESP32 core: **3.3.12**
- esptool: **5.3.1**
- ESP-IDF SDK: **v5.5.5**
- Veľkosť aplikácie: **988 208 B** (31 % z 3 MB)
- Dátum buildu: 2026-11-15
