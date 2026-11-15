# Troubleshooting — detailná diagnostika

Zápisník z reálneho oživovania. Každá sekcia obsahuje **príznak → príčinu → riešenie**.

---

## Diagnostické príkazy

### Zistenie portov

```bash
ls -l /dev/serial/by-id/
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
lsusb | grep -iE "1a86|303a|espressif|ch34|cp210"
```

| Vendor:Product | Zariadenie |
|---|---|
| `303a:1001` | Espressif USB JTAG/serial debug unit (natívny USB-OTG) |
| `1a86:55d3` | QinHeng CH343 (USB-UART bridge) |
| `10c4:ea60` | CP210x (iný bridge) |

### Ktorý port je ktorý

```bash
udevadm info -q property -n /dev/ttyACM0 | grep -E "ID_VENDOR|ID_MODEL|ID_USB_DRIVER"
```

- `ID_USB_DRIVER=cdc_acm` + `ID_VENDOR_ID=1a86` → CH343 (USB-UART konektor)
- `ID_USB_DRIVER=usbfs`/`ID_MODEL=USB_JTAG` → natívny USB-OTG

> **Pozor:** na jadrách 6.10+ a 7.x sa CH343 registruje cez `cdc_acm`
> ako `/dev/ttyACM0`, **nie** `/dev/ttyUSB0`. Pri starších jadrách
> to bude `ttyUSB0`.

### Čítanie sériového výstupu

```bash
# jednoduché
timeout 10 cat /dev/ttyACM0

# s resetom dosky
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
s.setDTR(True); time.sleep(0.2); s.setDTR(False)   # EN low -> high
s.reset_input_buffer()
end = time.time() + 10
while time.time() < end:
    d = s.read(2048)
    if d: print(d.decode('utf-8', 'replace'), end='')
"

# filtrovaný výstup (bez FB-OVF šumu)
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
end = time.time() + 10
while time.time() < end:
    d = s.read(2048)
    if d:
        for l in d.decode('utf-8','replace').splitlines():
            if l.strip() and 'FB-OVF' not in l: print(l)
"
```

### esptool priamo

```bash
ESPTOOL=~/.arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool

$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 chip-id
$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 flash-id
$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 -b 460800 read-flash 0x0 0x1000 boot.bin
```

### Práva na port

```bash
groups                     # musi obsahovat 'dialout'
sudo usermod -aG dialout $USER   # potom sa odhlasit/prihlasit
```

---

## Boot problémy

### Boot loop s `ets_loader.c 78`

```
ESP-ROM:esp32s3-20210327
rst:0x7 (TG0WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
Saved PC:0x400454d5
mode:QIO, clock div:1
load:0x3fce2820,len:0x10cc
ets_loader.c 78
```

**Príčina:** pád v 2. stupni ROM bootloadera, aplikácia sa vôbec nezačne.

**Najčastejšie dôvody (v tomto poradí):**

| # | Príčina | Riešenie |
|---|---|---|
| 1 | **Nahrávanie cez CH343** — bootloader dostal nekonzistentné dáta | Nahrávať cez **USB-OTG** |
| 2 | Chýbajúci `boot_app0.bin` na 0xe000 | `arduino-cli` ho pridá automaticky |
| 3 | Poškodená partition table | `EraseFlash=all` raz |
| 4 | Nesprávny FlashMode/Size | `QIO` + `16MB` |

**Rýchla oprava:**
```bash
$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 erase-flash
```
potom nahrať znova.

---

## USB-UART (CH343) problémy

### `Wrong boot mode detected (0x8)`

```
A fatal error occurred: Failed to connect to ESP32-S3:
Wrong boot mode detected (0x8)! The chip needs to be in download mode.
```

**Príčina:** `0x8` = `SPI_FAST_FLASH_BOOT` — chip normálne bootuje, esptool
ho nedonútil do download mode. Reset obvod CH343 nezabral.

**Riešenia:**
1. **USB-OTG** (najspoľahlivejšie)
2. Manuálne: podrž BOOT → RST → 3 s → pusti BOOT
3. Nižšia rýchlosť: `UploadSpeed=115200`

### `Invalid head of packet (0x45)`

```
A fatal error occurred: Invalid head of packet (0x45):
Possible serial noise or corruption.
```

**Príčina:** `0x45` = `'E'` — prvý znak `ESP-ROM:`. Boot log sa dostal
do packet streamu počas synchronizácie. Race condition.

**Riešenie:** USB-OTG, alebo manuálny download mode + `--before no-reset`.

### Manuálny download mode + upload

