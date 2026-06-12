#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, WHISPER_URL, OLLAMA_URL

static constexpr uint32_t SAMPLE_RATE   = 16000;
static constexpr uint32_t MAX_SECONDS   = 8;
static constexpr size_t   MAX_SAMPLES   = SAMPLE_RATE * MAX_SECONDS;
static constexpr size_t   CHUNK_SAMPLES = 2048;
static constexpr size_t   WAV_HEADER    = 44;
static const char*        OLLAMA_MODEL  = "gemma3:12b";   // 使うモデル

static uint8_t* g_wav     = nullptr;
static int16_t* g_pcm     = nullptr;
static size_t   g_samples = 0;

static const String BOUNDARY = "----AtomS3RFormBoundary7MA4YWxkTrZu0gW";

// ---- WAVヘッダ ----
void write_wav_header(uint8_t* buf, uint32_t data_bytes, uint32_t rate) {
  uint32_t byte_rate  = rate * 2;
  uint32_t chunk_size = 36 + data_bytes;
  auto wr32 = [&](size_t o, uint32_t v){ buf[o]=v; buf[o+1]=v>>8; buf[o+2]=v>>16; buf[o+3]=v>>24; };
  auto wr16 = [&](size_t o, uint16_t v){ buf[o]=v; buf[o+1]=v>>8; };
  memcpy(buf+0,"RIFF",4);  wr32(4,chunk_size);
  memcpy(buf+8,"WAVE",4);
  memcpy(buf+12,"fmt ",4); wr32(16,16);
  wr16(20,1); wr16(22,1); wr32(24,rate); wr32(28,byte_rate); wr16(32,2); wr16(34,16);
  memcpy(buf+36,"data",4); wr32(40,data_bytes);
}

bool wifi_connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) { delay(500); Serial.print("."); retry++; }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void to_mic()     { M5.Speaker.end(); M5.Mic.begin(); }
void to_speaker() { M5.Mic.end();     M5.Speaker.begin(); }

void record_while_pressed() {
  to_mic();
  g_samples = 0;
  Serial.println("Recording...");
  while (M5.BtnA.isPressed() && (g_samples + CHUNK_SAMPLES) <= MAX_SAMPLES) {
    if (M5.Mic.record(&g_pcm[g_samples], CHUNK_SAMPLES, SAMPLE_RATE)) {
      while (M5.Mic.isRecording()) { delay(1); }
      g_samples += CHUNK_SAMPLES;
    }
    M5.update();
  }
  to_speaker();
  write_wav_header(g_wav, g_samples * 2, SAMPLE_RATE);
  Serial.printf("Recorded %u samples\n", (unsigned)g_samples);
}

// ---- 音声を whisper にPOST → 文字起こし ----
bool send_to_whisper(String& out_text) {
  size_t wav_len = WAV_HEADER + g_samples * 2;
  String head = "--" + BOUNDARY + "\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String tail = "\r\n--" + BOUNDARY + "--\r\n";
  size_t body_len = head.length() + wav_len + tail.length();
  uint8_t* body = (uint8_t*)heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM);
  if (!body) return false;
  size_t off = 0;
  memcpy(body+off, head.c_str(), head.length()); off += head.length();
  memcpy(body+off, g_wav, wav_len);              off += wav_len;
  memcpy(body+off, tail.c_str(), tail.length()); off += tail.length();

  HTTPClient http;
  http.begin(WHISPER_URL);
  http.setTimeout(30000);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + BOUNDARY);
  int code = http.POST(body, body_len);
  Serial.printf("STT HTTP status: %d\n", code);

  bool ok = false;
  if (code == 200) {
    String resp = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, resp)) { out_text = doc["text"].as<String>(); ok = true; }
  }
  http.end();
  heap_caps_free(body);
  return ok;
}

// ---- 文字を Gemma(Ollama) にPOST → 返事をもらう ----
bool send_to_llm(const String& user_text, String& out_reply) {
  // ArduinoJsonでリクエストJSONを「組み立てる」（章⑥はパース、今回は逆）
  JsonDocument req;
  req["model"]  = OLLAMA_MODEL;
  req["stream"] = false;
  JsonArray msgs = req["messages"].to<JsonArray>();      // messages配列を作る
  JsonObject sys = msgs.add<JsonObject>();               // 1つ目: 性格設定
  sys["role"]    = "system";
  sys["content"] = "あなたは親しみやすい相棒です。日本語で、短く1〜2文で答えてください。";
  JsonObject usr = msgs.add<JsonObject>();               // 2つ目: ユーザー発言
  usr["role"]    = "user";
  usr["content"] = user_text;

  String body;
  serializeJson(req, body);                              // JSON文字列にする
  Serial.printf("LLM req: %s\n", body.c_str());

  HTTPClient http;
  http.begin(OLLAMA_URL);
  http.setTimeout(60000);                                // 生成は遅い。初回はモデルロードもあるので長め
  http.addHeader("Content-Type", "application/json");    // 今回は素のJSON
  int code = http.POST(body);
  Serial.printf("LLM HTTP status: %d\n", code);

  bool ok = false;
  if (code == 200) {
    String resp = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (!err) { out_reply = doc["message"]["content"].as<String>(); ok = true; }
    else { Serial.printf("LLM JSON error: %s\n", err.c_str()); }
  }
  http.end();
  return ok;
}

void show_lines(const String& title, const String& body_text) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, 0);
  M5.Display.println(title);
  M5.Display.println(body_text);
}

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);
  Serial.begin(115200);
  delay(3000);

  size_t cap = WAV_HEADER + MAX_SAMPLES * 2;
  g_wav = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  g_pcm = (int16_t*)(g_wav + WAV_HEADER);
  Serial.printf("PSRAM size: %u\n", (unsigned)ESP.getPsramSize());

  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("WiFi...");

  if (!g_wav) { M5.Display.fillScreen(TFT_RED); M5.Display.println("PSRAM NG"); return; }

  if (wifi_connect()) {
    Serial.printf("WiFi OK %s\n", WiFi.localIP().toString().c_str());
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("押して話してね");
    M5.Speaker.tone(2000, 100);
  } else {
    M5.Display.fillScreen(TFT_RED);
    M5.Speaker.tone(400, 300);
  }
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setFont(&fonts::efontJA_16);
    M5.Display.setCursor(0, 0);
    M5.Display.println("録音中...");

    record_while_pressed();
    if (g_samples == 0) return;

    // ① 音声 → 文字
    show_lines("変換中...", "");
    String heard;
    if (!send_to_whisper(heard)) { show_lines("STT失敗", ""); M5.Speaker.tone(400,200); return; }
    Serial.printf("Heard: %s\n", heard.c_str());

    // ② 文字 → Gemma → 返事
    show_lines("あなた:", heard);
    M5.Speaker.tone(1500, 50);
    String reply;
    if (send_to_llm(heard, reply)) {
      Serial.printf("Reply: %s\n", reply.c_str());
      show_lines("相棒:", reply);
      M5.Speaker.tone(2000, 100);
    } else {
      show_lines("LLM失敗", "");
      M5.Speaker.tone(400, 200);
    }
  }
}