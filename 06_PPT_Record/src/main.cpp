#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, WHISPER_URL

// ============================================================
// 録音パラメータ
// ============================================================
static constexpr uint32_t SAMPLE_RATE   = 16000;   // 1秒あたりのサンプル数。whisperは16kHzが基本
static constexpr uint32_t MAX_SECONDS   = 8;        // 最大録音秒数（これ以上は自動で打ち切り）
static constexpr size_t   MAX_SAMPLES   = SAMPLE_RATE * MAX_SECONDS;  // 確保するサンプル数
static constexpr size_t   CHUNK_SAMPLES = 2048;     // 一度のrecord()で録る量（小さすぎると隙間が増える）
static constexpr size_t   WAV_HEADER    = 44;       // WAVヘッダは必ず44バイト（後述）

// 録音バッファ。先頭44バイトをWAVヘッダ用に空けておき、その後ろにPCM（生の音データ）を入れる。
// こうしておくと「ヘッダ + PCM」が1本のメモリに連続して並ぶ＝そのまま送れる形になる。
static uint8_t* g_wav     = nullptr;   // [ WAVヘッダ44バイト | PCMデータ... ]
static int16_t* g_pcm     = nullptr;   // g_wav + 44 を「16bit整数の配列」として見たもの
static size_t   g_samples = 0;         // 実際に録れたサンプル数

// multipart/form-data の「区切り線」。本文と被らない、適当に長くてユニークな文字列にする。
static const String BOUNDARY = "----AtomS3RFormBoundary7MA4YWxkTrZu0gW";

// ============================================================
// WAVヘッダ（44バイト, PCM 16bit mono）を buf の先頭に書き込む
//   data_bytes : PCMデータのバイト数（サンプル数 × 2）
//   rate       : サンプリングレート（16000）
// ※ 数値はすべてリトルエンディアン（下位バイトから先に置く）で書く決まり
// ============================================================
void write_wav_header(uint8_t* buf, uint32_t data_bytes, uint32_t rate) {
  uint32_t byte_rate  = rate * 2;          // 1秒あたりのバイト数 = rate × ch(1) × bytes/sample(2)
  uint32_t chunk_size = 36 + data_bytes;   // ファイル全体サイズ - 8

  // 4バイト整数を書くヘルパー（下位バイトから順に＝リトルエンディアン）
  auto wr32 = [&](size_t o, uint32_t v){ buf[o]=v; buf[o+1]=v>>8; buf[o+2]=v>>16; buf[o+3]=v>>24; };
  // 2バイト整数を書くヘルパー
  auto wr16 = [&](size_t o, uint16_t v){ buf[o]=v; buf[o+1]=v>>8; };

  memcpy(buf+0,  "RIFF", 4);   // [0-3]   おまじない："RIFF"
  wr32(4,  chunk_size);        // [4-7]   このあとに続くデータの総バイト数(=全体-8)
  memcpy(buf+8,  "WAVE", 4);   // [8-11]  "WAVE"（中身は音だよ、の宣言）
  memcpy(buf+12, "fmt ", 4);   // [12-15] "fmt "（フォーマット情報チャンクの開始。最後の空白も含めて4文字）
  wr32(16, 16);                // [16-19] fmtチャンクの長さ。PCMなら16固定
  wr16(20, 1);                 // [20-21] フォーマット種別。1 = PCM（無圧縮）
  wr16(22, 1);                 // [22-23] チャンネル数。1 = モノラル
  wr32(24, rate);              // [24-27] サンプリングレート（16000）
  wr32(28, byte_rate);         // [28-31] バイトレート（1秒あたりのバイト数）
  wr16(32, 2);                 // [32-33] ブロックサイズ = ch × bytes/sample = 1×2
  wr16(34, 16);                // [34-35] 1サンプルのビット数。16bit
  memcpy(buf+36, "data", 4);   // [36-39] "data"（ここから音の本体だよ、の宣言）
  wr32(40, data_bytes);        // [40-43] PCMデータのバイト数
  // [44-]                        ここから先が実際の音（PCM）。それは g_pcm に録音で入れる
}

// ============================================================
// Wi-Fi 接続（章④と同じ）
// ============================================================
bool wifi_connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) { delay(500); Serial.print("."); retry++; }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// ============================================================
// マイクとスピーカーは同時に使えない（I2Sを共有しているため）
// 使う直前に、もう一方を止めてこちらを起こす
// ============================================================
void to_mic()     { M5.Speaker.end(); M5.Mic.begin(); }
void to_speaker() { M5.Mic.end();     M5.Speaker.begin(); }

// ============================================================
// ボタンを押している間だけ録音する
// ============================================================
void record_while_pressed() {
  to_mic();              // マイクに切り替え
  g_samples = 0;
  Serial.println("Recording...");

  // ボタンが押されていて、かつバッファに空きがある限りループ
  while (M5.BtnA.isPressed() && (g_samples + CHUNK_SAMPLES) <= MAX_SAMPLES) {
    // g_pcm[g_samples] の位置から CHUNK_SAMPLES 分を録音（録音先をずらしながら追記していく）
    if (M5.Mic.record(&g_pcm[g_samples], CHUNK_SAMPLES, SAMPLE_RATE)) {
      while (M5.Mic.isRecording()) { delay(1); }  // この塊を録り終わるまで待つ
      g_samples += CHUNK_SAMPLES;                 // 録れた分だけ書き込み位置を進める
    }
    M5.update();         // ボタンの押下状態を更新（これを呼ばないと離したのを検知できない）
  }

  to_speaker();          // スピーカーに戻す（このあとビープを鳴らすため）

  // 録れたサンプル数からWAVヘッダを完成させる（先頭44バイトに書き込む）
  write_wav_header(g_wav, g_samples * 2, SAMPLE_RATE);
  Serial.printf("Recorded %u samples, WAV %u bytes\n",
                (unsigned)g_samples, (unsigned)(WAV_HEADER + g_samples * 2));
}

