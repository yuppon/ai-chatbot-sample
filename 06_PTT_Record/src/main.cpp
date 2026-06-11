#include <M5Unified.h>

// ---- 録音パラメータ ----
static constexpr uint32_t SAMPLE_RATE   = 16000;        // whisper が好む 16kHz
static constexpr uint32_t MAX_SECONDS   = 5;            // 最大録音秒数
static constexpr size_t   MAX_SAMPLES   = SAMPLE_RATE * MAX_SECONDS;
static constexpr size_t   CHUNK_SAMPLES = 2048;         // 一度に録る量
static constexpr size_t   WAV_HEADER    = 44;           // WAVヘッダは44バイト固定

// WAVバッファ：先頭44バイトをヘッダ用に空けて、その後ろにPCMを入れる
static uint8_t* g_wav     = nullptr;                    // [ヘッダ44 + PCM]
static int16_t* g_pcm     = nullptr;                    // g_wav + 44 を int16 として見る
static size_t   g_samples = 0;                          // 録れたサンプル数

// =============================================================
// WAVヘッダ（44バイト, PCM 16bit mono）を書き込む
// =============================================================
void write_wav_header(uint8_t* buf, uint32_t data_bytes, uint32_t rate) {
  uint32_t byte_rate   = rate * 1 * 2;   // sampleRate * channels * bytesPerSample
  uint32_t chunk_size  = 36 + data_bytes;
  auto wr32 = [&](size_t o, uint32_t v){ buf[o]=v; buf[o+1]=v>>8; buf[o+2]=v>>16; buf[o+3]=v>>24; };
  auto wr16 = [&](size_t o, uint16_t v){ buf[o]=v; buf[o+1]=v>>8; };
  memcpy(buf+0,  "RIFF", 4);  wr32(4,  chunk_size);
  memcpy(buf+8,  "WAVE", 4);
  memcpy(buf+12, "fmt ", 4);  wr32(16, 16);   // fmtチャンクサイズ
  wr16(20, 1);                                // PCM
  wr16(22, 1);                                // mono
  wr32(24, rate);
  wr32(28, byte_rate);
  wr16(32, 2);                                // blockAlign
  wr16(34, 16);                               // bitsPerSample
  memcpy(buf+36, "data", 4);  wr32(40, data_bytes);
}

// =============================================================
// マイク/スピーカーの切り替え（同時使用不可のため）
// =============================================================
void to_mic() { M5.Speaker.end(); M5.Mic.begin(); }
void to_speaker() { M5.Mic.end(); M5.Speaker.begin(); }

// =============================================================
// ボタンが押されている間だけ録音
// =============================================================
void record_while_pressed() {
  to_mic();
  g_samples = 0;
  Serial.println("Recording...");

  while (M5.BtnA.isPressed() && (g_samples + CHUNK_SAMPLES) <= MAX_SAMPLES) {
    // g_pcm[g_samples] から CHUNK_SAMPLES 分を録音
    if (M5.Mic.record(&g_pcm[g_samples], CHUNK_SAMPLES, SAMPLE_RATE)) {
      while (M5.Mic.isRecording()) { delay(1); }   // この塊が録り終わるまで待つ
      g_samples += CHUNK_SAMPLES;
    }
    M5.update();   // ボタン状態を更新
  }

  to_speaker();

  uint32_t data_bytes = g_samples * 2;
  write_wav_header(g_wav, data_bytes, SAMPLE_RATE);

  Serial.printf("Recorded %u samples (%u PCM bytes)\n", (unsigned)g_samples, (unsigned)data_bytes);
  Serial.printf("WAV total size: %u bytes\n", (unsigned)(WAV_HEADER + data_bytes));
}

// =============================================================
// 録ったものをスピーカーで再生（確認用）
// =============================================================
void playback() {
  Serial.println("Playback...");
  M5.Speaker.playRaw(g_pcm, g_samples, SAMPLE_RATE);
  while (M5.Speaker.isPlaying()) { delay(10); }
}

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;   // 章④と同じ
  M5.begin(cfg);

  Serial.begin(115200);
  delay(3000);

  // バッファ確保（PSRAMがあればそちら優先、なければ内部RAM）
  size_t cap = WAV_HEADER + MAX_SAMPLES * 2;
  g_wav = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  if (!g_wav) g_wav = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
  g_pcm = (int16_t*)(g_wav + WAV_HEADER);

  Serial.printf("Buffer: %u bytes, free heap: %u\n", (unsigned)cap, (unsigned)ESP.getFreeHeap());

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, 0);
  M5.Display.println("Hold BtnA\nto record");

  M5.Speaker.tone(2000, 100);
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setCursor(0, 0);
    M5.Display.println("REC...");

    record_while_pressed();   // 離すまで録音

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Done\n%u smp", (unsigned)g_samples);

    playback();               // 録れたか耳で確認
  }
}