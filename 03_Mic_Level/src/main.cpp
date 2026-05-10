#include <M5Unified.h>
#include <math.h>

static constexpr size_t BUF_LEN     = 512;       // 1回に取るサンプル数
static constexpr uint32_t SAMPLE_HZ = 16000;     // サンプリング周波数
static int16_t buf[BUF_LEN];

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);

  Serial.begin(115200);
  delay(5000);

  // ★ Atomic Echo Base はスピーカーとマイクが I2S を共有するので、
  //   マイクを使う前にスピーカーを止める
  M5.Speaker.end();
  M5.Mic.begin();

  Serial.printf("Mic enabled: %d\n", M5.Mic.isEnabled());

  M5.Display.fillScreen(TFT_BLACK);
}

void loop() {
  M5.update();

  // 録音できたら音量を計算
  if (M5.Mic.record(buf, BUF_LEN, SAMPLE_HZ)) {

    // RMS（二乗平均平方根）= 音の大きさ
    uint64_t sum = 0;
    for (size_t i = 0; i < BUF_LEN; i++) {
      sum += (int32_t)buf[i] * buf[i];
    }
    float rms = sqrtf((float)sum / BUF_LEN);

    // 対数スケールで見やすく
    int bar = (int)(log10f(rms + 1) * 35);
    if (bar < 0)   bar = 0;
    if (bar > 128) bar = 128;

    // 画面に横棒で表示（AtomS3R は 128x128）
    M5.Display.fillRect(0, 60, 128, 8, TFT_BLACK);   // 一旦消す
    M5.Display.fillRect(0, 60, bar, 8, TFT_GREEN);   // 緑のバー

    Serial.printf("RMS: %7.1f  bar: %3d\n", rms, bar);
  }
}