#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ---- LLMプロバイダ識別子（secrets.h の LLM_PROVIDER で選択） ----
#define LLM_OLLAMA 0
#define LLM_OPENAI 1
#define LLM_GEMINI 2
#define LLM_AZURE  3

#include "secrets.h"

static constexpr uint32_t SAMPLE_RATE   = 16000;
static constexpr uint32_t MAX_SECONDS   = 8;
static constexpr size_t   MAX_SAMPLES   = SAMPLE_RATE * MAX_SECONDS;
static constexpr size_t   CHUNK_SAMPLES = 2048;
static constexpr size_t   WAV_HEADER    = 44;
// AivisSpeech の style ID（GET /speakers で確認。1431611904 = まい ノーマル）
static const int          SPEAKER_ID    = 1431611904;
// LLM 最大生成トークン（返答は1文なので冗長生成を早期終了させレイテンシ短縮）
static const int          LLM_NUM_PREDICT = 128;

// 全プロバイダ共通のシステムプロンプト
static const char* LLM_SYSTEM_PROMPT =
    "あなたは親しみやすい相棒です。日本語で、必ず1文だけで答えてください。絵文字は使わないでください。";

static uint8_t* g_wav     = nullptr;
static int16_t* g_pcm     = nullptr;
static size_t   g_samples = 0;

static const String BOUNDARY = "----AtomS3RFormBoundary7MA4YWxkTrZu0gW";

// ---- WAVヘッダ（16bit/mono PCM）を buf 先頭44バイトに書き込む ----
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

// ---- URLエンコード（RFC3986 非予約文字以外を %XX に） ----
String url_encode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool wifi_connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) { delay(500); Serial.print("."); retry++; }
  Serial.println();
  // WiFiモデムスリープを無効化。省電力復帰の遅延で HTTP 転送が極端に遅くなるのを防ぐ
  // （これが無いと数十KBの送受信に数十秒かかることがある）
  WiFi.setSleep(false);
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

// ============================================================
// 音声 → whisper → 文字
// ============================================================
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

// ============================================================
// 文字 → LLM → 返事
//   secrets.h の LLM_PROVIDER で Ollama / OpenAI / Gemini / Azure を切替
// ============================================================

// OpenAI 互換の messages 配列（system + user）を req に追加する。
// Ollama も OpenAI / Azure も同じ形式なので共通化。
static void add_chat_messages(JsonDocument& req, const String& user_text) {
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"]    = "system";
  sys["content"] = LLM_SYSTEM_PROMPT;
  JsonObject usr = msgs.add<JsonObject>();
  usr["role"]    = "user";
  usr["content"] = user_text;
}

// OpenAI / Azure 共通: Chat Completions 形式のリクエストボディを生成
static String build_openai_body(const String& user_text, bool include_model) {
  JsonDocument req;
  if (include_model) req["model"] = OPENAI_MODEL;  // Azureはデプロイ名がURL側なので不要
  req["max_tokens"] = LLM_NUM_PREDICT;             // 生成長を制限してレイテンシ短縮
  add_chat_messages(req, user_text);
  String body;
  serializeJson(req, body);
  return body;
}

bool send_to_llm(const String& user_text, String& out_reply) {
  // ---- プロバイダごとに URL / ボディ / ヘッダ / クライアントを決定 ----
  String url, body;
  HTTPClient http;
  bool ok = false;

#if LLM_PROVIDER == LLM_OLLAMA
  // ローカル Ollama（平文HTTP）
  url = OLLAMA_URL;
  {
    JsonDocument req;
    req["model"]  = OLLAMA_MODEL;
    req["stream"] = false;
    req["options"]["num_predict"] = LLM_NUM_PREDICT;  // 生成長を制限してレイテンシ短縮
    add_chat_messages(req, user_text);
    serializeJson(req, body);
  }
  http.begin(url);
  http.setTimeout(60000);
  http.addHeader("Content-Type", "application/json");
  {
    int code = http.POST(body);
    Serial.printf("LLM HTTP status: %d\n", code);
    if (code == 200) {
      String resp = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, resp)) { out_reply = doc["message"]["content"].as<String>(); ok = true; }
    }
  }
  http.end();

#else
  // クラウドAPI（HTTPS）。プロトタイプ向けに証明書検証はスキップ
  WiFiClientSecure client;
  client.setInsecure();

  #if LLM_PROVIDER == LLM_OPENAI
    url  = "https://api.openai.com/v1/chat/completions";
    body = build_openai_body(user_text, true);
  #elif LLM_PROVIDER == LLM_AZURE
    url  = String(AZURE_ENDPOINT) + "/openai/deployments/" + AZURE_DEPLOYMENT
         + "/chat/completions?api-version=" + AZURE_API_VERSION;
    body = build_openai_body(user_text, false);
  #elif LLM_PROVIDER == LLM_GEMINI
    url  = String("https://generativelanguage.googleapis.com/v1beta/models/")
         + GEMINI_MODEL + ":generateContent?key=" + GEMINI_API_KEY;
    {
      JsonDocument req;
      JsonObject si = req["systemInstruction"].to<JsonObject>();
      si["parts"][0]["text"] = LLM_SYSTEM_PROMPT;
      JsonArray contents = req["contents"].to<JsonArray>();
      JsonObject turn = contents.add<JsonObject>();
      turn["role"] = "user";
      turn["parts"][0]["text"] = user_text;
      req["generationConfig"]["maxOutputTokens"] = LLM_NUM_PREDICT;  // 生成長制限でレイテンシ短縮
      serializeJson(req, body);
    }
  #endif

  http.begin(client, url);
  http.setTimeout(60000);
  http.addHeader("Content-Type", "application/json");
  #if LLM_PROVIDER == LLM_OPENAI
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  #elif LLM_PROVIDER == LLM_AZURE
    http.addHeader("api-key", AZURE_API_KEY);
  #endif

  {
    int code = http.POST(body);
    Serial.printf("LLM HTTP status: %d\n", code);
    if (code == 200) {
      String resp = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, resp)) {
  #if LLM_PROVIDER == LLM_GEMINI
        out_reply = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  #else
        out_reply = doc["choices"][0]["message"]["content"].as<String>();
  #endif
        ok = true;
      }
    }
  }
  http.end();
