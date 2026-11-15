#!/usr/bin/env bash
# Nastavenie prostredia pre ESP32-S3-CAM projekt od nuly.
#
# Pouzitie:
#   ./setup-environment.sh          # nainstaluje core + skontroluje
#   ./setup-environment.sh --check  # len overi stav, nic nemeni
#
# Testovane na Ubuntu 26.04.1 LTS, arduino-cli 1.6.0.

set -euo pipefail

ESP32_INDEX="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
ESP32_CORE="esp32:esp32@3.3.12"
USER_DIR="${HOME}/Arduino"

FQBN="esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,MSCOnBoot=default,DFUOnBoot=default"

check_only=0
[[ "${1:-}" == "--check" ]] && check_only=1

info()  { printf '\033[1;34m[INFO]\033[0m %s\n' "$*"; }
ok()    { printf '\033[1;32m[ OK ]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[WARN]\033[0m %s\n' "$*"; }
fail()  { printf '\033[1;31m[FAIL]\033[0m %s\n' "$*"; }

# ---------------------------------------------------------------- 1. nastroje
info "Kontrola nastrojov..."

if ! command -v arduino-cli >/dev/null 2>&1; then
  if [[ $check_only -eq 1 ]]; then
    fail "arduino-cli chyba"
  else
    warn "arduino-cli nie je nainstalovany, instalujem..."
    mkdir -p "${HOME}/.local/bin"
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
      | BINDIR="${HOME}/.local/bin" sh
    export PATH="${HOME}/.local/bin:${PATH}"
  fi
fi

if command -v arduino-cli >/dev/null 2>&1; then
  ok "arduino-cli $(arduino-cli version | awk '{print $3}')"
fi

command -v git >/dev/null 2>&1 && ok "git $(git --version | awk '{print $3}')" || fail "git chyba"

# ---------------------------------------------------------------- 2. dialout
if id -nG "$USER" | grep -qw dialout; then
  ok "pouzivatel je v skupine dialout"
else
  warn "pouzivatel NIE JE v skupine dialout — port sa nebude dat otvorit"
  echo "      sudo usermod -aG dialout \$USER   (potom sa odhlasit/prihlasit)"
fi

# ---------------------------------------------------------------- 3. config
info "Konfiguracia arduino-cli..."
CONFIG_DIR="${HOME}/.arduino15"
mkdir -p "$CONFIG_DIR"

if [[ $check_only -eq 0 ]]; then
  arduino-cli config init --overwrite >/dev/null 2>&1 || true
  arduino-cli config set board_manager.additional_urls "$ESP32_INDEX"
  arduino-cli config set directories.data "$CONFIG_DIR"
  arduino-cli config set directories.user "$USER_DIR"
  mkdir -p "$USER_DIR"
  ok "board manager URL nastavena"
fi

if arduino-cli config dump 2>/dev/null | grep -q "espressif.github.io"; then
  ok "esp32 index URL pritomny"
else
  fail "esp32 index URL chyba"
fi

# ---------------------------------------------------------------- 4. core
info "Kontrola esp32 core..."
if arduino-cli core list 2>/dev/null | grep -q "^esp32:esp32"; then
  have=$(arduino-cli core list 2>/dev/null | awk '/^esp32:esp32/{print $2}')
  ok "esp32 core nainstalovany: $have"
  if [[ "$have" != "3.3.12" ]]; then
    warn "projekt je testovany s 3.3.12, mas $have"
  fi
else
  if [[ $check_only -eq 1 ]]; then
    fail "esp32 core chyba — spusti bez --check"
  else
    info "Instalujem ${ESP32_CORE} (par minut)..."
    arduino-cli core update-index
    arduino-cli core install "$ESP32_CORE"
    ok "esp32 core nainstalovany"
  fi
fi

# ---------------------------------------------------------------- 5. tools
info "Kontrola Espressif toolov..."
for t in esptool_py esp-x32 esp32s3-libs; do
  if [[ -d "${CONFIG_DIR}/packages/esp32/tools/${t}" ]]; then
    ok "${t}: $(ls "${CONFIG_DIR}/packages/esp32/tools/${t}" | head -1)"
  else
    fail "${t} chyba"
  fi
done

# ---------------------------------------------------------------- 6. port
info "Kontrola seriovych portov..."
found=0
for p in /dev/ttyACM* /dev/ttyUSB*; do
  [[ -e "$p" ]] || continue
  found=1
  drv=$(udevadm info -q property -n "$p" 2>/dev/null | awk -F= '/ID_USB_DRIVER/{print $2}')
  vid=$(udevadm info -q property -n "$p" 2>/dev/null | awk -F= '/ID_VENDOR_ID/{print $2}')
  printf '      %s  driver=%-10s vid=%-6s' "$p" "${drv:-?}" "${vid:-?}"
  case "$vid" in
    303a) echo " -> USB-OTG (odporucane)" ;;
    1a86) echo " -> CH343 USB-UART (na tejto doske nestabilne)" ;;
    10c4) echo " -> CP210x" ;;
    *)    echo "" ;;
  esac
done
[[ $found -eq 0 ]] && warn "ziadny port — pripoj dosku cez USB-OTG"

# ---------------------------------------------------------------- 7. FQBN
info "Validacia FQBN..."
if arduino-cli board details -b esp32:esp32:esp32s3 2>/dev/null | grep -q "PSRAM=opi"; then
  ok "OPI PSRAM dostupna v board definition"
else
  fail "PSRAM=opi nie je dostupna — mas prilis stary core"
fi

echo
echo "FQBN pre tento projekt:"
echo "  $FQBN"
echo
echo "Build:"
echo "  arduino-cli compile --fqbn \"\$FQBN\" ESP32S3_CAM_WEB"
echo "Upload:"
echo "  arduino-cli upload -p /dev/ttyACM0 --fqbn \"\$FQBN\" ESP32S3_CAM_WEB"
echo
ok "Hotovo."
