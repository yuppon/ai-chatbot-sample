# 俺はAI Chatbotを自作したい

M5Stack **AtomS3R** + **Atomic Echo Base** で AI Chatbot を作るまでの勉強記録のコード置き場です。

**全10章でボタンを押すと声で会話してくれるシンプルなチャットボットをつくります。** ぜんぶローカル、クラウドAPIは1つも使っていません。

---

## なぜこのリポジトリ？

なぜかM5Stackシリーズで AI Chatbot を作りたい。
そう思ってまず手を出したのが、**StackChan Minimal** というオープンソースプロジェクトでした。

これがすごくよく出来ていて、ウェブインストール一発で動いてしまう…
これだと勉強にはならなかったので、**一個一個自分で書きながら積み上げていく**ことにしました。

Arduinoはそこそこいじってきたけど、UIFLOW2とかM5Burnerとかの挙動がマジでわからなくなって、結局 **PlatformIO** をベースに制作することに。

このリポジトリは、その勉強の各章ごとに動くコードを置いていく場所です。

---

## 完成形

```
[AtomS3R] ボタン押下中 → マイク録音
   ↓ 離す
[Mac: whisper-server]   音声 → 文字     (耳)
   ↓
[Mac: Ollama + Gemma 3] 文字 → 返事     (脳)
   ↓
[Mac: VOICEVOX]         返事 → 音声     (口)
   ↓
[AtomS3R] スピーカーで再生
```

ハードは AtomS3R と Atomic Echo Base だけ。Mac 側で 3つのローカルサーバー（耳・脳・口）を動かして、HTTP で繋いでいます。

---

## 各章
申し訳ありません。こちらがMarkdown形式のテーブルです。

