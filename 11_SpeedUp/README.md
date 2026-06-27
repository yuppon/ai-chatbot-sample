# 俺はAI Companionを自作したい
BODY & SOULを分離して、M5Stack S3Rはマイクとスピーカーのみ。
それ以外はMacで行う構成にしています。

基本はぜんぶローカルで完結しますが、LLM（脳）は `secrets.h` の設定だけで **ローカル Ollama / OpenAI / Gemini / Azure OpenAI** に切り替えられます。

---

## 完成形

```
耳 - [AtomS3R] ボタン押している間、マイク録音
   ↓
耳 - [Mac: mlx-whisper] TTS - 音声をテキスト化
   ↓
脳 - [Mac: Ollama or OPENAI API or Gemini] LLM - 応答生成 
   ↓
口 - [Mac: AivisSpeech] STT - 音響合成
   ↓
[AtomS3R] スピーカーで再生
```

ハードは AtomS3R と Atomic Echo Base だけ。
Mac 側で 3つのローカルサーバー（耳・脳・口）を動かし、HTTP で繋いでいます。

## 技術スタック

| 役割 | 採用 | 備考 |
|---|---|---|
| デバイス | AtomS3R + Atomic Echo Base | ESP32-S3-PICO-1-N8R8（8MB Flash + 8MB PSRAM） |
| 開発環境 | PlatformIO + M5Unified | VSCode でビルド |
| STT（耳） | mlx-whisper（Apple MLX ネイティブ） | Python + FastAPI ラッパー、HTTP サーバー化、**日本語前提** |
| LLM（脳） | Ollama + Gemma 3 12B（既定） | `secrets.h` で OpenAI / Gemini / Azure にも切替可 |
| TTS（口） | AivisSpeech（VOICEVOX互換API） | 日本語 TTS。ポート 10101、話者は style ID 指定 |
| JSON | ArduinoJson v7 | サイズ指定不要の `JsonDocument` |

---

## 使い方

**PlatformIO プロジェクト** です。VSCode でこのフォルダを開き、下部ステータスバーから Build（✓）/ Upload（→）/ Serial Monitor（🔌）。

### `secrets.h` の作成
`src/secrets.h.sample` を `secrets.h` にコピーして自分の値で書き換えてください。

Mac の IP（`secrets.h` の各 URL に使う）は次で調べます。

```bash
ipconfig getifaddr en0     # Wi-Fi。出なければ en1
```

### Mac 側のサーバー構成

| 役割 | サーバー | ポート | 備考 |
|---|---|---|---|
| 👂 耳（STT） | mlx-whisper（FastAPI ラッパー） | 8080 | 既定モデル `mlx-community/whisper-medium-mlx` |
| 🧠 頭（LLM） | Ollama（ローカル） / Gemini 等（クラウド） | 11434 | `secrets.h` の `LLM_PROVIDER` で切替。クラウド時は Ollama 不要。既定 `gemma3:12b` |
| 👄 口（TTS） | AivisSpeech Engine（VOICEVOX互換） | 10101 | 話者は style ID 指定 |

**いずれも `--host 0.0.0.0` で待ち受ける**（端末から LAN 経由で到達するため）。各サーバーは別ターミナルで起動する。

#### 👂 耳（STT / mlx-whisper）

初回セットアップ:
```bash
cd M5StackS3R/server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

起動（**日本語前提**。モデルは初回のみ自動ダウンロード。起動時ウォームアップでコールドスタート除去）:
```bash
cd M5StackS3R/server
source .venv/bin/activate
uvicorn whisper_server:app --host 0.0.0.0 --port 8080