// ============================================================
// WAVを multipart/form-data に包んで whisper にPOST → text を取り出す
// ============================================================
bool send_to_whisper(String& out_text) {
  size_t wav_len = WAV_HEADER + g_samples * 2;   // 送るWAVの総バイト数

  // multipart の「ファイル部分」の前置き（ヘッダ）。
  // 「これは file という名前のフォーム項目で、audio.wav というファイルです」と宣言している。
  String head = "--" + BOUNDARY + "\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                "Content-Type: audio/wav\r\n"
                "\r\n";                            // 空行のあとに本体（WAV）が続く

  // 本体のあとに置く終端。「--BOUNDARY--」でフォーム全体の終わりを示す。
  String tail = "\r\n--" + BOUNDARY + "--\r\n";

  // [head(テキスト)] + [WAVの生バイト] + [tail(テキスト)] を1本に連結したものが送信本文。
  // curl が -F で自動でやってくれていた作業を、ここでは手で組み立てている。
  size_t body_len = head.length() + wav_len + tail.length();
  uint8_t* body = (uint8_t*)heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM);  // PSRAMに確保
  if (!body) { Serial.println("body malloc failed"); return false; }

  size_t off = 0;
  memcpy(body + off, head.c_str(), head.length()); off += head.length();
  memcpy(body + off, g_wav,        wav_len);       off += wav_len;
  memcpy(body + off, tail.c_str(), tail.length()); off += tail.length();

  HTTPClient http;
  http.begin(WHISPER_URL);
  http.setTimeout(30000);   // mediumモデルは処理に数秒かかるので長めに（既定だと足りないことがある）
  // Content-Type に boundary を必ず入れる。これがないとサーバーが本文を区切れない。
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + BOUNDARY);

  int code = http.POST(body, body_len);   // 本文を丸ごとPOST
  Serial.printf("HTTP status: %d\n", code);

  bool ok = false;
  if (code == 200) {
    String resp = http.getString();        // 例: {"text":"こんにちは"}
    Serial.printf("Resp: %s\n", resp.c_str());

    JsonDocument doc;                        // ArduinoJson v7（サイズ指定不要）
    DeserializationError err = deserializeJson(doc, resp);
    if (!err) {
      out_text = doc["text"].as<String>();   // "text" フィールドを取り出す
      ok = true;
    } else {
      Serial.printf("JSON error: %s\n", err.c_str());
    }
  }
  http.end();
  heap_caps_free(body);   // 使い終わった送信バッファを解放
  return ok;
}

// ============================================================
// 文字起こし結果を画面に表示（日本語フォントを使う）
// ============================================================
void show_text(const String& t) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);   // ★日本語フォント。これがないと文字化け（豆腐）になる
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, 0);
  M5.Display.println(t);
}

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;  // Atomic Echo Base を使う宣言（章④と同じ）
  M5.begin(cfg);
  Serial.begin(115200);
  delay(3000);

  // 録音バッファをPSRAMに確保（ヘッダ44 + 最大サンプル分）
  size_t cap = WAV_HEADER + MAX_SAMPLES * 2;
  g_wav = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  g_pcm = (int16_t*)(g_wav + WAV_HEADER);   // ヘッダの直後をPCM領域として使う
  Serial.printf("PSRAM size: %u, wav buf: %u\n", (unsigned)ESP.getPsramSize(), (unsigned)cap);

  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("WiFi...");

  if (!g_wav) {   // PSRAM確保に失敗（＝PSRAM未有効）
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setCursor(0, 0);
    M5.Display.println("PSRAM NG");
    return;
  }

  if (wifi_connect()) {
    Serial.printf("WiFi OK %s\n", WiFi.localIP().toString().c_str());
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("押して話してね");
    M5.Speaker.tone(2000, 100);   // 接続成功音
  } else {
    M5.Display.fillScreen(TFT_RED);
    M5.Speaker.tone(400, 300);    // 失敗音
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {     // ボタンが「押された瞬間」を検知
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setFont(&fonts::efontJA_16);
    M5.Display.setCursor(0, 0);
    M5.Display.println("録音中...");

    record_while_pressed();       // 離すまで録音（この中でボタンを離すのを待つ）
    if (g_samples == 0) return;   // 何も録れていなければ何もしない

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("変換中...");
    M5.Speaker.tone(1500, 50);

    String text;
    if (send_to_whisper(text)) {
      Serial.printf("Text: %s\n", text.c_str());
      show_text(text);            // 画面に結果を表示
      M5.Speaker.tone(2000, 100);
    } else {
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(0, 0);
      M5.Display.println("失敗");
      M5.Speaker.tone(400, 200);
    }
  }
}