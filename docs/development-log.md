# Development log

Built on 2026-09-29, pair-programming with Claude Code (Claude Opus 5.5) on the DGX Spark.

## 1. Starting point

- **Earlier project, `syngenta-ai-bridge`**: a FastAPI server on the DGX Spark. The game engine sends text or voice,
  Whisper transcribes it, rules and an LLM pick a scene object, and the engine highlights it.
- **Existing Unreal code** for the ROSOrin Pro:
  - `CPP_RosBridgeComponent`: rosbridge WebSocket, `/cmd_vel` publishing, `/odom` subscription driving the pawn
  - `CPP_RosOrinPawn`: robot pawn (mesh + RosBridge)
  - `CPP_PlayerController`: Enhanced Input keyboard driving through `SetRobotMovement`
- **Goal**: the same kind of AI bridge, but for motion: *"move forward"* → robot moves, *"stop"* → robot stops.

## 2. Decisions

| Question | Decision | Reason |
|---|---|---|
| Who talks to the robot? | Unreal (the server only returns a command) | Same pattern as syngenta. Unreal already owns the rosbridge connection. One owner of `/cmd_vel`. Local safety. |
| Which LLM? | Ollama `gemma4:e4b` (already installed on the Spark) | Nothing was running on port 8000 (the syngenta setup), and Ollama was. |
| How to get reliable output? | Ollama structured outputs (JSON schema in `format`) | The model can only produce the allowed commands. |
| Rules or LLM? | Rules first, LLM fallback | "stop" and common moves must be instant and deterministic. First test: the LLM needed 10 s cold and 0.5 s warm. |
| Command format | normalized `forward/strafe/turn` in -1…1, plus duration/distance/angle | Maps straight onto the existing `SetRobotMovement(Forward, Strafe, Turn)`. Speeds stay in Unreal. |
| Continuous moves | re-publish at 10 Hz, 20 s cap | Works with or without a base-side `cmd_vel` watchdog. |
| Mic capture in Unreal | `Audio::FAudioCaptureSynth` + a hand-built WAV | No submix or asset setup, and the API is stable across UE 5 versions. |
| Port | 8002 | syngenta keeps 8001, so both can run. |

## 3. Bug: voice uploads failed with PyAV 19

The first end-to-end voice test returned empty transcripts. Server log:
`TypeError("open() got an unexpected keyword argument 'metadata_errors'")`.

- The fresh venv had installed **PyAV 19.0.0**. faster-whisper 1.2.1 calls `av.open(..., metadata_errors="ignore")`,
  and that argument no longer exists in PyAV 18+.
- The syngenta venv (PyAV 17.1.0) was checked and is **not** affected.
- **Fix**: pin `av==17.1.0` in `requirements.txt`.

## 4. A misheard "stop" that would have driven forward

Voice tests with synthetic speech showed that Whisper sometimes turns a short, shouted **"Stop!"** into
**"So..."**, **"Sup?"**, **"up."** or **"Soap"**. All Whisper sizes do this (see
[testing.md](testing.md#3-whisper-model-comparison)), so a bigger model was not the answer.

The first fix mapped those fragments to stop. A later fix added "up" as a forward word so that "scoot up a tiny bit"
would work. The next voice test then produced:

```
said 'Stop!'   heard 'So, up.'   ->  move forward      <-- a shouted stop became FORWARD
```

**Fixes:**
- "up" removed as a direction from both the rules and the LLM prompt.
- **Fragment rule**: an utterance of ≤ 3 words made only of stop-like fragments ("so up", "robot sup") is a stop.
- The LLM prompt now says short fragments like "so/sup/up/top" are a misheard stop, and "when unsure, do NOT move".
- A stress test was added: 80 randomized stop-type clips must produce **zero** moves.

Later stress runs found more mishearings ("job", "Jump!", "Hope.", "Chop it!", "Please" for "Freeze!", "Salt" for
"Halt!", "Robot Stump", "Dope!"). They were added to the fragment list so they stop instantly instead of taking
1.5 s through the LLM. One run heard "Stop!" as **"Go!"**. The LLM answered unknown, but it could have said forward,
so a bare "go"/"move" is now always unknown ("Which way?").

The remaining mishearings vary from run to run ("Sock", "Dump.", "Ciao"). They are **not** added one by one:
they come back unknown, and Unreal stops a moving robot on unknown (`bStopOnUnknownCommand`).

## 5. LLM inventing a direction

"Could you scoot up a tiny bit?" was answered by the LLM as **strafe_right**. Later, "scoot over a tad" also came
back as strafe_right.

**Fix: a direction guard.** A strafe from the LLM is only accepted if the text contains left/right/side/sideways/slide,
and a turn only if it contains left/right/turn/rotate/around/clockwise/opposite/degrees. Otherwise the answer is
unknown, with "I'm not sure which way to go." The prompt also says "scoot" means a small move, not sideways.

## 6. Unreal code review

The C++ couldn't be compiled on the Spark (no Unreal Engine), so a second agent reviewed it against the UE 5 API
docs. It found no compile or link errors. It found 8 runtime issues, all fixed:

| # | Issue | Fix |
|---|---|---|
| 1 | **Crash on EndPlay**: the synth was destroyed while the mic thread was still calling into it | close the stream with `AbortCapturing()` (guarded by `IsStreamOpen()`) before `Reset()`, the same as `UAudioCaptureComponent` |
| 2 | `StopCapturing()` has internal `check()` asserts | no longer used. Open on press, `AbortCapturing()` on release |
| 3 | Answers arriving after EndPlay could re-drive the robot | EndPlay marks all requests stale, and `HasBegunPlay()` is checked in the response handler |
| 4 | A slow AI answer could start fighting keyboard driving | new `ManualOverride()` also drops pending answers. Called from `UpdateRobotMovement` |
| 5 | "Thinking…" could hide an instant error message | the status message is shown before the request is posted |
| 6 | Time math used unclamped values, and a tiny value would "move" with no motion | clamp to ±1 first. Below `DeadZone` → stop instead |
| 7 | Unknown mic format was silently treated as 1 ch / 48 kHz (garbled audio) | refuse to record if the device info is unavailable |
| 8 | "Listening…" disappeared at once when `MaxRecordSeconds` = 0 | show it for 60 s in that case |

## 7. Final state

| Check | Result |
|---|---|
| Parser unit tests | 64 / 64 |
| Stop stress (80 clips) | 75 stop, 5 unknown, 0 moves |
| Move stress (40 clips) | 40 / 40 |
| Stop during an LLM call | 3 ms |
| Unreal C++ | reviewed, 8 runtime fixes applied, **not yet compiled or run on the robot** |

## Files created

| File | Purpose |
|---|---|
| `server.py` | FastAPI app, Whisper, endpoints |
| `robot_commands.py` | rules, LLM call, validation, response building |
| `test_commands.py` | unit tests |
| `run.sh`, `requirements.txt` | run and install |
| `unreal/Source/CPP_AICommandComponent.*` | new Unreal component |
| `unreal/Source/CPP_RosOrinPawn.*`, `CPP_PlayerController.*` | small additions, marked `// AI BRIDGE` |
| `tools/voice_pipeline_test.py`, `tools/whisper_benchmark.py` | reproducible voice tests |
| `docs/*` | this documentation |
