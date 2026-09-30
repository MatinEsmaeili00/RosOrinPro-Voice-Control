# RosOrinPro AI Bridge  (runs on the DGX Spark)
#
#   Unreal --(text or voice)--> this server --(JSON command)--> Unreal --(/cmd_vel)--> rosbridge --> robot
#
# To run this code, put this in the terminal:
#   cd ~/RosOrinPro_ai_bridge
#   ./run.sh
#
# or the long way:
#   source .venv/bin/activate
#   uvicorn server:app --host 0.0.0.0 --port 8002

import io
import os
import threading
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI, File, UploadFile
from faster_whisper import WhisperModel
from pydantic import BaseModel

import robot_commands


WHISPER_MODEL = os.getenv("WHISPER_MODEL", "base")
WHISPER_DEVICE = os.getenv("WHISPER_DEVICE", "cpu")
WHISPER_COMPUTE = os.getenv("WHISPER_COMPUTE", "int8")

# Keep a copy of the last voice clip so you can listen to what Unreal actually sent.
SAVE_LAST_AUDIO = os.getenv("SAVE_LAST_AUDIO", "1") == "1"
LAST_AUDIO_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "last_voice.wav")


# Load Whisper once when the server starts.
whisper_model = WhisperModel(WHISPER_MODEL, device=WHISPER_DEVICE, compute_type=WHISPER_COMPUTE)
whisper_lock = threading.Lock()


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Load the LLM in the background so the server is reachable right away.
    threading.Thread(target=robot_commands.warm_up_llm, daemon=True).start()
    yield


app = FastAPI(title="RosOrinPro AI Bridge", lifespan=lifespan)


class TextCommand(BaseModel):
    text: str


def run_command(user_text: str, started: float) -> dict:
    result = robot_commands.parse_command(user_text)
    result["latency_ms"] = round((time.perf_counter() - started) * 1000)

    print(
        f"[{result['source']} {result['latency_ms']}ms] \"{result['transcript']}\" -> "
        f"{result['action']} {result['command']} | f={result['forward']} s={result['strafe']} "
        f"t={result['turn']} | {result['duration_s']}s {result['distance_m']}m {result['angle_deg']}deg"
    )

    return result


def transcribe_audio(audio_bytes: bytes) -> str:
    """Converts WAV bytes into text using faster-whisper."""

    with whisper_lock:
        segments, info = whisper_model.transcribe(
            io.BytesIO(audio_bytes),
            language="en",
            beam_size=5,
            vad_filter=True,
        )
        return " ".join(segment.text for segment in segments).strip()


@app.get("/health")
def health():
    return {
        "status": "bridge_ready",
        "llm_model": robot_commands.LLM_MODEL,
        "llm_loaded": robot_commands.llm_ready,
        "whisper_model": WHISPER_MODEL,
    }


@app.post("/command_text")
def command_text(command: TextCommand):
    return run_command(command.text, time.perf_counter())


# Plain "def" (not async) so FastAPI runs it in a worker thread: a "stop" sent
# while Whisper is busy is still answered immediately.
@app.post("/command_voice")
def command_voice(file: UploadFile = File(...)):
    started = time.perf_counter()
    audio_bytes = file.file.read()

    print(f"Received voice file: {file.filename} ({len(audio_bytes)} bytes)")

    if len(audio_bytes) == 0:
        return run_command("", started)

    if SAVE_LAST_AUDIO:
        with open(LAST_AUDIO_PATH, "wb") as last_audio:
            last_audio.write(audio_bytes)

    try:
        transcript = transcribe_audio(audio_bytes)
    except Exception as error:
        print("Voice command error:", repr(error))
        result = run_command("", started)
        result["spoken_response"] = "I had trouble understanding the voice command."
        return result

    print("Whisper transcript:", transcript)

    return run_command(transcript, started)
