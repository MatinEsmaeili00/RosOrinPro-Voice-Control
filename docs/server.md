# Server (DGX Spark)

`server.py` is a small [FastAPI](https://fastapi.tiangolo.com/) app. It turns text or speech into one robot command.

## Requirements

- Python 3.10+ (tested with 3.12 on the DGX Spark, aarch64)
- [Ollama](https://github.com/ollama/ollama) running locally with a model pulled (default `gemma4:e4b`)
- About 150 MB for the Whisper `base` model (downloaded automatically on first start)

## Install and run

```bash
cd RosOrinPro-Voice-Control
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
ollama pull gemma4:e4b
./run.sh
```

`run.sh` activates the venv and starts `uvicorn server:app --host 0.0.0.0 --port 8002`. Check it:

```bash
curl http://localhost:8002/health
# {"status":"bridge_ready","llm_model":"gemma4:e4b","llm_loaded":true,"whisper_model":"base"}
```

At startup the server:
1. loads Whisper (a few seconds), and
2. warms up the LLM in a background thread (an empty `/api/generate` call with `keep_alive: 1h`), so the first real
   command isn't slowed down by a ~10 s cold load. `llm_loaded` in `/health` turns `true` when that finishes.

> **Important: PyAV version.** `requirements.txt` pins `av==17.1.0`. PyAV 18 and newer removed the
> `metadata_errors` argument that faster-whisper 1.2.1 still passes to `av.open()`. With a newer PyAV every voice
> command fails with `TypeError: open() got an unexpected keyword argument 'metadata_errors'`.

## Configuration

Set environment variables before `./run.sh`, for example `LLM_MODEL=llama3.1:8b PORT=8003 ./run.sh`.

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8002` | HTTP port |
| `OLLAMA_URL` | `http://localhost:11434` | Ollama server |
| `LLM_MODEL` | `gemma4:e4b` | any model from `ollama list` |
| `LLM_KEEP_ALIVE` | `1h` | how long Ollama keeps the model in GPU memory after a request |
| `LLM_TIMEOUT_S` | `45` | timeout for one LLM call |
| `WHISPER_MODEL` | `base` | `tiny`, `base`, `base.en`, `small.en`, … |
| `WHISPER_DEVICE` | `cpu` | `cuda` only if your CTranslate2 build supports it (the aarch64 pip wheel is CPU-only) |
| `WHISPER_COMPUTE` | `int8` | CTranslate2 compute type |
| `SAVE_LAST_AUDIO` | `1` | save the last uploaded clip as `last_voice.wav` (useful to hear what Unreal sent) |

Speed and range limits for the parser are constants at the top of `robot_commands.py`: `SLOW_SPEED`, `MIN_SPEED`,
`MAX_DURATION_S`, `MAX_DISTANCE_M`, `A_LITTLE_S`.

## HTTP API

### `GET /health`
```json
{"status": "bridge_ready", "llm_model": "gemma4:e4b", "llm_loaded": true, "whisper_model": "base"}
```

### `POST /command_text`
Request body: `{"text": "turn right 90 degrees"}`

```bash
curl -X POST http://localhost:8002/command_text -H 'Content-Type: application/json' -d '{"text":"turn right 90 degrees"}'
```

### `POST /command_voice`
`multipart/form-data` with one file field called `file`. Any format PyAV can decode works (WAV, MP3, OGG, …). Unreal
sends 16-bit mono PCM WAV.

```bash
curl -F file=@command.wav http://localhost:8002/command_voice
```

### Response (all command endpoints)

```json
{
  "transcript": "turn right 90 degrees",
  "action": "move",
  "command": "turn_right",
  "forward": 0.0,
  "strafe": 0.0,
  "turn": -1.0,
  "duration_s": 0.0,
  "distance_m": 0.0,
  "angle_deg": 90.0,
  "spoken_response": "Turning right 90 degrees.",
  "source": "rules",
  "latency_ms": 2
}
```

Field meanings are in [commands.md](commands.md#json-protocol).

## How a request is processed

1. **Speech to text** (`/command_voice` only): [faster-whisper](https://github.com/SYSTRAN/faster-whisper) `base`,
   `language="en"`, `beam_size=5`, `vad_filter=True` ([Silero VAD](https://github.com/snakers4/silero-vad) cuts
   silence). The WAV is decoded straight from memory, with no temp files. A lock allows one transcription at a time.
2. **Rules** (`robot_commands.parse_with_rules`). Details in [commands.md](commands.md#how-parsing-works).
3. **LLM fallback** (`robot_commands.call_llm`) if the rules return nothing:
   - `POST /api/chat` with `"format": <JSON schema>`, `"think": false`, `"temperature": 0`, `"keep_alive": "1h"`
     ([Ollama structured outputs](https://ollama.com/blog/structured-outputs)).
   - The schema only allows `command ∈ {forward, backward, strafe_left, strafe_right, turn_left, turn_right, stop,
     unknown}` plus numeric `speed`, `duration_s`, `distance_m`, `angle_deg` and a `spoken_response`.
   - The result is validated: numbers are clamped, the direction vector is built by the server (never taken from the
     LLM), and a strafe or turn is rejected if the user never said a matching word.
   - If the model rejects `think` (models without a thinking mode), the call is retried without it.
4. **Response** with timing (`latency_ms`). Every command is also printed as one log line:
   ```
   [rules 401ms] "Move forward." -> move forward | f=1.0 s=0.0 t=0.0 | 0.0s 0.0m 0.0deg
   ```

The endpoints are plain `def` functions, not `async`, so FastAPI runs them in a thread pool. A "stop" arriving while
Whisper or the LLM is busy with another request is still answered right away (measured: 3 ms during a 1 s LLM
call).

## Latency (measured on the DGX Spark)

| Path | Time |
|---|---|
| Text, rules | 1–5 ms |
| Text, LLM fallback | 1.0–1.7 s |
| Voice, rules (Whisper included) | ≈ 0.4 s |
| Voice, LLM fallback | ≈ 1.5 s |
| First LLM call without warm-up | ≈ 10 s (model load), which is why the server warms up at start |

## Start automatically at boot (optional)

Change the two paths to where the repo lives. On the lab's DGX Spark it is `~/RosOrinPro_ai_bridge`.

```ini
# ~/.config/systemd/user/rosorin-voice.service
[Unit]
Description=RosOrinPro voice bridge
After=network-online.target

[Service]
WorkingDirectory=%h/RosOrinPro_ai_bridge
ExecStart=%h/RosOrinPro_ai_bridge/run.sh
Restart=on-failure

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now rosorin-voice
loginctl enable-linger $USER     # keep it running when you're logged out
journalctl --user -u rosorin-voice -f
```

## Differences from `syngenta-ai-bridge`

| | syngenta-ai-bridge | this project |
|---|---|---|
| Task | highlight scene objects | drive a robot |
| LLM API | OpenAI-compatible `/v1/chat/completions` (port 8000, `meta/llama-3.1-8b-instruct`) | Ollama native `/api/chat` with a JSON schema (`gemma4:e4b`) |
| Voice endpoint | `async def`, temp file | thread pool, in-memory decode, runs alongside other requests |
| Port | 8001 | 8002 |
