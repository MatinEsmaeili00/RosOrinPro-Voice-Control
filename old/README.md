# RosOrinPro AI Bridge

Talk to the ROSOrin Pro robot from Unreal: *"move forward"* → robot drives forward, *"stop"* → robot stops.

This works the same way as `syngenta-ai-bridge`: Unreal sends what you said to the DGX Spark, the AI answers
with a small JSON command, and **Unreal carries it out**. Unreal already talks to the robot over rosbridge.

```
 You (mic / console)
      │  hold T and speak
      ▼
 Unreal  CPP_AICommandComponent ──HTTP──►  DGX Spark  server.py  (port 8002)
      ▲                                      │  Whisper: voice → text
      │                                      │  rules (instant) → Ollama gemma4:e4b (fallback)
      │◄──────── {"action":"move","forward":1.0,...} ┘
      ▼
 CPP_RosBridgeComponent::SetRobotMovement() ──rosbridge ws://130.39.95.23:9090──► /cmd_vel ──► robot
```

---

## 1. Start the server (DGX Spark)

```bash
cd ~/RosOrinPro_ai_bridge
./run.sh
```

Check it from the Unreal PC's browser: <http://130.39.94.33:8002/health> → `{"status":"bridge_ready", ...}`

Ollama must be running (it is a system service here). The server loads `gemma4:e4b` into GPU memory at
startup and keeps it loaded, so commands are answered in about 0.5 s.

**First-time setup on another machine:** `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`

---

## 2. Unreal setup

### a) Copy the files
Copy everything in `unreal/Source/` into your Unreal project's source folder (next to your existing
`CPP_RosBridgeComponent.h/.cpp`):

| File | What |
|---|---|
| `CPP_AICommandComponent.h/.cpp` | **new**: mic recording, HTTP to the server, drives the robot |
| `CPP_RosOrinPawn.h/.cpp` | **replace**: adds the `AICommand` component |
| `CPP_PlayerController.h/.cpp` | **replace**: push-to-talk key, stop key, `AskAI` console command |
| `CPP_RosBridgeComponent.h/.cpp` | **no changes** |

Every change to your existing files is marked with an `// AI BRIDGE` comment.

### b) Build.cs: add the modules
In `Source/<YourProject>/<YourProject>.Build.cs`, make sure these are in the list:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
    "WebSockets", "Json",                       // you already have these
    "HTTP", "AudioCapture", "AudioCaptureCore"  // NEW for the AI bridge
});
```

### c) Enable the plugin
**Edit → Plugins → search "Audio Capture" → enable → restart the editor.**

### d) Rebuild
Close the editor and build from Rider / Visual Studio. Live Coding can't add new classes or components.

### e) Check the IP
Select the robot pawn → **AICommand** component → **AI Server URL** = `http://130.39.94.33:8002`
(already the default).

---

## 3. Controls

| Input | Does |
|---|---|
| **Hold T**, speak, release | voice command |
| **X** | emergency stop (instant, no AI involved) |
| `~` console: `AskAI "turn left 90 degrees"` | typed command (great for testing without a mic) |
| WASD / your robot keys | manual driving; cancels any AI move (and AI answers still on the way) |

Keys can be changed on `BP_PlayerController` → *Input | AI*. For VR controllers, create two Input Actions
(bool), add them to `RobotMappingContext`, and assign them to **Talk Action** / **AI Stop Action**.

### Things you can say

| Say | Robot |
|---|---|
| "move forward", "go straight", "go ahead" | forward until you say stop |
| "move forward for 3 seconds" / "go forward 1 meter" / "50 cm" | forward for that time / distance |
| "back up", "reverse", "go back a little" | backward |
| "turn left", "turn right 90 degrees", "turn around", "spin clockwise" | rotate in place |
| "slide to the right", "move left", "strafe left" | slide sideways (mecanum). "slide" is recognized more reliably than "strafe" |
| "move backward slowly" | half speed |
| "stop", "halt", "wait", "hold on", "no" | stop |
| anything else ("scoot up a tiny bit", "don't go forward") | the LLM figures it out |

One move per sentence. For "go forward then turn left" it does the first part.

---

## 4. Safety

- **"stop" never waits for the AI.** Stop words are matched by simple rules before anything else, in about 1 ms after
  transcription. A stop word anywhere in a sentence means stop.