# モデルを変える場合（mlx-community の MLX 変換済みモデルを指定）
WHISPER_MODEL=mlx-community/whisper-large-v3-mlx uvicorn whisper_server:app --host 0.0.0.0 --port 8080
```

#### 🧠 頭（LLM / Ollama）

ローカル LLM を使う場合のみ起動（クラウド API 利用時は不要）:
```bash
# 0.0.0.0 待受で端末から到達可能に（会話中は既定の keep-alive で温まったまま）
OLLAMA_HOST=0.0.0.0 ollama serve
```
> 長時間放置後の初回応答も即応にしたいなら `OLLAMA_KEEP_ALIVE=-1` を頭に付けてモデルを常駐させる。
クラウド（Gemini / OpenAI / Azure）への切替は [LLM をクラウドAPIに切り替える](#llm-をクラウドapiに切り替える) を参照。

#### 👄 口（TTS / AivisSpeech）

```bash
# ポート 10101、0.0.0.0 待受で端末から到達可能に
/Applications/AivisSpeech.app/Contents/Resources/AivisSpeech-Engine/run --host 0.0.0.0 --port 10101
```
> 話者（style ID）は `curl http://127.0.0.1:10101/speakers` で一覧取得し、`src/main.cpp` の `SPEAKER_ID` に設定する（既定 `1431611904` = まい ノーマル）。

### LLM をクラウドAPIに切り替える

脳（LLM）は `secrets.h` の `LLM_PROVIDER` を変えてビルドし直すだけで切り替えられます（**コンパイル時の切替**）。耳（whisper）と口（AivisSpeech）はローカルのままです。

```c
// secrets.h
#define LLM_PROVIDER  LLM_OLLAMA   // ← LLM_OLLAMA / LLM_OPENAI / LLM_GEMINI / LLM_AZURE
```

| プロバイダ | 設定する `#define` | APIキー取得先 |
|---|---|---|
| `LLM_OLLAMA` | `OLLAMA_URL`, `OLLAMA_MODEL` | （ローカル、キー不要） |
| `LLM_OPENAI` | `OPENAI_API_KEY`, `OPENAI_MODEL` | https://platform.openai.com/api-keys |
| `LLM_GEMINI` | `GEMINI_API_KEY`, `GEMINI_MODEL` | https://aistudio.google.com/apikey |
| `LLM_AZURE` | `AZURE_API_KEY`, `AZURE_ENDPOINT`, `AZURE_DEPLOYMENT`, `AZURE_API_VERSION` | Azure Portal（Azure OpenAI リソース） |

> ⚠️ クラウドAPIは HTTPS 接続です。本実装はプロトタイプ向けに TLS 証明書の検証をスキップしています（`WiFiClientSecure::setInsecure()`）。本番運用ではルートCA証明書の埋め込みを検討してください。
>
> ⚠️ APIキーはファームウェアに焼き込まれます。`secrets.h` は Git に上げない運用のままにしてください。

---

## ハマりポイント

- **WiFi が遅すぎて転送に数十秒かかる**：ESP32 の WiFi モデムスリープ（省電力）が原因。小さな TCP セグメントごとに復帰遅延が乗り、数十KBの送受信に数十秒かかることがある。接続後に `WiFi.setSleep(false)` を呼べば解消（[main.cpp](src/main.cpp) の `wifi_connect()` で実施済み）。
- サーバーは **必ず `--host 0.0.0.0`** で起動する（`127.0.0.1` のままだと端末から LAN 経由で到達できない）。

---

## 共通の前提

- M5Stack **AtomS3R** + **Atomic Echo Base**
- **USB-C ケーブル（データ通信対応）**
- macOS（Apple Silicon 推奨）+ VSCode + PlatformIO IDE
- 同一 LAN 上の Mac と AtomS3R

---

## 参考

- [StackChan Minimal](https://github.com/A-Uta/AI_StackChan_Minimal) — このリポジトリの動機になったプロジェクト
- [PlatformIO](https://docs.platformio.org/)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [mlx-whisper](https://pypi.org/project/mlx-whisper/)
- [Ollama](https://ollama.com/)
- [AiVis Speech](https://aivis-project.com/)

---

## ライセンス

[MIT License](./LICENSE) — 学習用のサンプルコードです。自由に参考・改変してください。
