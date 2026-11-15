/*
  ESP32-S3-WROOM-1 N16R8 CAM — AP rezim + MJPEG, s AUTOFOKUSOM (OV5640)

  Doska: ESP32S3 Dev Module (N16R8, OPI PSRAM)
  Kabel: USB-OTG (pravy konektor) -> /dev/ttyACM0

  AP:
    SSID     : ESP32-S3-CAM
    Heslo    : 12345678
    IP dosky : 192.168.4.1

  URL:
    http://192.168.4.1/          — web s obrazom
    http://192.168.4.1/stream    — MJPEG stream
    http://192.168.4.1/capture   — jedna JPEG snimka
    http://192.168.4.1/focus     — vynuti autofokus

  Dovod zlej kvality predtym: modul ma AF (VVS-OV56 ... AF), ale AF
  sa nikdy neinicializoval -> objektiv bol nezaostreny.
*/

#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_camera.h"
#include "esp_camera_af.h"
#include "camera_pins.h"
#include "app_httpd.h"

// ====== AP nastavenia ======
static const char* AP_SSID = "ESP32-S3-CAM";
static const char* AP_PASS = "12345678";   // min. 8 znakov (WPA2)

// ====== Tepelna/energeticka optimalizacia ======
// CPU 240 MHz: enkodovanie JPEG je na S3 CPU-bound. Nizsie CPU = dlhsia
// praca na frame = VIAC tepla pri rovnakom fps. 240 MHz je energeticky lepsie.
#define CPU_FREQ_MHZ    240
// WiFi TX power: 34 = 8.5 dBm (default max je 78 = 19.5 dBm — najvacsi zdroj tepla!)
#define WIFI_TX_POWER   34
#define AP_CHANNEL      6

// ====== Kvalita obrazu ======

#define CAM_FRAMESIZE   FRAMESIZE_QVGA    // 320x240 — 1.5x menej pixelov ako CIF
#define CAM_JPEG_QUALITY 12     // 0-63, vyssie = mensi subor/menej tepla
#define CAM_XCLK_HZ     16000000  // 16 MHz: kompromis fps vs. teplo kamery

static bool camReady = false;
static bool afReady  = false;
static sensor_t* camSensor = nullptr;

const char* sensorName(sensor_t* s) {
  if (!s) return "unknown";
  switch (s->id.PID) {
    case OV2640_PID: return "OV2640";
    case OV3660_PID: return "OV3660";
    case OV5640_PID: return "OV5640";
    case GC2145_PID: return "GC2145";
    case GC0308_PID: return "GC0308";
    default:         return "other/unknown";
  }
}