- **A misheard command stops the robot.** If the AI doesn't understand you while the robot is moving, it
  stops (`bStopOnUnknownCommand`). A lone "Stop!" is sometimes transcribed as "So" / "Sup" / "up". Those
  also count as stop. **Tip:** "stop stop" or "robot stop" is recognized much more reliably than one short "Stop!".
- **X key** stops immediately and throws away any AI answer still on its way.
- **Old answers can't restart the robot.** If a slow "move" answer arrives after a newer "stop", it is ignored.
- **Time limit:** a move with no time or distance stops by itself after `MaxMoveSeconds` (20 s).
- **The LLM can't invent a direction.** It may only strafe or turn if you actually said left, right, sideways,
  turn, etc.
- **Stopping the game stops the robot.** `EndPlay` sends a stop.
- Speeds stay in Unreal: the AI only sends -1..1 values; `LinearSpeed` / `TurnSpeed` on the RosBridge
  component set the real m/s and rad/s.

---

## 5. What the server sends back

`POST /command_text` with `{"text": "..."}`, or `POST /command_voice` (multipart WAV, field `file`):

```json
{
  "transcript": "move forward for 2 seconds",
  "action": "move",            // move | stop | unknown
  "command": "forward",        // forward backward strafe_left strafe_right turn_left turn_right stop
  "forward": 1.0,              // -1..1  (+ = forward)             -> linear.x
  "strafe": 0.0,               // -1..1  (+ = LEFT, ROS convention) -> linear.y
  "turn": 0.0,                 // -1..1  (+ = LEFT / CCW)           -> angular.z
  "duration_s": 2.0,           // 0 = until "stop"
  "distance_m": 0.0,           // Unreal converts to time with LinearSpeed
  "angle_deg": 0.0,            // Unreal converts to time with TurnSpeed
  "spoken_response": "Moving forward for 2 seconds.",
  "source": "rules",           // rules | llm | error
  "latency_ms": 3
}
```

If the real robot slides or turns the wrong way, tick **bInvertStrafe** / **bInvertTurn** on the AICommand
component. (Note: in your keyboard code `RobotMoveX` goes straight into `linear.y`, and +y is *left* in ROS.
If D slides the robot left, that's why.)

---

## 6. Testing without Unreal

```bash
source .venv/bin/activate
python test_commands.py                    # rules only, instant
python test_commands.py --llm              # + Ollama fallback phrases
python test_commands.py --say "go back half a meter"
python test_commands.py --server http://130.39.94.33:8002 --say "turn around"
curl -F file=@some_clip.wav http://130.39.94.33:8002/command_voice
```

## 7. Settings (environment variables for `./run.sh`)

| Variable | Default | |
|---|---|---|
| `PORT` | `8002` | syngenta bridge uses 8001, so both can run at once |
| `LLM_MODEL` | `gemma4:e4b` | any Ollama model (`ollama list`) |
| `OLLAMA_URL` | `http://localhost:11434` | |
| `WHISPER_MODEL` | `base` | `small.en` is ~3x slower and was no better on short commands in testing |
| `SAVE_LAST_AUDIO` | `1` | saves the last clip to `last_voice.wav` so you can listen to what Unreal sent |

Example: `LLM_MODEL=llama3.1:8b ./run.sh`

## 8. Troubleshooting

| Problem | Fix |
|---|---|
| `AI BRIDGE NOT REACHABLE` in Unreal | Is `./run.sh` running? Open `/health` in the PC's browser. Same network as the DGX (130.39.x.x)? |
| `MICROPHONE NOT AVAILABLE` | Enable the **Audio Capture** plugin; set the right mic as the Windows *default recording device* (for VR, the headset mic). |
| Transcript is empty or nonsense | Listen to `~/RosOrinPro_ai_bridge/last_voice.wav`. Check the `AI MIC: ... channels ... Hz` line in the Unreal log. |
| On-screen text not visible in VR | Debug messages only show on the desktop window. Bind **OnAIResponse** (AICommand component) to a 3D widget. |
| Voice upload says "trouble understanding" | Keep `av==17.1.0` (see requirements.txt). PyAV 18+ breaks faster-whisper 1.2.1. |
| Robot turns but the Unreal model doesn't rotate | Your odom code only copies position. Rotation from `/odom` orientation isn't applied yet. |