```bash
# 1. Fyzicky: podrz BOOT, klikni RST, drz 3s, pusti BOOT

# 2. Over, ze je v download mode
$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 -b 115200 \
         --before no-reset --after no-reset chip-id

# 3. Nahraj s no-reset
$ESPTOOL --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
         --before no-reset --after no-reset \
         write-flash --flash-mode qio --flash-freq 80m --flash-size 16MB \
         0x0     build/ESP32S3_CAM.ino.bootloader.bin \
         0x8000  build/ESP32S3_CAM.ino.partitions.bin \
         0xe000  boot_app0.bin \
         0x10000 build/ESP32S3_CAM.ino.bin
```

---

## WiFi problémy

### AP sa nevysiela — telefón sieť nevidí

**Diagnostika:** pridaj do kódu po `softAP()`:

```cpp
Serial.printf("WiFi mode      : %d (2=AP)\n", (int)WiFi.getMode());
Serial.printf("AP kanal       : %d\n", WiFi.channel());
Serial.printf("AP MAC         : %s\n", WiFi.softAPmacAddress().c_str());
Serial.printf("TX power (API) : %.1f dBm\n", WiFi.getTxPower());
int8_t txp = 0;
esp_wifi_get_max_tx_power(&txp);
Serial.printf("TX power (raw) : %d (%.1f dBm)\n", txp, txp * 0.25);
```

Ak `TX power (raw) = 0` → **AP nevysiela**, aj keď `softAP()` vrátil `true`.

**Riešenie:**
```cpp
#include "esp_wifi.h"
// ...
WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);
esp_wifi_set_max_tx_power(34);      // 34 = 8.5 dBm, 78 = 19.5 dBm
esp_wifi_set_ps(WIFI_PS_NONE);      // bez power-save
```

### `Channel number X is out of range`

```
E wifi:(2G)Channel number 13 is out of range from 1 to 11 in country code cc=01
```

**Riešenie:** nastav krajinu **pred** `softAP()`:

```cpp
wifi_country_t country = {};
strncpy(country.cc, "SK", sizeof(country.cc));
country.schan = 1;
country.nchan = 13;
country.max_tx_power = 34;
country.policy = WIFI_COUNTRY_POLICY_MANUAL;

WiFi.mode(WIFI_AP);
esp_wifi_set_country(&country);
WiFi.softAP(AP_SSID, AP_PASS, 13, 0, 4);
```

### Klienti sa pripájajú a hneď padajú

- `WiFi.setSleep(false)`
- `esp_wifi_set_ps(WIFI_PS_NONE)`
- nižší počet max klientov (2 namiesto 4)
- nižší TX power (menej rušenia v malej miestnosti)

### `WiFi.scanNetworks()` vypne AP

Po skene treba AP znovu spustiť **aj s** krajinou a txpower:

```cpp
int n = WiFi.scanNetworks();
WiFi.scanDelete();
WiFi.mode(WIFI_AP);
esp_wifi_set_country(&country);
WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 2);
esp_wifi_set_max_tx_power(34);
esp_wifi_set_ps(WIFI_PS_NONE);
```

---

## Kamera problémy

### `0x105 ESP_ERR_NOT_FOUND` — senzor neodpovedá

| Príčina | Riešenie |
|---|---|
| FPC nie je zasunutý | Kontakty (medi) **dole** k PCB, latch zatvorený |
| `PWDN`/`RESET` nastavené na GPIO | Na tejto doske **nie sú zapojené** → `-1` |
| Zlý pin mapping | Použi `CAMERA_MODEL_ESP32S3_EYE` mapping |
| Zhorený modul | Vymeň kameru |

### `0x101 ESP_ERR_NO_MEM`

**Príčina:** PSRAM nie je aktivovaná.

**Riešenie:**
- Tools → PSRAM → **OPI PSRAM**
- FQBN: `PSRAM=opi`
- Over: `Serial.println(ESP.getPsramSize());` → musí byť `8388608`

### `cam_hal: FB-OVF` (frame buffer overflow)

```
cam_hal: FB-OVF
cam_hal: FB-OVF
```

**Príčina:** kamera generuje viac snímok, než server stíha spracovať.

| Riešenie | Efekt |
|---|---|
| `CAMERA_GRAB_WHEN_EMPTY` | Nulové FB-OVF, ale v MJPEG streame spôsobí `fb_get FAILED` |
| `CAMERA_GRAB_LATEST` + nižší XCLK | **Odporúčané** |
| Vyššie `fb_count` | Viac PSRAM, menej overflow |
| Nižšie rozlíšenie | Najúčinnejšie |

### `stream: fb_get FAILED`

