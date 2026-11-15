# ESP32-S3-CAM WiFi MJPEG Stream

Kamerový stream na **ESP32-S3-WROOM-1 N16R8** s kamerou **OV5640**, WiFi AP režim,
MJPEG stream a variabilný FPS regulátor.

Nabité praktickými poznatkami z reálneho oživovania dosky — vrátane vecí,
ktoré v dokumentácii nenájdeš a ktoré spôsobujú hodiny ladеnia.

---

## Obsah

- [Hardvér](#hardvér)
- [Rýchly štart](#rýchly-štart)
- [Nastavenia Arduino IDE](#nastavenia-arduino-ide)
- [HTTP API](#http-api)
- [Štruktúra projektu](#štruktúra-projektu)
- [Pin mapping](#pin-mapping)
- [Namerané parametre](#namerané-parametre)
- [Problémy a ich riešenia](#problémy-a-ich-riešenia)
- [Poznámky k vozítku](#poznámky-k-vozítku)
- [Známe obmedzenia](#známe-obmedzenia)
- [Úpravy](#úpravy)

---

## Hardvér

| Položka | Detail |
|---|---|
| Modul | ESP32-S3-WROOM-1 **N16R8** |
| Flash | 16 MB (quad, QIO) |
| PSRAM | 8 MB **OPI** (octal) |
| CPU | 2× Xtensa LX7 @ 240 MHz |
| Kamera | **OV5640** (5 MP, AF modul VVS-OV56) |
| Konektor kamery | 24-pin FPC 0.5 mm, DVP 8-bit |
| USB | 2× USB-C — **USB-OTG** (natívny) + **USB-UART** (CH343) |
| SD slot | microSD, SDMMC 1-bit |
| Tlačidlá | BOOT (GPIO0), RST (EN) |
| LED | GPIO2, aktívna HIGH |
| Varianty dosky | Freenove FNK0085, GOOUUU ESP32-S3-CAM, „ESP32-S3 CAM" |

### Dvojica USB konektorov — dôležité

| Konektor | Bridge | Port (Linux) | Použitie |
|---|---|---|---|
| **USB-OTG** (pravý) | natívny USB Serial/JTAG v S3 | `/dev/ttyACM0` | **✅ odporúčané** |
| **USB-UART** (ľavý) | CH343 (`1a86:55d3`) | `/dev/ttyACM0` cez `cdc_acm` | ⚠️ nestabilné |

**Na tejto doske je CH343 problematický** — viď [Problémy](#problémy-a-ich-riešenia).

---

## Rýchly štart

1. Otvor `ESP32S3_CAM_WEB/ESP32S3_CAM_WEB.ino` v Arduino IDE 2.x
2. Kábel do **pravého USB-C konektora (USB-OTG)**
3. Nahraj
4. Pripoj sa telefónom/PC na WiFi:

| | |
|---|---|
| **SSID** | `ESP32-S3-CAM` |
| **Heslo** | `12345678` |
| **IP** | `192.168.4.1` |

5. Prehliadač → `http://192.168.4.1/`

> Pripojením na AP **stratíš internet** na tom zariadení (AP nemá upstream).
> Ak potrebuješ internet + stream súčasne, prepni na STA režim (viď [Úpravy](#úpravy)).

---

## Nastavenia Arduino IDE

### Board Manager URL
```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Core: **esp32 by Espressif Systems 3.3.12** (alebo novší 3.x)

### Tools

```
Board             : ESP32S3 Dev Module
CPU Frequency     : 240MHz (WiFi)
Flash Mode        : QIO 80MHz
Flash Size        : 16MB (128Mb)
Partition Scheme  : 16M Flash (3MB APP/9.9MB FATFS)
PSRAM             : OPI PSRAM          ← KRITICKÉ
Upload Speed      : 921600
USB Mode          : Hardware CDC and JTAG
USB CDC On Boot   : Enabled
JTAG Adapter      : Disabled
Core Debug Level  : None
Erase All Flash   : Disabled
```

### FQBN

```
esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,\
PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,\
USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,\
DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,\
MSCOnBoot=default,DFUOnBoot=default
```

Súbor `sketch.json` má toto prednastavené vrátane portu.

### CLI build & upload

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,MSCOnBoot=default,DFUOnBoot=default" ESP32S3_CAM_WEB

arduino-cli upload -p /dev/ttyACM0 --fqbn "<rovnaký FQBN>" ESP32S3_CAM_WEB
```

> `arduino-cli` cachuje build podľa plného FQBN. Ak zmeníš čo i len jednu
> položku, musíš znova skompilovať, inak upload zlyhá.

---

## HTTP API

| Endpoint | Metóda | Popis |
|---|---|---|
| `/` | GET | Web UI — obraz, tlačidlá snímka/stream/AF, FPS 15/20/25/30/max |
| `/stream` | GET | MJPEG stream (`multipart/x-mixed-replace`) |
| `/capture` | GET | Jedna JPEG snímka |
| `/focus` | GET | Vynúti AF cyklus (v tejto verzii vracia 501) |
| `/fps?f=N` | GET | Nastaví cieľové FPS (5–30) |
| `/fps?f=auto` | GET | Zruší reguláciu — maximum kamery |

### Príklad

```bash
# zmena na 20 fps
curl "http://192.168.4.1/fps?f=20"
# -> fps=20

# uloženie snímky
curl -o snap.jpg "http://192.168.4.1/capture"

# stream do súboru (10 s)
timeout 10 curl -o stream.mjpeg "http://192.168.4.1/stream"
```

### FPS regulátor

Time-based pacing v `stream_handler()`:

```cpp
int64_t period_us = 1000000LL / target;
int64_t wait = next_frame_us - esp_timer_get_time();
if (wait > 0) vTaskDelay(pdMS_TO_TICKS((wait + 999) / 1000));
next_frame_us += period_us;
```

- Drží cieľové FPS presne (±0.5)
- Ak kamera nestíha, ide max rýchlosťou (nikdy nepodtaktuje pod realitu)
- Pri zaostávaní > 2 periódy sa plán resetuje (nezasekne sa)

---

## Štruktúra projektu

```
esp32s3-cam-stream/
├── README.md
├── .gitignore
├── ESP32S3_CAM_WEB/
│   ├── ESP32S3_CAM_WEB.ino   # WiFi AP, init kamery, ladenie obrazu
│   ├── app_httpd.h            # HTTP server, MJPEG, FPS regulátor, UI
│   ├── camera_pins.h          # Pin mapping (overený na HW)
│   └── sketch.json            # FQBN + port pre IDE 2
└── docs/
    └── TROUBLESHOOTING.md     # Detailná diagnostika problémov
```

---

## Pin mapping

**Identický s `CAMERA_MODEL_ESP32S3_EYE`** z Arduino-ESP32 core.

### Kamera (24-pin FPC)

| Signál | GPIO | Poznámka |
|---|---|---|
| XCLK | 15 | generuje S3 cez LEDC |
| PCLK | 13 | |
| VSYNC | 6 | |
| HREF | 7 | |
| SIOD (SCCB SDA) | 4 | |
| SIOC (SCCB SCL) | 5 | |
| D0 (Y2) | 11 | |
| D1 (Y3) | 9 | |
| D2 (Y4) | 8 | |
| D3 (Y5) | 10 | |
| D4 (Y6) | 12 | |
| D5 (Y7) | 18 | |
| D6 (Y8) | 17 | |
| D7 (Y9) | 16 | |
| **PWDN** | **-1** | **nie je zapojené** |
| **RESET** | **-1** | **nie je zapojené** |

> Poradie D0–D7 je **nemonotonické** — nepredpokladaj, že D0=11, D1=12, …

### SD karta (SDMMC 1-bit)

| Signál | GPIO |
|---|---|
| CLK | 39 |
| CMD | 38 |
| D0 | 40 |

> Pri mounte SD sa zaberú 3 zo 4 JTAG padov (39/38/40). USB-Serial-JTAG
> na natívnom porte to neovplyvní. Karta musí byť **FAT32**.

### Ostatné

| Funkcia | GPIO | Poznámka |
|---|---|---|
| On-board LED | 2 | aktívna HIGH |
| BOOT tlačidlo | 0 | strapping |
| **OPI PSRAM** | **35, 36, 37** | **NIKDY nepoužívať!** |

---

## Namerané parametre

Konfigurácia: QVGA 320×240, JPEG q12, XCLK 16 MHz, CPU 240 MHz, TX 8.5 dBm

| Parameter | Hodnota |
|---|---|
| **FPS** | 12–15 (strop OV5640 modulu) |
| **Teplota idle** | 61–63 °C |
| **Teplota stream** | 64–67 °C |
| **PSRAM voľná** | 8 386 096 B / 8 388 608 B |
| **Flash** | 16 MB, sketch 31 % (988 KB) |
| **WiFi kanál** | 6 |
| **TX power** | 8.5 dBm |
| **Motivácia pre TX power** | 19.5 dBm = najväčší zdroj tepla |

### Porovnanie konfigurácií (reálne merania)

| Rozlíšenie | XCLK | CPU | FPS | Teplota |
|---|---|---|---|---|
| SVGA 800×600 | 10 MHz | 240 | ~7 | 67 °C |
| VGA 640×480 | 10 MHz | 160 | 5–10 | 62 °C |
| CIF 400×296 | 10 MHz | 240 | 11 | 62 °C |
| CIF 400×296 | 20 MHz | 240 | 15 | 67 °C |
| **QVGA 320×240** | **16 MHz** | **240** | **12–15** | **65 °C** |

> **CPU 160 MHz je kontraproduktívne**: JPEG enkódovanie je CPU-bound,
> nižšie CPU = dlhšia práca na frame = viac tepla pri rovnakom FPS.

---

## Problémy a ich riešenia

Detailná diagnostika je v [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).
Tu sú najdôležitejšie:

### 1. Nahrávanie cez USB-UART (CH343) zlyháva

**Príznaky:**
```
A fatal error occurred: Failed to connect to ESP32-S3:
Wrong boot mode detected (0x8)! The chip needs to be in download mode.
```
alebo
```
A fatal error occurred: Invalid head of packet (0x45):
Possible serial noise or corruption.
```
Niekedy doska skončí v **boot loope**:
```
rst:0x7 (TG0WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
mode:QIO, clock div:1
ets_loader.c 78
```

**Príčina:** CH343 reset obvod na tejto doske má slabé časovanie.
`0x45` = `'E'` z `ESP-ROM:` — boot log sa dostane do packet streamu.

**Riešenie:** **Nahrávaj cez USB-OTG (pravý konektor).** Ide na prvý pokus.

Manuálny download mode (ak treba):
```
Podrž BOOT → klikni RST → drž 3 s → pusti BOOT
```

---

### 2. WiFi AP sa nevysiela — telefón sieť nevidí

**Príznak:** `WiFi.softAP()` vráti `true`, IP nastavená, ale sieť nie je viditeľná.
V diagnostike: `TX power (API): 0.0 dBm`.

**Príčina:** ESP32-S3 v Arduino core 3.x nechá `txpower = 0` — rádio
nevysiela, hoci API hlási úspech.

**Riešenie:**
```cpp
WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);
esp_wifi_set_max_tx_power(34);   // 34 = 8.5 dBm, 78 = 19.5 dBm
```
Musí byť **po** `softAP()`. Nastavuje sa cez `esp_wifi.h`, nie `WiFi.h`.

---

### 3. Kanál 13 odmietnutý

**Príznak:**
```
E wifi:(2G)Channel number 13 is out of range from 1 to 11 in country code cc=01
```

**Príčina:** default krajina je `cc=01` (USA) → len kanály 1–11.

**Riešenie:**
```cpp
wifi_country_t country = {};
strncpy(country.cc, "SK", sizeof(country.cc));
country.schan = 1;
country.nchan = 13;
country.max_tx_power = 34;
country.policy = WIFI_COUNTRY_POLICY_MANUAL;

WiFi.mode(WIFI_AP);
esp_wifi_set_country(&country);   // PRED softAP()
WiFi.softAP(AP_SSID, AP_PASS, 13, 0, 4);
```

> `WiFi.scanNetworks()` vypne AP — po skene treba AP znova spustiť
> aj s `esp_wifi_set_country()` a `esp_wifi_set_max_tx_power()`.

---

### 4. Stream padá na `fb_get FAILED`

**Príznak:** v sériovom výstupe `stream: fb_get FAILED`, obraz sa nenačíta.

**Príčina:** `CAMERA_GRAB_WHEN_EMPTY` + 2 framebuffery. Keď stream drží
jeden buffer, kamera nemá kam zapisovať a `esp_camera_fb_get()` vráti NULL.

**Riešenie:** `CAMERA_GRAB_LATEST` + aspoň 2 buffery:
```cpp
cfg.fb_count   = 2;
cfg.grab_mode  = CAMERA_GRAB_LATEST;
```

---

### 5. `cam_hal: FB-OVF` zaplaví sériový výstup

**Príznak:** stovky riadkov `FB-OVF` — frame buffer overflow.

**Príčina:** kamera generuje snímky rýchlejšie, než ich server stíha odosielať.

**Riešenie:** znížiť XCLK, alebo `CAMERA_GRAB_WHEN_EMPTY`.
Pozor — `WHEN_EMPTY` kombinovaný s MJPEG streamom spôsobí problém 4.
Najlepšie: XCLK 10–16 MHz + `GRAB_LATEST`.

---

### 6. Autofokus nefunguje — `AF podpora: NIE`

**Príznak:** `esp_camera_af_is_supported()` vráti `false`, obraz je rozmazaný.

**Príčina:** V `esp32-camera` 3.3.12 je objekt `ov5640_af.c.obj` **prázdny** —
AF API existuje, ale pre OV5640 nie je implementované. Overenie:

```bash
nm -C libespressif__esp32-camera.a | sed -n '/ov5640_af.c.obj/,$p' | head
# ov5640_af.c.obj:
# (prázdny výstup)
```

**Riešenie:** **mechanické zaostrenie.** Na module je závit s poistným
krúžkom — povoľ ho, otáčaj objektívom pri živom streame, dotiahni.

Softvérový AF by vyžadoval ESP-IDF s `esp_cam_sensor` (VCM driver `dw9714`),
nie Arduino `esp32-camera`.

---

### 7. Obraz je „otrasný" aj po zaostrení

Skontroluj v tomto poradí:
1. `CAM_JPEG_QUALITY` — nižšie číslo = lepšia kvalita (používame 12; 4 = max)
2. `set_sharpness(sensor, 2)` — výrazne pomôže
3. `set_denoise(sensor, 0)` — default odmazáva detaily
4. Osvetlenie — OV5640 v šere šumí
5. `AWB/AGC/AEC = 1` (zapnuté)

---

## Poznámky k vozítku

### Čo funguje
- WiFi AP stream na telefón — dosť na navigáciu
- 15 FPS pri QVGA — pre pomalé vozítko OK
- Latencia ~100–200 ms (MJPEG, WiFi)
- Teplota 65 °C — pre S3 bezpečná (limit 105 °C)

### Na čo pozor
1. **Zaostri objektív** — inak nevidíš prekážky
2. **AP režim blokuje internet** na telefóne
3. **Napájanie**: motory na samostatný zdroj. Pri spoločnej batérii
   treba DC-DC step-down 5 V/2 A — špičky z motorov inak resetujú dosku (brownout)
4. **FPS 15 je limit** — pri rýchlosti >~1 m/s bude obraz trhaný
5. **MJPEG stream blokuje HTTP server** — ovládacie endpointy musia ísť
   z iného spojenia alebo na inom porte

### Odporúčané vylepšenia
- **STA režim** namiesto AP — doska na domáci router, mDNS (`esp32-cam.local`)
- **`/status`** endpoint (JSON: teplota, fps, uptime, RAM)
- **`/gpio`** endpoint na ovládanie pinov (relé, smer motora)
- **Oddeliť stream a ovládanie** na rôzne porty

---

## Známe obmedzenia

| Obmedzenie | Detail | Riešenie |
|---|---|---|
| **Max ~15 FPS** | Strop OV5640 modulu/FPC, nie nastavení. Skúšané 10/16/20 MHz XCLK, CIF/QVGA — bez zmeny | Vymeniť za **OV2640** (25–30 FPS pri VGA) |
| **Bez autofokusu** | `ov5640_af.c.obj` prázdny v core 3.3.12 | Mechanické zaostrenie |
| **CH343 nestabilný** | Nahrávanie padá | USB-OTG |
| **AP bez internetu** | AP nemá upstream | STA režim |
| **SD karta nepripojená** | `0x107` = karta neodpovedá | Vložiť FAT32 kartu |

---

## Úpravy

Všetky nastavenia sú v hlavičkách súborov:

### `ESP32S3_CAM_WEB.ino`

```cpp
// WiFi AP
static const char* AP_SSID = "ESP32-S3-CAM";
static const char* AP_PASS = "12345678";

// Výkon / teplo
#define CPU_FREQ_MHZ    240      // 240 = optimálne (160 = viac tepla pri rovnakom FPS)
#define WIFI_TX_POWER   34       // 34 = 8.5 dBm, 78 = 19.5 dBm
#define AP_CHANNEL      6

// Kvalita obrazu
#define CAM_FRAMESIZE   FRAMESIZE_QVGA   // QVGA / CIF / VGA / SVGA
#define CAM_JPEG_QUALITY 12              // 0–63, nižšie = lepšie
#define CAM_XCLK_HZ     16000000         // 10 / 16 / 20 MHz
```

### `app_httpd.h`

```cpp
#define TARGET_FPS_DEFAULT  15
#define TARGET_FPS_MIN      5
#define TARGET_FPS_MAX      30
```

### Prepnutie AP → STA režim

V `setup()` nahraď blok s `WiFi.mode(WIFI_AP)`:

```cpp
WiFi.mode(WIFI_STA);
WiFi.setSleep(false);
WiFi.begin("TVOJA_SSID", "TVOJE_HESLO");
while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
Serial.print("IP: "); Serial.println(WiFi.localIP());
```

A v `CDCOnBoot` nič nemeniť. `txpower` je v STA režime riešený automaticky.

---

## Licencia

MIT — voľne použiteľné.

## Autor

Vytvorené pri oživovaní ESP32-S3-CAM dosky (Freenove/GOOUUU klon, N16R8),
2026.
