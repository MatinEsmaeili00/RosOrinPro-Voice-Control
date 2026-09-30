"""
End-to-end voice tests for the bridge, without Unreal or a microphone.

Speech is generated with Piper TTS, then packed EXACTLY like
CPP_AICommandComponent::BuildWavFile does it in Unreal (48 kHz stereo float mic ->
mono 16-bit PCM WAV with auto-gain) and uploaded to /command_voice as multipart.

Setup (once):
  python3 -m venv .venv-tools && .venv-tools/bin/pip install -r tools/requirements.txt
  cd tools && ../.venv-tools/bin/python -m piper.download_voices en_US-lessac-medium

Run (bridge must be running):
  .venv-tools/bin/python tools/voice_pipeline_test.py phrases        # a few commands
  .venv-tools/bin/python tools/voice_pipeline_test.py stop-stress    # 80 stop clips, none may move
  .venv-tools/bin/python tools/voice_pipeline_test.py move-stress    # 40 movement clips
  .venv-tools/bin/python tools/voice_pipeline_test.py concurrency    # "stop" during a slow LLM call
  .venv-tools/bin/python tools/voice_pipeline_test.py all
"""

import argparse
import collections
import io
import os
import struct
import threading
import time
import wave

import numpy as np
import requests
from piper import PiperVoice, SynthesisConfig


HERE = os.path.dirname(os.path.abspath(__file__))

PHRASES = [
    "Move forward.", "Stop!", "Robot, stop!", "Turn left ninety degrees.", "Go backward for two seconds.",
    "Slide to the right.", "Could you scoot up a tiny bit?", "Turn around.",
    "Go forward half a meter, then turn left.", "What's the weather like?",
]

STOP_WORDS = ["Stop!", "Stop.", "Stop!!", "Halt!", "Wait!", "No!", "Stop it!", "Robot, stop!", "Stop stop!", "Freeze!"]

MOVE_CASES = {
    "Move forward.": "forward", "Go backward.": "backward", "Turn left.": "turn_left",
    "Turn right.": "turn_right", "Slide to the left.": "strafe_left", "Strafe right.": "strafe_right",
    "Go forward two meters.": "forward", "Turn around.": "turn_left",
}


class UnrealLikeMic:
    def __init__(self, voice_path: str, server: str):
        self.voice = PiperVoice.load(voice_path)
        self.server = server.rstrip("/")
        self.rng = np.random.default_rng(0)

    def speak(self, text: str, take: int = 0) -> np.ndarray:
        """TTS -> float mono at the TTS rate. 'take' varies voice noise and speed."""
        config = SynthesisConfig(noise_scale=0.3 + 0.1 * take, length_scale=0.8 + 0.06 * take)
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as wav:
            self.voice.synthesize_wav(text, wav, syn_config=config)
        buffer.seek(0)
        with wave.open(buffer) as wav:
            rate = wav.getframerate()
            pcm = np.frombuffer(wav.readframes(wav.getnframes()), dtype=np.int16)
        return pcm.astype(np.float32) / 32768.0, rate

    def capture(self, text: str, take: int = 0, mic_rate: int = 48000, channels: int = 2) -> tuple:
        """Simulates a quiet stereo headset mic at 48 kHz with some noise and silence around the words."""
        mono, rate = self.speak(text, take)
        new_times = np.arange(int(len(mono) * mic_rate / rate)) / mic_rate
        gain = 0.05 + 0.05 * (take % 4)
        mono = np.interp(new_times, np.arange(len(mono)) / rate, mono) * gain
        pad = np.zeros(int(0.4 * mic_rate), dtype=np.float32)
        mono = np.concatenate([pad, mono, pad])
        mono = mono + self.rng.normal(0, 0.002, len(mono))
        interleaved = np.repeat(mono[:, None], channels, axis=1).reshape(-1).astype(np.float32)
        return interleaved, channels, mic_rate

    @staticmethod
    def build_wav(samples: np.ndarray, channels: int, rate: int) -> bytes:
        """Same logic as CPP_AICommandComponent::BuildWavFile."""
        frames = len(samples) // channels
        mono = samples[: frames * channels].reshape(frames, channels).mean(axis=1)
        peak = float(np.max(np.abs(mono))) if frames else 0.0
        if 0.001 < peak < 0.5:
            mono = mono * min(0.9 / peak, 8.0)
        pcm = (np.clip(mono, -1, 1) * 32767).astype("<i2").tobytes()
        header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
        header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
        header += b"data" + struct.pack("<I", len(pcm))
        return header + pcm

    def send(self, text: str, take: int = 0) -> dict:
        wav = self.build_wav(*self.capture(text, take))
        files = {"file": ("command.wav", wav, "audio/wav")}
        return requests.post(f"{self.server}/command_voice", files=files, timeout=60).json()


