# Konfigurácia prostredia

Všetko potrebné na reprodukciu buildu — od nuly na čistom systéme.

## Obsah

| Súbor | Na čo je |
|---|---|
| [VERSIONS.md](VERSIONS.md) | **Presné verzie** všetkého, s čím bol projekt stavaný |
| [setup-environment.sh](setup-environment.sh) | Automatické nastavenie prostredia (arduino-cli + core + validácia) |
| [arduino-cli.yaml](arduino-cli.yaml) | Referenčný `~/.arduino15/arduino-cli.yaml` |
| [ide2-settings.md](ide2-settings.md) | Arduino IDE 2 — Tools menu pre tento projekt |
| [vscode-settings.json](vscode-settings.json) | VS Code + Arduino extension |
| [platformio.ini](platformio.ini) | PlatformIO alternativa (bez Arduino IDE) |
| [Dockerfile](Dockerfile) | Reprodukovateľné build prostredie v kontajneri |
| [../.github/workflows/build.yml](../.github/workflows/build.yml) | GitHub Actions — automatický build pri každom pushi |

---

## Rýchle nastavenie (Linux)

```bash
cd build-config
chmod +x setup-environment.sh
./setup-environment.sh
```

Skript:
- nainštaluje `arduino-cli` (ak chýba)
- nastaví `board_manager.additional_urls`
- nainštaluje `esp32:esp32@3.3.12`
- overí `dialout` skupinu
- nájde a identifikuje USB porty (USB-OTG vs CH343)
- overí, že `PSRAM=opi` je dostupné
- vypíše hotový FQBN

Len kontrola bez zmien:

```bash
./setup-environment.sh --check
```

---

## Manuálne nastavenie

### 1. arduino-cli

```bash
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
  | BINDIR=~/.local/bin sh
export PATH="$HOME/.local/bin:$PATH"
```

### 2. Board manager URL

```bash
arduino-cli config init
arduino-cli config set board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli config set directories.data ~/.arduino15
arduino-cli config set directories.user ~/Arduino
```

### 3. ESP32 core

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
```

### 4. Práva na port

```bash
sudo usermod -aG dialout $USER
# odhlásiť sa a prihlásiť
```

### 5. Overenie

```bash
arduino-cli core list
arduino-cli board details -b esp32:esp32:esp32s3 | sed 's/\x1b\[[0-9;]*m//g' | grep PSRAM
```

Očakávané:
```
esp32:esp32  3.3.12  3.3.12  esp32
               Disabled     PSRAM=disabled
               QSPI PSRAM   PSRAM=enabled
               OPI PSRAM    PSRAM=opi        ← toto potrebujeme
```

---

## Build a upload

```bash
FQBN="esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,MSCOnBoot=default,DFUOnBoot=default"

# build
arduino-cli compile --fqbn "$FQBN" ESP32S3_CAM_WEB

# upload (USB-OTG)
arduino-cli upload -p /dev/ttyACM0 --fqbn "$FQBN" ESP32S3_CAM_WEB
```

> ⚠️ `arduino-cli` cachuje build **podľa plného FQBN**. Ak zmeníš čo i len
> jednu položku, musíš najprv `compile`, potom `upload` s tým istým FQBN.

---

## Docker (bez inštalácie čohokoľvek)

```bash
cd build-config
docker build -t esp32-build .

# build
docker run --rm -v "$PWD/..:/work" -w /work esp32-build \
  sh -c 'arduino-cli compile --fqbn "$FQBN" ESP32S3_CAM_WEB'

# upload
docker run --rm -it --device=/dev/ttyACM0 \
  -v "$PWD/..:/work" -w /work esp32-build \
  sh -c 'arduino-cli upload -p /dev/ttyACM0 --fqbn "$FQBN" ESP32S3_CAM_WEB'
```

---

## PlatformIO

```bash
pip install platformio
pio run                 # build
pio run -t upload       # upload
pio device monitor      # serial 115200
```

**Kritické nastavenie** v `platformio.ini`:

```ini
board_build.arduino.memory_type = qio_opi   ; quad flash + OCTAL psram
build_flags = -DBOARD_HAS_PSRAM             ; bez toho psramFound() == false
```

Bez týchto dvoch riadkov PSRAM **nikdy nefunguje** — default board definícia
je N8 bez PSRAM (`qio_qspi`).

---

## GitHub Actions

Pri každom pushi do `main` (ak sa zmenil `ESP32S3_CAM_WEB/**`) sa:
1. nainštaluje `arduino-cli` + `esp32@3.3.12`
2. skompiluje firmware
3. uloží binárky ako artifact (`firmware-<sha>`, 30 dní)

Manuálne spustenie: Actions → Build firmware → Run workflow.

---

## Čo je v prostredí (zistené)

Pozri [VERSIONS.md](VERSIONS.md). Skrátene:

| | |
|---|---|
| Ubuntu | 26.04.1 LTS, kernel 7.0.0-34 |
| arduino-cli | 1.6.0 |
| Arduino IDE | 2.3.10 (Flatpak `cc.arduino.IDE2`) |
| esp32 core | 3.3.12 |
| esptool | 5.3.1 |
| GCC (esp-x32) | 2601 |
| ESP-IDF SDK | v5.5.5 |