bool initCamera() {
  camera_config_t cfg = {};
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;

  cfg.xclk_freq_hz = CAM_XCLK_HZ;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;

  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = CAM_FRAMESIZE;
  cfg.jpeg_quality = CAM_JPEG_QUALITY;
  cfg.fb_count     = 2;             // 2 buffery: jeden sa plni, druhy sa odosiela
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  // LATEST: server vzdy dostane najnovsi frame
  cfg.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("Kamera init FAILED: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }

  camSensor = esp_camera_sensor_get();
  Serial.printf("Kamera OK: PID 0x%x (%s)\n",
                camSensor ? camSensor->id.PID : 0, sensorName(camSensor));
  return camSensor != nullptr;
}

// Zapne autofokus a doladi obraz (ostrost, kontrast, saturaciu)
void setupImageQuality() {
  if (!camSensor) return;

  // --- Autofokus ---
  afReady = esp_camera_af_is_supported(camSensor);
  Serial.printf("AF podpora: %s\n", afReady ? "ANO" : "NIE");
  if (afReady) {
    esp_camera_af_config_t afc = {};
    afc.mode       = ESP_CAMERA_AF_MODE_AUTO;
    afc.step_size  = 0;
    afc.range_min  = 0;
    afc.range_max  = 1023;
    afc.timeout_ms = 3000;

    esp_err_t e = esp_camera_af_init(camSensor, &afc);
    Serial.printf("  AF init    : %s\n", esp_err_to_name(e));
    e = esp_camera_af_set_mode(camSensor, ESP_CAMERA_AF_MODE_AUTO);
    Serial.printf("  AF mode    : %s\n", esp_err_to_name(e));
    e = esp_camera_af_trigger(camSensor);
    Serial.printf("  AF trigger : %s\n", esp_err_to_name(e));

    esp_camera_af_status_t st = {};
    e = esp_camera_af_wait(camSensor, 3000, &st);
    Serial.printf("  AF wait    : %s (focused=%d, raw=%u)\n",
                  esp_err_to_name(e), st.focused, st.raw);
  }

  // --- Ostrost a ladenie obrazu (OV5640) ---
  // Ostrost je pri nezaostrenom modulu najdolezitejsi parameter.
  if (camSensor->set_sharpness) {
    esp_err_t e = camSensor->set_sharpness(camSensor, 2);  // 0..2+, vyssie = ostrejsie
    Serial.printf("  sharpness  : %s\n", esp_err_to_name(e));
  }
  if (camSensor->set_denoise) {
    esp_err_t e = camSensor->set_denoise(camSensor, 0);    // 0 = bez odmazavania detailov
    Serial.printf("  denoise    : %s\n", esp_err_to_name(e));
  }
  camSensor->set_special_effect(camSensor, 0); // 0 = no effect
  camSensor->set_whitebal(camSensor, 1);       // AWB on
  camSensor->set_gain_ctrl(camSensor, 1);      // AGC on
  camSensor->set_exposure_ctrl(camSensor, 1);  // AEC on
  camSensor->set_brightness(camSensor, 0);
  camSensor->set_contrast(camSensor, 1);
  camSensor->set_saturation(camSensor, 1);
  camSensor->set_hmirror(camSensor, 0);
  camSensor->set_vflip(camSensor, 0);

  Serial.println("Senzor: sharpness 2, denoise 0, AWB/AGC/AEC on");
  Serial.println("POZOR: AF nie je dostupny v tejto verzii esp32-camera,");
  Serial.println("       objektiv treba zaostrit mechanicky (poistny srobik).");
}

void runAutofocus() {
  if (!afReady || !camSensor) return;
  esp_camera_af_trigger(camSensor);
  esp_camera_af_status_t st = {};
  esp_camera_af_wait(camSensor, 3000, &st);
  Serial.printf("AF: focused=%d raw=%u\n", st.focused, st.raw);
}

void setup() {
  // CPU frekvencia DNU — menej tepla. Volat co najskor.
  setCpuFrequencyMhz(CPU_FREQ_MHZ);

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("====== ESP32-S3 CAM :: AP + MJPEG + AF ======");
  Serial.printf("CPU: %lu MHz\n", getCpuFrequencyMhz());
  Serial.printf("PSRAM: %u B free / %u B total\n",
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getPsramSize());

  camReady = initCamera();
  if (!camReady) {
    Serial.println("Bez kamery stream nema zmysel — koncim.");
    return;
  }
  setupImageQuality();

  WiFi.persistent(false);

  // Krajina MUSI byt nastavena pred softAP(), inak S3 pouzije cc=01 (USA)
  // a dovoli len kanaly 1-11. Europa = kanaly 1-13.
  wifi_country_t country = {};
  strncpy(country.cc, "SK", sizeof(country.cc));
  country.schan = 1;
  country.nchan = 13;
  country.max_tx_power = WIFI_TX_POWER;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  esp_err_t ce = esp_wifi_set_country(&country);
  Serial.printf("Krajina SK: %s\n", esp_err_to_name(ce));

  IPAddress ip(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, mask);

  // Kanal 6 + WPA2, max 4 klienti, skryta siet = false
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, 2);
  if (!ok) {
    Serial.println("softAP start FAILED — skusam bez hesla...");
    ok = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, 0, 2);
    if (!ok) {
      Serial.println("softAP zlyhal uplne — WiFi modul problem");
      return;
    }
    Serial.println("softAP OK ale BEZ HESLA (open network)");
  }

  // Diagnostika
  wifi_mode_t m = WiFi.getMode();
  Serial.printf("WiFi mode      : %d (2=AP)\n", (int)m);
  Serial.printf("AP kanal       : %d\n", WiFi.channel());
  Serial.printf("AP MAC         : %s\n", WiFi.softAPmacAddress().c_str());
  // --- Kritical: S3 v core 3.x casto necha txpower = 0 -> AP nevysiela ---
  // 34 = 8.5 dBm: staci na miestnost, vyrazne menej tepla ako 19.5 dBm.
  esp_err_t e1 = esp_wifi_set_max_tx_power(WIFI_TX_POWER);
  int8_t txp = 0;
  esp_wifi_get_max_tx_power(&txp);
  Serial.printf("TX power set  : %s, teraz %d (%.1f dBm)\n",
                esp_err_to_name(e1), txp, txp * 0.25);
  Serial.printf("TX power (API) : %.1f dBm\n", WiFi.getTxPower());

  // vypnut power-save pre AP (inak klienti padaju)
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.println("-------------------------------------------");
  Serial.printf("AP SSID : %s\n", AP_SSID);
  Serial.printf("AP pass : %s\n", AP_PASS);
  Serial.print ("AP IP   : ");
  Serial.println(WiFi.softAPIP());
  Serial.println("-------------------------------------------");
  Serial.println("  http://192.168.4.1/         web");
  Serial.println("  http://192.168.4.1/stream  MJPEG");
  Serial.println("  http://192.168.4.1/capture snimka");
  Serial.println("  http://192.168.4.1/focus   vynutit AF");
  Serial.println("===========================================");

  if (!startCameraServer()) Serial.println("HTTP server sa nepodarilo spustit.");

  // Sken okolia — overi, ci WiFi radio vobec funguje
  Serial.println("Skenujem okolie (5 s)...");
  int n = WiFi.scanNetworks();
  Serial.printf("Najdenych sieti: %d\n", n);
  for (int i = 0; i < n && i < 10; i++) {
    Serial.printf("  %-20s  %d dBm  ch%d\n",
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }
  WiFi.scanDelete();
  // Sken vypne AP, treba ho znovu spustit
  WiFi.mode(WIFI_AP);
  esp_wifi_set_country(&country);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, 2);
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);
  esp_wifi_set_ps(WIFI_PS_NONE);
  int8_t tx2 = 0; esp_wifi_get_max_tx_power(&tx2);
  Serial.printf("AP znovu spustene po skene, tx=%d (%.1f dBm)\n", tx2, tx2 * 0.25);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    Serial.printf("AP klienti: %d | CPU %lu MHz | teplota %d C\n",
                  WiFi.softAPgetStationNum(),
                  getCpuFrequencyMhz(),
                  (int)temperatureRead());
  }

  // Serial prikazy: 'f' = autofokus
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'f') runAutofocus();
  }
  delay(100);
}