def run_phrases(mic: UnrealLikeMic) -> None:
    print("\n== phrases ==")
    for text in PHRASES:
        r = mic.send(text)
        print(
            f"said {text!r:44} heard {r['transcript']!r:44} -> {r['action']:7} {str(r['command']):12} "
            f"[{r['source']}, {r['latency_ms']} ms]"
        )


def run_stop_stress(mic: UnrealLikeMic) -> int:
    print("\n== stop stress: 8 takes x 10 stop words, NONE may come back as 'move' ==")
    tally, moves = collections.Counter(), []
    for word in STOP_WORDS:
        for take in range(8):
            r = mic.send(word, take)
            tally[(word, r["action"])] += 1
            if r["action"] == "move":
                moves.append((word, r["transcript"], r["command"]))
    print(f"{'said':14} {'stop':>5} {'unknown':>8} {'move':>5}")
    for word in STOP_WORDS:
        print(f"{word:14} {tally[(word, 'stop')]:5} {tally[(word, 'unknown')]:8} {tally[(word, 'move')]:5}")
    total_stop = sum(v for (w, a), v in tally.items() if a == "stop")
    total_unknown = sum(v for (w, a), v in tally.items() if a == "unknown")
    print(f"TOTAL          {total_stop:5} {total_unknown:8} {len(moves):5}")
    print("moves (must be empty):", moves or "none")
    return len(moves)


def run_move_stress(mic: UnrealLikeMic) -> int:
    print("\n== move stress: 5 takes x 8 commands ==")
    wrong = []
    for text, expected in MOVE_CASES.items():
        correct = 0
        for take in range(5):
            r = mic.send(text, take)
            if r["command"] == expected:
                correct += 1
            else:
                wrong.append((text, r["transcript"], r["action"], r["command"]))
        print(f"{text:26} {correct}/5")
    print("wrong:", wrong or "none")
    dangerous = [w for w in wrong if w[2] == "move"]
    print("wrong AND moved (dangerous):", dangerous or "none")
    return len(dangerous)


def run_concurrency(server: str) -> None:
    print("\n== concurrency: 'stop' while a slow LLM request is running ==")
    result = {}

    def slow_request():
        start = time.perf_counter()
        requests.post(f"{server}/command_text", json={"text": "could you please mosey on over toward the far wall"}, timeout=60)
        result["slow"] = time.perf_counter() - start

    thread = threading.Thread(target=slow_request)
    thread.start()
    time.sleep(0.2)
    start = time.perf_counter()
    stop = requests.post(f"{server}/command_text", json={"text": "stop"}, timeout=60).json()
    result["stop"] = time.perf_counter() - start
    thread.join()
    print(f"stop answered in {result['stop'] * 1000:.0f} ms ({stop['action']}) while the LLM request took {result['slow'] * 1000:.0f} ms")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("test", choices=["phrases", "stop-stress", "move-stress", "concurrency", "all"])
    parser.add_argument("--server", default=os.getenv("BRIDGE_URL", "http://localhost:8002"))
    parser.add_argument("--voice", default=os.path.join(HERE, "en_US-lessac-medium.onnx"))
    args = parser.parse_args()

    if args.test == "concurrency":
        run_concurrency(args.server)
        return

    mic = UnrealLikeMic(args.voice, args.server)
    problems = 0

    if args.test in ("phrases", "all"):
        run_phrases(mic)
    if args.test in ("stop-stress", "all"):
        problems += run_stop_stress(mic)
    if args.test in ("move-stress", "all"):
        problems += run_move_stress(mic)
    if args.test == "all":
        run_concurrency(args.server)

    raise SystemExit(1 if problems else 0)


if __name__ == "__main__":
    main()
