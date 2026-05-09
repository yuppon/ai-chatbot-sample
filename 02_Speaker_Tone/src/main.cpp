#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);     // シリアル通信を 115200bps で開始
  delay(3000);              // PCがポートを開く時間として3秒待つ
  Serial.println("Hello from AtomS3R!");

  M5.Display.fillScreen(TFT_RED);
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    Serial.println("Button pressed!"); 
    M5.Display.fillScreen(TFT_BLUE);
  }
}