#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, VOICEVOX_BASE

static const int SPEAKER_ID = 8;   // 3 = ずんだもん(ノーマル)。他のIDは Appendix 参照

// ============================================================
// URLエンコード（日本語をURLに乗せるための変換）
// 例: "こんにちは" → "%E3%81%93%E3%82%93..."
// ============================================================
String url_encode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    // 英数字と一部記号はそのまま。それ以外は %XX に変換
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

// ============================================================
// Wi-Fi 接続
// ============================================================
bool wifi_connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) { delay(500); Serial.print("."); retry++; }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// ============================================================
// VOICEVOXでテキストを音声合成 → WAVを返す
//   ① /audio_query?text=...&speaker=N  → クエリJSONを取得
//   ② /synthesis?speaker=N             → クエリを投げてWAVを取得
// ============================================================
bool synthesize(const String& text, uint8_t*& out_wav, size_t& out_len) {
  // ① /audio_query
  String url1 = String(VOICEVOX_BASE) + "/audio_query?text=" + url_encode(text)
              + "&speaker=" + String(SPEAKER_ID);
  Serial.printf("Query URL: %s\n", url1.c_str());

  HTTPClient http1;
  http1.begin(url1);
  http1.setTimeout(15000);
  int code1 = http1.POST("");                      // 本文は空でOK（パラメータはURLに）
  Serial.printf("audio_query status: %d\n", code1);
  if (code1 != 200) { http1.end(); return false; }
  String query_json = http1.getString();           // ← これをそのまま②に渡す
  http1.end();
  Serial.printf("Query JSON size: %u\n", (unsigned)query_json.length());

  // ② /synthesis
  String url2 = String(VOICEVOX_BASE) + "/synthesis?speaker=" + String(SPEAKER_ID);
  HTTPClient http2;
  http2.begin(url2);
  http2.setTimeout(30000);                         // 合成は数秒かかる
  http2.addHeader("Content-Type", "application/json");
  int code2 = http2.POST(query_json);              // ①のJSONをそのまま本文に
  Serial.printf("synthesis status: %d\n", code2);
  if (code2 != 200) { http2.end(); return false; }

  // 返ってくるのはバイナリ(WAV)。サイズを取得してPSRAMに確保
  int wav_len = http2.getSize();
  Serial.printf("WAV size: %d bytes\n", wav_len);
  if (wav_len <= 0) { http2.end(); return false; }

  uint8_t* buf = (uint8_t*)heap_caps_malloc(wav_len, MALLOC_CAP_SPIRAM);
  if (!buf) { http2.end(); return false; }

  // ストリームから読み込む
  WiFiClient* stream = http2.getStreamPtr();
  size_t read_total = 0;
  while (http2.connected() && read_total < (size_t)wav_len) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf + read_total, avail);
      read_total += r;
    } else {
      delay(1);
    }
  }
  http2.end();

  out_wav = buf;
  out_len = read_total;
  return true;
}

// ============================================================
// WAVヘッダを読んで、PCM部分をスピーカーで再生
//   VOICEVOX は 24kHz / 16bit / mono の WAV を返す
// ============================================================
void play_wav(const uint8_t* wav, size_t len) {
  if (len < 44) return;

  // ヘッダから sampleRate を読む（オフセット24-27, リトルエンディアン）
  uint32_t sample_rate = (uint32_t)wav[24]
                       | ((uint32_t)wav[25] << 8)
                       | ((uint32_t)wav[26] << 16)
                       | ((uint32_t)wav[27] << 24);
  // データ部の長さ（オフセット40-43）
  uint32_t data_bytes  = (uint32_t)wav[40]
                       | ((uint32_t)wav[41] << 8)
                       | ((uint32_t)wav[42] << 16)
                       | ((uint32_t)wav[43] << 24);

  // PCM本体はヘッダの直後から。16bit を int16 として渡す
  const int16_t* pcm  = (const int16_t*)(wav + 44);
  size_t samples = data_bytes / 2;   // 16bit なので 2バイト = 1サンプル

  Serial.printf("Play: %u Hz, %u samples\n", (unsigned)sample_rate, (unsigned)samples);
  M5.Speaker.playRaw(pcm, samples, sample_rate);
  while (M5.Speaker.isPlaying()) { delay(10); }
}

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);
  Serial.begin(115200);
  delay(3000);

  Serial.printf("PSRAM size: %u\n", (unsigned)ESP.getPsramSize());

  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("WiFi...");

  if (wifi_connect()) {
    Serial.printf("WiFi OK %s\n", WiFi.localIP().toString().c_str());
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("押すと喋るよ");
    M5.Speaker.tone(2000, 100);
  } else {
    M5.Display.fillScreen(TFT_RED);
    M5.Speaker.tone(400, 300);
  }
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("合成中...");

    uint8_t* wav = nullptr;
    size_t   len = 0;
    if (synthesize("こんにちは、ずんだもんなのだ。", wav, len)) {
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(0, 0);
      M5.Display.println("再生中...");
      play_wav(wav, len);
      heap_caps_free(wav);
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(0, 0);
      M5.Display.println("押すと喋るよ");
    } else {
      M5.Display.fillScreen(TFT_RED);
      M5.Speaker.tone(400, 200);
    }
  }
}