"""
耳（STT）: mlx-whisper を whisper.cpp サーバー互換の HTTP API でラップする。

起動:
    uvicorn whisper_server:app --host 0.0.0.0 --port 8080

エンドポイント:
    POST /inference  multipart/form-data, フィールド名 "file" (WAV 16kHz/mono)
    → {"text": "転写テキスト"}

環境変数:
    WHISPER_MODEL     mlx-community の MLX 変換済みモデル（既定: whisper-medium-mlx）
    WHISPER_LANGUAGE  言語コード（既定: ja）
"""

import io
import os
import time

import mlx_whisper
import numpy as np
import soundfile as sf
from fastapi import FastAPI, File, HTTPException, UploadFile

MODEL_ID = os.environ.get("WHISPER_MODEL", "mlx-community/whisper-medium-mlx")
LANGUAGE = os.environ.get("WHISPER_LANGUAGE", "ja")
SAMPLE_RATE = 16000  # 端末は 16kHz mono で送ってくる

print(f"[whisper] model={MODEL_ID} language={LANGUAGE}")


def transcribe(audio: np.ndarray) -> str:
    if audio.ndim > 1:  # 念のため mono 化
        audio = audio.mean(axis=1)
    result = mlx_whisper.transcribe(
        audio.astype(np.float32, copy=False),
        path_or_hf_repo=MODEL_ID,
        language=LANGUAGE,
    )
    return result["text"].strip()


# 起動時ウォームアップ（初回リクエストのモデルロード/コンパイルを先に済ませる）
print("[whisper] warming up...")
_t = time.perf_counter()
transcribe(np.zeros(SAMPLE_RATE, dtype=np.float32))
print(f"[whisper] ready ({(time.perf_counter() - _t) * 1000:.0f} ms)")


app = FastAPI()


@app.post("/inference")
async def inference(file: UploadFile = File(...)):
    audio_bytes = await file.read()
    if not audio_bytes:
        raise HTTPException(status_code=400, detail="empty file")

    try:
        with io.BytesIO(audio_bytes) as buf:
            data, sr = sf.read(buf)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"failed to read audio: {e}")

    audio_sec = len(data) / sr if sr else 0.0
    t0 = time.perf_counter()
    text = transcribe(data)
    infer_ms = (time.perf_counter() - t0) * 1000
    print(f"[whisper] audio={audio_sec:.2f}s infer={infer_ms:.0f}ms text={text!r}")
    return {"text": text}
