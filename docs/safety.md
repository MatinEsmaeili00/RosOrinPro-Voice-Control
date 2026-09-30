# Safety

This drives a real robot from speech recognition and an LLM, and both of those make mistakes. The design rule
everywhere is: **when in doubt, stop. Never move in a direction the user didn't ask for.**

> This is not a certified safety system. Keep the robot's hardware power switch or e-stop within reach, and test
> with the wheels off the ground first ([first-run checklist](testing.md#first-run-checklist-on-the-real-robot)).

## Safety layers

| # | Layer | Where | What it prevents |
|---|---|---|---|
| 1 | **Stop is rule-based and checked first** | `robot_commands.py` `STOP_RE` | "stop" waiting on the LLM, or the LLM misreading it. A stop word *anywhere* in a sentence means stop ("move forward and then stop" → stop). |
| 2 | **Stop-like fragments = stop** | `STOP_MISHEARD`, `sounds_like_stop()` | A shouted "Stop!" that Whisper hears as "So", "Sup", "up", "So, up.", "Soap", "job", "Chop it!", "Salt"… |
| 3 | **"up" is never a direction** | `DIRECTION_PATTERNS` | "So, up." becoming *forward* (this happened, see the [development log](development-log.md#4-a-misheard-stop-that-would-have-driven-forward)). |
| 4 | **Bare "go" isn't guessed** | `NO_DIRECTION_ONLY` | Whisper once heard "Stop!" as "Go!". It returns unknown, never forward. |
| 5 | **Unknown while moving = stop** | Unreal `bStopOnUnknownCommand` | Any mishearing the lists don't cover. If the AI didn't understand you, it assumes you wanted it to stop. |
| 6 | **LLM direction guard** | `call_llm()` | The LLM inventing a sideways or turning move ("scoot over a tad" → strafe_right was rejected). |
| 7 | **LLM told to prefer not moving** | system prompt | "When unsure whether to move, do NOT move." Negations ("don't go forward") → stop. |
| 8 | **Values clamped, vector from a table** | `build_command()`, Unreal `ApplyAIResult` | Out-of-range speeds or durations. The model never sets the direction sign itself. |
| 9 | **Emergency stop key (X)** | `EmergencyStop()` | Needs no network, Whisper or LLM. Also discards every answer still on its way. |
| 10 | **Stale answers dropped** | request IDs in `HandleServerResponse` | A slow "move" answer arriving after a newer "stop" (the server handles requests in parallel, so this can happen). |
| 11 | **Manual driving wins** | `ManualOverride()` from `UpdateRobotMovement` | AI and keyboard fighting over `/cmd_vel`. Pending AI answers are dropped too. |
| 12 | **Time limit** | `MaxMoveSeconds` = 20 s | "move forward" running forever if nobody says stop. |
| 13 | **Stop on EndPlay** | `EndPlay()` | Stopping Play (or a crash in the level) leaving the real robot driving. |
| 14 | **Dead-zone check** | `ApplyAIResult` | A move too small for the robot to perform showing as "active" for 20 s. It stops instead. |
| 15 | **Speed limits stay in Unreal** | `LinearSpeed` 0.08 m/s, `TurnSpeed` 0.3 rad/s | The AI can only request -1…1. It can't ask for more than the configured maximum. |
| 16 | **Stop never waits** | server thread pool | Measured: "stop" answered in **3 ms** while a 1 s LLM request was running. |

## How reliable is a spoken "stop"?

Measured with 80 randomized synthetic clips (10 stop-type words × 8 voice variations, quiet headset-level audio,
background noise). Full method in [testing.md](testing.md).

| Said | stop | unknown (Unreal stops if moving) | move |
|---|---|---|---|
| Stop! | 6 | 2 | 0 |
| Stop. | 7 | 1 | 0 |
| Stop!! | 7 | 1 | 0 |
| Halt! | 8 | 0 | 0 |
| Wait! | 8 | 0 | 0 |
| No! | 8 | 0 | 0 |
| Stop it! | 7 | 1 | 0 |
| Robot, stop! | 8 | 0 | 0 |
| Stop stop! | 8 | 0 | 0 |
| Freeze! | 8 | 0 | 0 |
| **Total** | **75** | **5** | **0** |

- **0 of 80 clips produced a move.** 75 stopped directly. The other 5 were unknown, which also stops a moving robot.
- A single short "Stop!" is the hardest case: it is one 0.4 s syllable. Longer phrases were perfect.
  **Say "stop stop" or "robot stop".**
- The unknown cases (typically ~1.5 s, because they go through the LLM) are slower than a direct stop (~0.4 s). For
  instant, guaranteed stops, use the **X key**.

## Known limits

| Limit | Effect | Possible improvement |
|---|---|---|
| Distances and angles are open loop (time × speed) | "forward 1 meter" is only as accurate as the robot's speed tracking | close the loop with `/odom` (position is already received) |
| Voice latency ≈ 0.4 s plus network plus robot | the robot keeps moving briefly after "stop" | X key for urgent stops, keep speeds low |
| Whisper `base` can mishear | covered by layers 2–5 | a push-to-talk "stop" button, or a keyword spotter for "stop" |
| One command per sentence | "forward then left" runs only the first step | a command queue |
| The pawn doesn't rotate in Unreal | the digital twin shows position only | apply the `/odom` orientation quaternion |
| No obstacle awareness | the AI doesn't know what's around the robot | add lidar/depth checks on the robot side |