#endif

  return ok;
}

// ============================================================
// 文字 → AivisSpeech（VOICEVOX互換API）→ WAV
// ============================================================
bool synthesize(const String& text, uint8_t*& out_wav, size_t& out_len) {
  // ① audio_query
  String url1 = String(AIVIS_BASE) + "/audio_query?text=" + url_encode(text)
              + "&speaker=" + String(SPEAKER_ID);
  HTTPClient http1;
  http1.begin(url1);
  http1.setTimeout(30000);
  uint32_t t_start = millis();
  int code1 = http1.POST("");
  Serial.printf("audio_query: %d  (%lu ms)\n", code1, (unsigned long)(millis() - t_start));
  if (code1 != 200) { http1.end(); return false; }
  String query_json = http1.getString();
  http1.end();

  // 発話前後の無音を削除（文字列置換で DRAM を節約）
  auto set_zero = [&](const char* key) {
    int idx = query_json.indexOf(key);
    if (idx < 0) return;
    int start = idx + strlen(key);
    int end   = start;
    while (end < (int)query_json.length() && query_json[end] != ',' && query_json[end] != '}') end++;
    query_json = query_json.substring(0, start) + "0.0" + query_json.substring(end);
  };
  set_zero("\"pre_phoneme_length\":");
  set_zero("\"post_phoneme_length\":");

  // ② synthesis
  uint32_t t_aq = millis();
  String url2 = String(AIVIS_BASE) + "/synthesis?speaker=" + String(SPEAKER_ID);
  HTTPClient http2;
  http2.begin(url2);
  http2.setTimeout(60000);
  http2.addHeader("Content-Type", "application/json");
  int code2 = http2.POST(query_json);
  Serial.printf("synthesis: %d  (%lu ms)\n", code2, (unsigned long)(millis() - t_aq));
  if (code2 != 200) { http2.end(); return false; }

  int wav_len = http2.getSize();
  Serial.printf("WAV Content-Length: %d\n", wav_len);
  if (wav_len <= 0) { http2.end(); return false; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(wav_len, MALLOC_CAP_SPIRAM);
  if (!buf) { http2.end(); return false; }

  WiFiClient* stream = http2.getStreamPtr();
  stream->setTimeout(60000);
  size_t read_total = 0;
  while (read_total < (size_t)wav_len) {
    size_t chunk = min((size_t)4096, (size_t)(wav_len - read_total));
    size_t got = stream->readBytes(buf + read_total, chunk);
    if (got == 0) { Serial.println("WAV read stalled"); break; }
    read_total += got;
  }
  Serial.printf("WAV read: %u / %d bytes\n", (unsigned)read_total, wav_len);
  http2.end();

  out_wav = buf;
  out_len = read_total;
  return true;
}

// ---- WAVヘッダから sample_rate を読んでスピーカーで再生 ----
void play_wav(const uint8_t* wav, size_t len) {
  if (len < 44) return;
  uint32_t sample_rate = (uint32_t)wav[24] | ((uint32_t)wav[25]<<8)
                       | ((uint32_t)wav[26]<<16) | ((uint32_t)wav[27]<<24);
  uint32_t data_bytes  = (uint32_t)wav[40] | ((uint32_t)wav[41]<<8)
                       | ((uint32_t)wav[42]<<16) | ((uint32_t)wav[43]<<24);
  const int16_t* pcm = (const int16_t*)(wav + 44);
  size_t samples = data_bytes / 2;
  M5.Speaker.playRaw(pcm, samples, sample_rate);
  while (M5.Speaker.isPlaying()) { delay(10); }
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
    // ① 録音
    show_lines("録音中...", "");
    record_while_pressed();
    if (g_samples == 0) return;

    // ② 音声 → 文字
    show_lines("聞き取り中...", "");
    String heard;
    uint32_t t0 = millis();
    if (!send_to_whisper(heard)) { show_lines("STT失敗", ""); M5.Speaker.tone(400,200); return; }
    uint32_t t_stt = millis();
    Serial.printf("Heard: %s\n", heard.c_str());
    Serial.printf("[time] STT  : %lu ms\n", (unsigned long)(t_stt - t0));

    // ③ 文字 → 返事
    show_lines("あなた:", heard);
    String reply;
    if (!send_to_llm(heard, reply)) { show_lines("LLM失敗", ""); M5.Speaker.tone(400,200); return; }
    uint32_t t_llm = millis();
    Serial.printf("Reply: %s\n", reply.c_str());
    Serial.printf("[time] LLM  : %lu ms\n", (unsigned long)(t_llm - t_stt));
    show_lines("相棒:", reply);

    // ④ 返事 → 音声
    uint8_t* wav = nullptr;
    size_t   len = 0;
    if (synthesize(reply, wav, len)) {
      uint32_t t_tts = millis();
      Serial.printf("[time] TTS  : %lu ms\n", (unsigned long)(t_tts - t_llm));
      play_wav(wav, len);
      heap_caps_free(wav);
      Serial.printf("[time] play : %lu ms\n", (unsigned long)(millis() - t_tts));
    } else {
      M5.Speaker.tone(400, 200);
    }
    Serial.printf("[time] TOTAL: %lu ms\n", (unsigned long)(millis() - t0));
  }
}