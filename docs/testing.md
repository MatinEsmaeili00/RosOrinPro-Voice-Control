# Testing

Everything below was run on the DGX Spark on 2026-09-29 against the final code.

## 1. Parser unit tests (no server needed)

```bash
.venv/bin/python test_commands.py          # rules only, instant
.venv/bin/python test_commands.py --llm    # also the Ollama fallback (Ollama must be running)
.venv/bin/python test_commands.py --say "go back half a meter"            # try one phrase locally
.venv/bin/python test_commands.py --server http://localhost:8002 --say "turn around"   # through the server
```

**Result: 64 / 64 pass.**

| Group | Count | What it checks |
|---|---|---|
| Rule cases | 48 | exact command, duration, distance, angle and speed for each phrase, including real Whisper outputs ("Go back word for two seconds.", "So, up.") |
| Must go to the LLM | 6 | the rules must **not** guess: "go up", "go forward then turn left", "don't go forward", "what can you do?", … |
| LLM cases | 10 | the LLM's answer must be in an allowed set: "scoot up a tiny bit" → forward/unknown, "don't go forward" → stop/unknown, "scoot over a tad" → never strafe |

## 2. End-to-end voice tests (no Unreal or mic needed)

`tools/voice_pipeline_test.py` produces speech with [Piper TTS](https://github.com/OHF-Voice/piper1-gpl) and
simulates the Unreal microphone path:

1. TTS → resample to **48 kHz stereo float**, the way a Windows mic arrives in `FAudioCaptureSynth`
2. quiet headset level (gain 0.05–0.2) + 0.4 s silence before and after + background noise
3. packed with **the same logic as `CPP_AICommandComponent::BuildWavFile`** (mono mix-down, auto-gain up to 8×,
   16-bit PCM, hand-written RIFF header)
4. uploaded as `multipart/form-data` to `/command_voice`, exactly as Unreal does it

Each "take" changes the voice's noise and speaking speed, so every clip is different.

```bash
python3 -m venv .venv-tools && .venv-tools/bin/pip install -r tools/requirements.txt
cd tools && ../.venv-tools/bin/python -m piper.download_voices en_US-lessac-medium && cd ..
.venv-tools/bin/python tools/voice_pipeline_test.py all --server http://localhost:8002
```

### Results

**Phrases**

| Said | Whisper heard | Result | Source, time |
|---|---|---|---|
| Move forward. | move forward | move forward | rules, 409 ms |
| Stop! | So | **stop** | rules, 413 ms |
| Robot, stop! | robot stop | stop | rules, 402 ms |
| Turn left ninety degrees. | Turn left 90 degrees. | move turn_left 90° | rules, 398 ms |
| Go backward for two seconds. | Go backward for 2 seconds. | move backward 2 s | rules, 467 ms |
| Slide to the right. | Slide to the right. | move strafe_right | rules, 377 ms |
| Could you scoot up a tiny bit? | (same) | move forward 1 s | llm, 1608 ms |
| Turn around. | Turn around. | move turn_left 180° | rules, 381 ms |
| Go forward half a meter, then turn left. | (same) | move forward (first step) | llm, 1599 ms |
| What's the weather like? | (same) | unknown | llm, 1541 ms |

**Stop stress (80 clips)**: 75 stop, 5 unknown, **0 moves**. The per-word table is in [safety.md](safety.md#how-reliable-is-a-spoken-stop).

**Move stress (40 clips)**

| Said | Correct |
|---|---|
| Move forward. | 5/5 |
| Go backward. | 5/5 |
| Turn left. | 5/5 |
| Turn right. | 5/5 |
| Slide to the left. | 5/5 |
| Strafe right. | 5/5 |
| Go forward two meters. | 5/5 |
| Turn around. | 5/5 |

In an earlier run, one "Strafe right." was heard as "Treyf Wright" and returned unknown. That's safe, and it's why
the docs recommend "slide".

**Concurrency**: "stop" answered in **3 ms** while a 1016 ms LLM request was running.

## 3. Whisper model comparison

`tools/whisper_benchmark.py` runs clean Piper clips through three model sizes (CPU int8, DGX Spark):

| Model | Avg per clip | "Stop!" heard as | Other 7 phrases |
|---|---|---|---|
| **base** (chosen) | **364 ms** | "Sup?" | all correct ("Halt!" → "Hold", "Stop." → "Stop!") |
| base.en | 438 ms | "up." | all correct |
| small.en | 1116 ms | "sub" | all correct |

A lone, clipped "Stop!" fooled every model size, so a bigger model doesn't fix it and costs 3× the time. The stop
problem is handled in the parser instead ([safety.md](safety.md)).

## 4. Unreal code review

No Unreal Engine is installed on the build machine, so the C++ **was not compiled here**. Instead it was reviewed
line by line against the UE 5 API documentation. The review found no compile or link problems. It did find 8
runtime issues, and all of them were fixed (see the [development log](development-log.md#6-unreal-code-review)), most
importantly a possible crash when stopping Play while the mic stream was open.

## First-run checklist on the real robot

1. **Wheels off the ground** (robot on a box) for the first tests.
2. Start the server (`./run.sh`) and open `http://130.39.94.33:8002/health` in a browser on the Unreal PC.
3. Build Unreal and press Play. The Output Log should show `CONNECTED TO ROBOT ROSBRIDGE` and `AI BRIDGE READY`.
4. **Typed first**: `~` → `AskAI "move forward for 2 seconds"`. The wheels should spin forward for 2 s and then stop.
5. Test **X** during a move: the wheels must stop at once.
6. Test directions: `AskAI "slide left for 2 seconds"` and `AskAI "turn left for 2 seconds"`. If either goes the wrong
   way, tick `bInvertStrafe` / `bInvertTurn`.
7. **Voice**: hold T, "move forward", release. The log shows `AI MIC: <device> | <channels> | <Hz>`. Then hold T,
   "stop stop". If the transcript is wrong, listen to `~/RosOrinPro_ai_bridge/last_voice.wav` on the Spark.
8. Test manual override: say "move forward", then press a movement key. The AI move must end immediately.
9. Stop Play while the robot is moving: the robot must stop.
10. Only then put the robot on the floor, in an open area, at the default low speeds.
