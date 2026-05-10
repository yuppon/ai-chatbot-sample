#include <M5Unified.h>

// ① 単音メロディ（ドレミファソラシド）
void play_scale() {
  const int notes[] = {523, 587, 659, 698, 784, 880, 988, 1047};
  for (int n : notes) {
    M5.Speaker.tone(n, 200);
    delay(220);
  }
}

// ② 起動音っぽいやつ（ピポッ系）
void play_boot() {
  M5.Speaker.tone(2000, 80);  delay(100);
  M5.Speaker.tone(2500, 80);  delay(100);
  M5.Speaker.tone(3000, 120);
}

// ③ 効果音（OK / NG / ファンファーレ）
void beep_ok() {
  M5.Speaker.tone(1000, 80); delay(90);
  M5.Speaker.tone(1500, 80); delay(90);
  M5.Speaker.tone(2000, 150);
}
void beep_ng() {
  M5.Speaker.tone(800, 100); delay(110);
  M5.Speaker.tone(400, 200);
}
void play_fanfare() {
  M5.Speaker.tone(523, 100); delay(110);
  M5.Speaker.tone(659, 100); delay(110);
  M5.Speaker.tone(784, 100); delay(110);
  M5.Speaker.tone(1047, 300);
}

// ④ ボタンの押下フィードバック
void beep_press()   { M5.Speaker.tone(1500, 30); }
void beep_release() { M5.Speaker.tone(1000, 30); }


void setup() {
  auto cfg = M5.config();
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);

  Serial.begin(115200);
  delay(5000);
  M5.Speaker.setVolume(32);
  M5.Display.fillScreen(TFT_RED);

  play_boot();   // 起動音
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed())       { beep_press();   M5.Display.fillScreen(TFT_BLUE); }
  if (M5.BtnA.wasReleased())      { beep_release(); M5.Display.fillScreen(TFT_RED);  }
  if (M5.BtnA.wasClicked())       { play_fanfare(); }
  if (M5.BtnA.wasDoubleClicked()) { play_scale();   }
  if (M5.BtnA.wasHold())          { beep_ok();      }
}