| # | フォルダ | ブログ記事 |
|---|---|---|
| ① | [01_LED_Blink](https://github.com/yuppon/ai-chatbot-sample/blob/main/01_LED_Blink) | [PlatformIO環境構築とLチカ（画面の色変更＋シリアル出力）](https://zenn.dev/yuppon/articles/264bc4ed88a126) |
| ② | [02_Speaker_Tone](https://github.com/yuppon/ai-chatbot-sample/blob/main/02_Speaker_Tone) | [Atomic Echo Base で音を鳴らす](https://zenn.dev/yuppon/articles/fc1bf784f302f2) |
| ③ | [03_Mic_Level](https://github.com/yuppon/ai-chatbot-sample/blob/main/03_Mic_Level) | [マイクで音量を測ってみる](https://zenn.dev/yuppon/articles/64a8dcf3b34e49) |
| ④ | [04_WiFi_HTTPGet](https://github.com/yuppon/ai-chatbot-sample/blob/main/04_WiFi_HTTPGet) | [Wi-Fi に繋いで HTTP GET（httpbin）](https://zenn.dev/yuppon/articles/ce524c41bed3f5) |
| ⑤ | – (Mac側) | [Mac に whisper.cpp サーバーを立てる](https://zenn.dev/yuppon/articles/1e5744eab9885f) |
| ⑥ | [06_PTT_Record](https://github.com/yuppon/ai-chatbot-sample/blob/main/06_PTT_Record) | [PTT録音 → whisper で文字起こし](https://zenn.dev/yuppon/articles/e6a71ec1b2fa31) |
| ⑦ | – (Mac側) | [Mac に Ollama + Gemma 3 を立てる](https://zenn.dev/yuppon/articles/cb428076f54072) |
| ⑧ | [08_LLM_Chat](https://github.com/yuppon/ai-chatbot-sample/blob/main/08_LLM_Chat) | [文字起こし結果を Gemma に投げて返事を表示](https://zenn.dev/yuppon/articles/f902dd182e0c75) |
| ⑨ | [09_TTS_Demo](https://github.com/yuppon/ai-chatbot-sample/blob/main/09_TTS_Demo) | [VOICEVOX で合成した音声を再生（最小デモ）](https://zenn.dev/yuppon/articles/a3f6f84b814b80) |
| ⑩ | [10_Full_Chat](https://github.com/yuppon/ai-chatbot-sample/blob/main/10_Full_Chat) | [耳・脳・口を全部繋いで音声会話（最終回）](https://zenn.dev/yuppon/articles/9e1b44081d8dca) |

⑤と⑦は Mac 側のセットアップ回なので、このリポジトリにコードはありません。

---

## 技術スタック

| 役割 | 採用 | 備考 |
|---|---|---|
| デバイス | AtomS3R + Atomic Echo Base | ESP32-S3-PICO-1-N8R8（8MB Flash + 8MB PSRAM） |
| 開発環境 | PlatformIO + M5Unified | VSCodeから章ごとにビルド |
| STT（耳） | whisper.cpp（medium） | Mac で CMake ビルド、HTTP サーバー化 |
| LLM（脳） | Ollama + Gemma 3 12B | 来歴・透明性重視で Google 系を採用 |
| TTS（口） | VOICEVOX（ずんだもん / 春日部つむぎ等） | 日本語 TTS のデファクト |
| JSON | ArduinoJson v7 | サイズ指定不要の `JsonDocument` |

### 採用しなかった候補（敬意を込めて）

- **piper**：当初構想で予定していたが、日本語が「まだ開発中」のため断念。英語チャットボットなら最速の選択肢
- **Qwen3 / DeepSeek**：性能は優秀だが、来歴ポリシー（中国系モデルを避けたい）の観点で外した
- **LM Studio**：GUI評価フェーズには便利だが、常時起動の裏方サーバー用途にはGUIの強みが活きない＋本体がクローズドソース
- **Docker**：Mac 上では Metal にアクセスできず CPU 動作に落ちるため、Mac で完結する今回の構成では採用せず

詳しい比較は各章のブログ記事に書いています。

### 「いずれ乗り換える」候補

シリーズ完了時点での予定。土台ができた今ならスムーズに移れるはず。

- **LLM**: Ollama → **llama.cpp**（whisper.cpp と同じ流儀で全部「自分でビルド」の一貫性）
- **TTS**: VOICEVOX → **Style-Bert-VITS2**（品質と感情表現が一段上、自分の声でも学習可能）

---

## 使い方

各章は **独立した PlatformIO プロジェクト** になっています。
VSCode で章のフォルダを直接開いてください。

```bash
# 例: 最終章を開く
code 10_Full_Chat
```

> ⚠️ このリポジトリのルート（`ai-chatbot-sample/`）を VSCode で開くと PlatformIO が認識しません。**章フォルダを個別に開く**のが正しい使い方です。

開いたら下部ステータスバーから：

| アイコン | 機能 |
|---|---|
| ✓ | Build |
| → | Upload |
| 🔌 | Serial Monitor |

### 章④以降は `secrets.h` の作成が必要

Wi-Fi のパスワードや Mac の IP は `secrets.h` に逃がし、Git に上げない運用にしています。各章フォルダの `src/secrets.h.example` を `secrets.h` にコピーして、自分の値で書き換えてください。

```bash
cp 10_Full_Chat/src/secrets.h.example 10_Full_Chat/src/secrets.h
# secrets.h を編集
```

### Mac 側のサーバー起動（章⑩を動かすとき）

```bash
# 耳
cd ~/develop/whisper.cpp
./build/bin/whisper-server -m models/ggml-medium.bin -l ja --host 0.0.0.0 --port 8080 --convert

# 脳
OLLAMA_HOST=0.0.0.0 ollama serve

# 口
/Applications/VOICEVOX.app/Contents/Resources/vv-engine/run --host 0.0.0.0 --port 50021
```

3つとも `0.0.0.0` で待ち受けているのがポイント。詳細は章⑤⑦⑨のブログを参照。

---

## 共通の前提

- M5Stack **AtomS3R**
- **Atomic Echo Base**（②章以降で使用）
- **USB-C ケーブル（データ通信対応）**
- macOS（Apple Silicon 推奨）+ VSCode + PlatformIO IDE
- 同一 LAN 上の Mac と AtomS3R

---

## この後の予定（続編構想）

- **VLM 編**：AtomS3R に「目」を付ける。カメラで撮った画像を VLM（Vision-Language Model）に投げて、見たものを喋らせる
- **Hermes 編**：LLM の脳を Hermes に乗せ換えて、ツール呼び出し（天気API、時刻取得など）ができるチャットボットへ
- **会話の文脈を覚える**：いまは1往復ずつ独立。`messages` 配列に履歴を溜めれば、覚えてる相棒になる
- **VAD で常時起動化**：ボタン押しっぱなしから、喋ったら勝手に録音する方式へ
- **エピローグ①**：LLM を Ollama → llama.cpp に乗り換え
- **エピローグ②**：TTS を VOICEVOX → Style-Bert-VITS2 に乗り換え

---

## 参考

- [StackChan Minimal](https://github.com/A-Uta/AI_StackChan_Minimal) — このリポジトリの動機になった素晴らしいプロジェクト
- [PlatformIO 公式ドキュメント](https://docs.platformio.org/)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- [Ollama](https://ollama.com/)
- [VOICEVOX](https://voicevox.hiroshiba.jp/)

---

## ライセンス

[MIT License](./LICENSE)
学習用のサンプルコードです。自由に参考・改変してください。

## クレジット

本プロジェクトでは音声合成に [VOICEVOX](https://voicevox.hiroshiba.jp/) を使用しています。

- VOICEVOX:ずんだもん
- VOICEVOX:春日部つむぎ

各キャラクターの利用規約に従ってご利用ください。