**Príčina:** `CAMERA_GRAB_WHEN_EMPTY` + `fb_count = 2` v MJPEG streame.
Stream drží jeden buffer, kamera nemá kam zapisovať.

**Riešenie:**
```cpp
cfg.fb_count   = 2;
cfg.grab_mode  = CAMERA_GRAB_LATEST;
```

### Autofokus nefunguje

```cpp
Serial.printf("AF: %s\n", esp_camera_af_is_supported(s) ? "ANO" : "NIE");
// -> AF: NIE
```

**Overenie, že AF v knižnici chýba:**

```bash
nm -C libespressif__esp32-camera.a | sed -n '/ov5640_af.c.obj/,$p' | head -5
# ov5640_af.c.obj:
# (ziadne symboly = prazdny objekt)
```

**Riešenie:** mechanické zaostrenie objektívu.
Softvérový AF = ESP-IDF + `esp_cam_sensor` + `dw9714` VCM driver.

### Rozmazaný obraz aj po zaostrení

```cpp
sensor->set_sharpness(sensor, 2);   // 0..2+
sensor->set_denoise(sensor, 0);     // 0 = neodmazavat detaily
sensor->set_contrast(sensor, 1);
sensor->set_saturation(sensor, 1);
sensor->set_whitebal(sensor, 1);
sensor->set_gain_ctrl(sensor, 1);
sensor->set_exposure_ctrl(sensor, 1);
```

Plus znížiť `CAM_JPEG_QUALITY` (nižšie = lepšie).

---

## Výkon / teplota

### CPU frekvencia a teplo

**Nepredpokladaj, že nižšie CPU = menej tepla.**

JPEG enkódovanie je CPU-bound. Pri 160 MHz trvá frame dlhšie →
procesor strávi viac času prácou → **viac tepla pri rovnakom FPS**.

| CPU | FPS (VGA) | Teplota |
|---|---|---|
| 240 MHz | 11 | 62 °C |
| 160 MHz | 7 | 62 °C |

**Záver:** nechať 240 MHz, znižovať rozlíšenie.

### Najväčšie zdroje tepla

1. **WiFi TX power** — 19.5 dBm → 8.5 dBm = najväčší efekt
2. **Rozlíšenie / JPEG kvalita** — menej práce
3. **XCLK kamery** — 10 MHz → 16 MHz = +2 °C
4. **AP režim** — vysiela neustále, aj bez klientov

### Podozrenie na brownout

**Príznaky:**
- `rst:0x? (BROWNOUT_RST)` v boot logu
- Reštarty pri zapnutí WiFi alebo pri streamovaní
- Reštarty pri pohybe motorov (vozítko)

**Riešenie:**
- Napájaný USB hub alebo powerbank
- Kratší / hrubší USB kábel
- DC-DC step-down pre motory oddelene
- Kondenzátor 470 µF na 5 V

---

## Arduino IDE / CLI

### Upload zlyhá po zmene FQBN

```
Failed uploading: uploading error: exit status 2
```

**Príčina:** `arduino-cli` cachuje build podľa **plného FQBN**.
Ak sa čo i len jedna položka zmení, build sa nenájde.

**Riešenie:** najprv `compile` s presne tým istým FQBN, potom `upload`.

### Nájdenie build artefaktov

```bash
ls -t ~/.cache/arduino/sketches/*/ESP32S3_CAM_WEB.ino.bin | head -1
```

### Overenie FQBN možností

```bash
arduino-cli board details -b esp32:esp32:esp32s3 | sed 's/\x1b\[[0-9;]*m//g'
```

Dá všetky platné `Option:` hodnoty vrátane `PSRAM=opi`, `FlashSize=16M`, atď.

---

## Užitočné overenia stavu

### Rýchly test dosky

```cpp
Serial.printf("Chip      : %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
Serial.printf("Cores     : %d @ %lu MHz\n", ESP.getChipCores(), getCpuFrequencyMhz());
Serial.printf("Flash     : %lu MB\n", ESP.getFlashChipSize() >> 20);
Serial.printf("PSRAM     : %u B\n", ESP.getPsramSize());
Serial.printf("Free heap : %u B\n", ESP.getFreeHeap());
Serial.printf("SDK       : %s\n", ESP.getSdkVersion());
Serial.printf("Teplota   : %d C\n", (int)temperatureRead());
```

Očakávané hodnoty pre N16R8:
```
Chip      : ESP32-S3 rev 2
Cores     : 2 @ 240 MHz
Flash     : 16 MB
PSRAM     : 8388608 B
SDK       : v5.5.5
```

### Kontrola build konfigurácie v binárke

```bash
strings build/ESP32S3_CAM_WEB.ino.bin | grep -iE "qio|opi|psram"
```
