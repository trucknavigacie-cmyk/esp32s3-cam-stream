#ifndef APP_HTTPD_H
#define APP_HTTPD_H

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_camera_af.h"
#include <stdlib.h>

// ---- Variabilna fps regulacia (5..30 fps, default 15) ----
// Stream drzi cielove fps: ked kamera da viac, frames sa neposielaju;
// ked da menej, stream ide max rychlostou kamery.
#define TARGET_FPS_DEFAULT  15
#define TARGET_FPS_MIN      5
#define TARGET_FPS_MAX      30

static volatile int g_target_fps = TARGET_FPS_DEFAULT;

// Staticky HTML - v RAM (~2 KB), server ho servuje pre / 
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 CAM</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;text-align:center}
 h2{margin:8px 0 12px;font-weight:500}
 img{max-width:100%;border-radius:8px;border:1px solid #333;background:#000}
 .bar{margin-top:12px;display:flex;gap:8px;justify-content:center;flex-wrap:wrap}
 button{background:#2d7ef7;color:#fff;border:0;padding:10px 16px;border-radius:8px;font-size:14px;cursor:pointer}
 button.alt{background:#444}
 #st{margin-top:10px;font-size:13px;color:#9aa}
</style></head><body>
<h2>ESP32-S3 N16R8 &mdash; OV5640</h2>
<img id="v" src="/stream" alt="stream">
<div class="bar">
  <button onclick="document.getElementById('v').src='/capture?t='+Date.now()">Snimka</button>
  <button class="alt" onclick="document.getElementById('v').src='/stream?t='+Date.now()">Stream</button>
  <button class="alt" onclick="fetch('/focus').then(r=>r.text()).then(t=>st.textContent='AF: '+t)">Zaostriť</button>
</div>
<div class="bar">
  <span style="align-self:center;font-size:13px;color:#9aa">FPS:</span>
  <button class="alt" onclick="setFps(15)">15</button>
  <button class="alt" onclick="setFps(20)">20</button>
  <button class="alt" onclick="setFps(25)">25</button>
  <button class="alt" onclick="setFps(30)">30</button>
  <button class="alt" onclick="setFps('auto')">max</button>
</div>
<div id="st">stream...</div>
<script>
const v=document.getElementById('v'),st=document.getElementById('st');
v.onerror=()=>{st.textContent='chyba streamu'};
v.onload=()=>{st.textContent='OK'};
function setFps(f){fetch('/fps?f='+f).then(r=>r.text()).then(t=>st.textContent=t);}
</script>
</body></html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

// Jedna JPEG snimka
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

// Vynuti autofokus
static esp_err_t focus_handler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s || !esp_camera_af_is_supported(s)) {
    httpd_resp_set_status(req, "501 Not Implemented");
    return httpd_resp_sendstr(req, "AF not supported");
  }
  esp_camera_af_trigger(s);
  esp_camera_af_status_t st = {};
  esp_camera_af_wait(s, 3000, &st);
  char buf[64];
  snprintf(buf, sizeof(buf), "focused=%d raw=%u", st.focused, st.raw);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, buf);
}

// Nastavenie cieloveho fps: /fps?f=20  (alebo ?f=auto = max kamery)
static esp_err_t fps_handler(httpd_req_t *req) {
  char buf[32];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char val[8] = {0};
    if (httpd_query_key_value(buf, "f", val, sizeof(val)) == ESP_OK) {
      if (strcmp(val, "auto") == 0) {
        g_target_fps = 0;
      } else {
        int f = atoi(val);
        if (f < TARGET_FPS_MIN) f = TARGET_FPS_MIN;
        if (f > TARGET_FPS_MAX) f = TARGET_FPS_MAX;
        g_target_fps = f;
      }
    }
  }
  char out[48];
  if (g_target_fps == 0) snprintf(out, sizeof(out), "fps=auto (max)");
  else                   snprintf(out, sizeof(out), "fps=%d", g_target_fps);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, out);
}

// MJPEG multipart stream
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part_buf[80];
  uint32_t frames = 0;
  uint32_t t_start = millis();
  uint32_t next_frame_us = esp_timer_get_time();

  while (true) {
    // --- fps pacing: pockaj do dalsieho slotu ---
    int target = g_target_fps;
    if (target > 0) {
      int64_t period_us = 1000000LL / target;
      int64_t now = esp_timer_get_time();
      int64_t wait = next_frame_us - now;
      if (wait > 0) {
        vTaskDelay(pdMS_TO_TICKS((wait + 999) / 1000));
      } else if (wait < -period_us * 2) {
        next_frame_us = now;   // stream zaostava -> neresetuj scheduling
      }
      next_frame_us += period_us;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("stream: fb_get FAILED");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    frames++;

    // fps raz za 5 s
    uint32_t el = millis() - t_start;
    if (el >= 5000) {
      Serial.printf("stream: %.1f fps\n", frames * 1000.0f / el);
      frames = 0;
      t_start = millis();
    }

    if (res != ESP_OK) break;   // klient zavrel spojenie
  }
  return res;
}

static httpd_uri_t uri_index = { "/", HTTP_GET, index_handler, NULL };
static httpd_uri_t uri_capture = { "/capture", HTTP_GET, capture_handler, NULL };
static httpd_uri_t uri_stream = { "/stream", HTTP_GET, stream_handler, NULL };
static httpd_uri_t uri_focus = { "/focus", HTTP_GET, focus_handler, NULL };
static httpd_uri_t uri_fps = { "/fps", HTTP_GET, fps_handler, NULL };

inline httpd_handle_t startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;

  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("HTTP server start FAILED");
    return NULL;
  }
  httpd_register_uri_handler(server, &uri_index);
  httpd_register_uri_handler(server, &uri_capture);
  httpd_register_uri_handler(server, &uri_stream);
  httpd_register_uri_handler(server, &uri_focus);
  httpd_register_uri_handler(server, &uri_fps);
  return server;
}

#endif
