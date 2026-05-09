#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;   // ★ 正解はこれ
  M5.begin(cfg);

  Serial.begin(115200);
  delay(5000);
  Serial.printf("Speaker enabled: %d\n", M5.Speaker.isEnabled());

  M5.Speaker.setVolume(32);
  M5.Speaker.tone(1000, 200);
  Serial.println("Beep!");

  M5.Display.fillScreen(TFT_RED);
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    Serial.println("Button pressed!");
    M5.Display.fillScreen(TFT_BLUE);
    M5.Speaker.tone(2000, 100);
  }
}