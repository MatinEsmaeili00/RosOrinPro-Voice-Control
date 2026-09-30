"""
Compares Whisper model sizes on short robot commands (accuracy + speed on this machine).
This is how "base" was chosen: bigger models were slower and not better on a lone "Stop!".

  .venv-tools/bin/python tools/whisper_benchmark.py base base.en small.en
"""

import io
import os
import sys
import time
import wave

from faster_whisper import WhisperModel
from piper import PiperVoice


HERE = os.path.dirname(os.path.abspath(__file__))
PHRASES = ["Stop!", "Stop.", "Stop the robot.", "Halt!", "Move forward.", "Turn right.", "Go back.", "Wait!"]


def main() -> None:
    models = sys.argv[1:] or ["base", "base.en", "small.en"]
    voice = PiperVoice.load(os.path.join(HERE, "en_US-lessac-medium.onnx"))

    clips = {}
    for text in PHRASES:
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as wav:
            voice.synthesize_wav(text, wav)
        clips[text] = buffer.getvalue()

    for name in models:
        model = WhisperModel(name, device="cpu", compute_type="int8")
        model.transcribe(io.BytesIO(clips["Move forward."]), language="en")  # warm-up

        total, results = 0.0, []
        for text, clip in clips.items():
            start = time.perf_counter()
            segments, _ = model.transcribe(io.BytesIO(clip), language="en", beam_size=5, vad_filter=True)
            heard = " ".join(segment.text for segment in segments).strip()
            total += time.perf_counter() - start
            results.append(f"{text} -> {heard!r}")

        print(f"\n{name}: average {1000 * total / len(clips):.0f} ms per clip")
        for line in results:
            print("   ", line)


if __name__ == "__main__":
    main()
