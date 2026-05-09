# 俺はAI Chatbotを自作したい

M5Stack **AtomS3R** + **Atomic Echo Base** で AI Chatbot を作るまでの勉強記録のコード置き場です。

## なぜこのリポジトリ？

なぜかM5Stackシリーズで AI Chatbot を作りたい。
そう思ってまず手を出したのが、**StackChan Minimal** というオープンソースプロジェクトでした。

これがすごくよく出来ていて、ウェブインストール一発で動いてしまう…
これだと勉強にはならなかったので、**一個一個自分で書きながら積み上げていく**ことにしました。

Arduinoはそこそこいじってきたけど、UIFLOW2とかM5Burnerとかの挙動がマジでわからなくなって、結局 **PlatformIO** をベースに制作することに。

このリポジトリは、その勉強の各章ごとに動くコードを置いていく場所です。

---

## 各章（仮）

ゴールから逆算したロードマップ。実際にやってみて変わるかも。寄り道もするかもしれないけど勉強記録なので許してね。

| # | フォルダ | 内容 | ブログ記事 |
|---|---|---|---|
| ① | [01_LED_Blink](./01_LED_Blink) | PlatformIO環境構築とLチカ（画面の色変更＋シリアル出力） | 公開予定 |
| ② | – | Atomic Echo Base で音を鳴らす | – |
| ③ | – | マイクで音量を測ってみる | – |
| ④ | – | Wi-Fi に繋いで HTTP GET | – |
| ⑤ | – | whisper.cpp に喋ったことを文字起こしさせる | – |
| ⑥ | – | Ollama / llama.cpp に話を聞いてもらう | – |
| ⑦ | – | piper-plus でしゃべらせる | – |
| ⑧ | – | 全部つないで会話させる | – |

ここまで辿り着いたら、**自作の AI Chatbot** が完成しているはず。

---

## 使い方

各章は **独立した PlatformIO プロジェクト** になっています。
VSCode で章のフォルダを直接開いてください。

```bash
# 例: 第1章を開く
code 01_LED_Blink
```

> ⚠️ このリポジトリのルート（`ai-chatbot-sample/`）を VSCode で開くと PlatformIO が認識しません。**章フォルダを個別に開く**のが正しい使い方です。

開いたら下部ステータスバーから：

| アイコン | 機能 |
|---|---|
| ✓ | Build |
| → | Upload |
| 🔌 | Serial Monitor |

---

## 共通の前提

- M5Stack **AtomS3R**
- **Atomic Echo Base**（②章以降で使用）
- **USB-C ケーブル（データ通信対応）**
- macOS + VSCode + PlatformIO IDE

---

## 参考

- [StackChan Minimal](https://github.com/A-Uta/AI_StackChan_Minimal) — このリポジトリの動機になった素晴らしいプロジェクト
- [PlatformIO 公式ドキュメント](https://docs.platformio.org/)
- [M5Unified](https://github.com/m5stack/M5Unified)

---

## ライセンス

[MIT License](./LICENSE)

学習用のサンプルコードです。自由に参考・改変してください。
