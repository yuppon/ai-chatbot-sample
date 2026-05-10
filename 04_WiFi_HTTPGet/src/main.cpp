#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

// =============================================================
// Wi-Fi 接続
// =============================================================
bool wifi_connect() {
  Serial.printf("Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("WiFi failed");
  return false;
}

// =============================================================
// httpbin.org/get からグローバルIPを取得
// =============================================================
bool fetch_global_ip(String& out_ip) {
  HTTPClient http;
  http.begin("http://httpbin.org/get");

  int code = http.GET();
  Serial.printf("HTTP status: %d\n", code);

  if (code != 200) {
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  Serial.printf("Body: %s\n", body.c_str());

  // JSON パース
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }

  // origin フィールドを取り出す
  out_ip = doc["origin"].as<String>();
  return true;
}

// =============================================================
// setup / loop
// =============================================================
void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);

  Serial.begin(115200);
  delay(5000);

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, 0);
  M5.Display.println("WiFi...");

  if (wifi_connect()) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Local IP:\n%s\n", WiFi.localIP().toString().c_str());
    M5.Display.println("\nPush BtnA");
    M5.Speaker.tone(2000, 100);
  } else {
    M5.Display.fillScreen(TFT_RED);
    M5.Speaker.tone(400, 300);
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    Serial.println("Fetching global IP...");
    M5.Speaker.tone(1500, 50);

    String global_ip;
    if (fetch_global_ip(global_ip)) {
      Serial.printf("Global IP: %s\n", global_ip.c_str());
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(0, 0);
      M5.Display.println("Global IP:");
      M5.Display.println(global_ip);
      M5.Speaker.tone(2000, 100);
    } else {
      M5.Speaker.tone(400, 200);
    }
  }
}