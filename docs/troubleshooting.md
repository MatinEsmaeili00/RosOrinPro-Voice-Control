# Troubleshooting

## Server

| Symptom | Cause / fix |
|---|---|
| `curl localhost:8002/health` fails | Server not running: `./run.sh`. Port already in use: `PORT=8003 ./run.sh` and update `AIServerURL`. |
| Works on the Spark but not from the Unreal PC | Wrong IP (`hostname -I` on the Spark), a different network, or a firewall. Open `http://<spark-ip>:8002/health` in the PC's browser. |
| `llm_loaded: false` in `/health` | Ollama not running (`systemctl status ollama`) or the model isn't pulled (`ollama list`, `ollama pull gemma4:e4b`). Simple commands still work through the rules. |
| Every voice command says "I had trouble understanding" and the log shows `unexpected keyword argument 'metadata_errors'` | PyAV too new: `.venv/bin/pip install av==17.1.0` |
| First LLM command takes ~10 s | Cold model load. The server warms up at start, so wait for `LLM gemma4:e4b ready.` in its log. |
| LLM answers are slow (> 3 s) | Another model is loaded on the GPU. Check with `ollama ps`. |

## Unreal build

| Symptom | Fix |
|---|---|
| `Cannot open include file: 'AudioCaptureCore.h'` | add `"AudioCaptureCore"` (and `"AudioCapture"`) to Build.cs |
| `HttpModule.h` not found / unresolved `FHttpModule` | add `"HTTP"` to Build.cs |
| New component doesn't appear on the pawn | Live Coding can't add components. Close the editor and build from the IDE. |
| Linker errors for `FAudioCaptureSynth` | enable the **Audio Capture** plugin, regenerate project files, rebuild |

## Unreal runtime

| Symptom | Cause / fix |
|---|---|
| `AI BRIDGE NOT REACHABLE` | see Server above, then check **AI Server URL** on the AICommand component |
| `MICROPHONE NOT AVAILABLE` | Audio Capture plugin not enabled, or no default recording device in Windows sound settings. For VR, set the headset mic as default. |
| "Hold the talk key while you speak" | The clip was shorter than `MinRecordSeconds` (0.3 s). Hold T for the whole sentence. |
| Transcript is empty or nonsense | On the Spark, listen to `last_voice.wav` in the repo folder. Check the `AI MIC: <device> \| <n> channels \| <rate> Hz` log line: the wrong device, or a silent or muted mic, are the usual causes. |
| AI answers but the robot doesn't move | Look for `Robot WebSocket is NOT connected!`: rosbridge isn't reachable at `RobotIP:9090`. Also check `AI: move is below the RosBridge DeadZone`. |
| Robot moves ~0.5 s then stops, even though no stop was said | Normally the 10 Hz re-publish prevents this. Make sure `RepublishRateHz` > 0. |
| Robot slides or turns the wrong way | tick `bInvertStrafe` / `bInvertTurn` (ROS: +y = left, +z = counter-clockwise) |
| Keyboard D slides the robot left | Existing keyboard code: `RobotMoveX` goes straight into `linear.y`, and +y is left in ROS. Invert the X axis in the Input Action or negate it in `UpdateRobotMovement`. |
| "move forward 2 meters" stops early | capped by `MaxMoveSeconds` (20 s = 1.6 m at 0.08 m/s). Raise it or `LinearSpeed`. |
| "move forward 1 meter" isn't exactly 1 m | open loop (time × speed). Accuracy depends on the robot's velocity tracking. |
| Robot turns but the Unreal model doesn't rotate | the existing odom code copies position only |
| Nothing on screen in VR | Debug messages only show on the desktop. Bind **OnAIResponse** to a 3D widget. |
| `T` or `X` does nothing | The possessed pawn may consume those keys first. Change **Push To Talk Key** / **AI Stop Key** on `BP_PlayerController`. |
| `AskAI` not found in the console | Exec functions only work on the *possessing* PlayerController, in non-Shipping builds. Use quotes: `AskAI "turn left"`. |
