#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

/*
  Pin mapping pre ESP32-S3-WROOM-1 N16R8 "CAM" dosku
  (Freenove FNK0085 / GOOUUU ESP32-S3-CAM / 2x USB-C / SD slot)

  = identicke s CAMERA_MODEL_ESP32S3_EYE z Arduino-ESP32 core.
  Otestovane ako rovnaky layout, ale definujeme explicitne,
  aby bolo jasne, odkial piny su.

  24-pin FPC, kontakty smerom k PCB:
    XCLK  = GPIO15
    PCLK  = GPIO13
    VSYNC = GPIO6
    HREF  = GPIO7
    SIOD  = GPIO4
    SIOC  = GPIO5
    D0..D7 (Y2..Y9) = 11, 9, 8, 10, 12, 18, 17, 16   <- pozor, nemonotonicke

  PWDN a RESET nie su na tejto doske zapojene -> MUSIA byt -1.

  SD karta (SDMMC, 1-bit):
    CLK = GPIO39, CMD = GPIO38, D0 = GPIO40

  Pozor: GPIO35/36/37 = OPI PSRAM (N16R8) -> nikdy nepouzivat.
*/

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5

#define Y2_GPIO_NUM    11   // D0
#define Y3_GPIO_NUM    9    // D1
#define Y4_GPIO_NUM    8    // D2
#define Y5_GPIO_NUM    10   // D3
#define Y6_GPIO_NUM    12   // D4
#define Y7_GPIO_NUM    18   // D5
#define Y8_GPIO_NUM    17   // D6
#define Y9_GPIO_NUM    16   // D7

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// SD karta (SDMMC 1-bit)
#define SD_CLK_GPIO_NUM 39
#define SD_CMD_GPIO_NUM 38
#define SD_D0_GPIO_NUM  40

// On-board LED (aktivna HIGH)
#define BOARD_LED_GPIO_NUM 2

#endif
