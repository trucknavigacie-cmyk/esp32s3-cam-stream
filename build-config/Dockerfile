# Docker — reprodukovatelne build prostredie
#
# Build:
#   docker run --rm -v "$PWD:/work" -w /work esp32-build \
#     arduino-cli compile --fqbn "$FQBN" ESP32S3_CAM_WEB
#
# Upload (Linux, USB-OTG na /dev/ttyACM0):
#   docker run --rm -it --device=/dev/ttyACM0 \
#     -v "$PWD:/work" -w /work esp32-build \
#     arduino-cli upload -p /dev/ttyACM0 --fqbn "$FQBN" ESP32S3_CAM_WEB

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV ARDUINO_CLI_VERSION=1.6.0
ENV ESP32_CORE_VERSION=3.3.12

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 python3-pip unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

# arduino-cli
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
      | BINDIR=/usr/local/bin sh \
    && arduino-cli version

# esp32 core
RUN arduino-cli config init \
    && arduino-cli config set board_manager.additional_urls \
         https://espressif.github.io/arduino-esp32/package_esp32_index.json \
    && arduino-cli config set directories.data /root/.arduino15 \
    && arduino-cli config set directories.user /root/Arduino \
    && arduino-cli core update-index \
    && arduino-cli core install esp32:esp32@${ESP32_CORE_VERSION} \
    && arduino-cli core list

# esptool na manualny flash
RUN pip3 install --break-system-packages esptool

ENV FQBN="esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,JTAGAdapter=default,DebugLevel=none,EraseFlash=none,LoopCore=1,EventsCore=1,MSCOnBoot=default,DFUOnBoot=default"

WORKDIR /work

CMD ["/bin/bash"]
