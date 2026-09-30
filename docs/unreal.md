# Unreal Engine integration

## Requirements

- Unreal Engine 5.1 or newer (Enhanced Input). The code uses stable UE 5 APIs.
- Modules: `WebSockets`, `Json`, `EnhancedInput`, `InputCore` (the project already had these), plus **`HTTP`,
  `AudioCapture`, `AudioCaptureCore`**.
- Plugin: **Audio Capture** (built in, disabled by default).
- A microphone set as the Windows default recording device. For VR, the headset mic.

## Installation

1. **Copy the files** from `unreal/Source/` into your project's source folder (for example `Source/<Project>/`):

   | File | Status |
   |---|---|
   | `CPP_AICommandComponent.h/.cpp` | **new** |
   | `CPP_RosOrinPawn.h/.cpp` | **modified**: adds the `AICommand` component |
   | `CPP_PlayerController.h/.cpp` | **modified**: push-to-talk, stop key, `AskAI` console command, manual override |
   | `CPP_RosBridgeComponent.h/.cpp` | **unchanged**, included so the repo is complete |

   Every change to an existing file is marked with an `// AI BRIDGE` comment.

2. **Build.cs**: add the new modules:

   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] {
       "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
       "WebSockets", "Json",
       "HTTP", "AudioCapture", "AudioCaptureCore"   // new
   });
   ```

3. **Enable the plugin**: *Edit → Plugins →* search **Audio Capture** → enable → restart the editor.

4. **Rebuild**: close the editor and build from Rider or Visual Studio. Live Coding can't add new classes, components
   or `UPROPERTY`s.

5. **Server address**: select the robot pawn → **AICommand** component → **AI Server URL**. The default is
   `http://130.39.94.33:8002`.

6. Press Play and look for `AI BRIDGE READY at http://...` in the Output Log. If you see
   `AI BRIDGE NOT REACHABLE`, see [troubleshooting.md](troubleshooting.md).

## Controls

| Input | Where it's set | Action |
|---|---|---|
| Hold **T**, release | `BP_PlayerController` → *Input \| AI* → **Push To Talk Key** | record, then send |
| **X** | **AI Stop Key** | emergency stop |
| VR button (optional) | **Talk Action** / **AI Stop Action** (Enhanced Input, bool) | same as T / X |
| `AskAI "..."` in the `~` console | `UFUNCTION(Exec)` on the controller | typed command |
| Robot movement keys | existing `RobotMoveXAction` / `RobotMoveYAction` | manual driving, cancels AI moves |

For VR buttons: create two `InputAction` assets (Value Type: Digital/bool), map them to controller buttons in
`RobotMappingContext`, and assign them on `BP_PlayerController`.

---

## Class walkthrough

### `UCPP_AICommandComponent` (new)

Lives on the robot pawn next to `RosBridge`. It records the mic, talks to the server, and executes the answer by
calling the RosBridge component on the same actor.

#### Properties

| Property | Default | Meaning |
|---|---|---|
| `AIServerURL` | `http://130.39.94.33:8002` | the bridge server |
| `RequestTimeout` | 30 s | HTTP timeout |
| `bShowOnScreenMessages` | true | "Listening…", "You: …", "AI: …" on the desktop window |
| `MaxMoveSeconds` | 20 s | a move with no time or distance stops after this. 0 = no limit (not recommended) |
| `bStopOnUnknownCommand` | true | if the AI didn't understand you **while the robot is moving**, stop it |
| `RepublishRateHz` | 10 | the active AI velocity is re-sent this often |
| `bInvertStrafe` / `bInvertTurn` | false | flip left/right if the real robot goes the wrong way |
| `MaxRecordSeconds` | 8 s | recording is sent automatically after this |
| `MinRecordSeconds` | 0.3 s | shorter clips (accidental taps) are ignored |
| `OnAIResponse` | event | `(Transcript, Action, SpokenResponse)` for every answer. Connect a widget or TTS to it. |

#### Blueprint functions

| Function | |
|---|---|
| `SendTextCommand(Text)` | send typed text, e.g. from a UMG text box |
| `StartVoiceCapture()` / `StopVoiceCaptureAndSend()` | push-to-talk |
| `EmergencyStop()` | stop now and drop any answers still on the way |
| `ManualOverride()` | cancel the AI move and drop pending answers, without sending a stop (the keyboard takes over) |
| `CancelAIMotion()` | stop re-publishing without sending a stop |
| `IsRecording()`, `IsExecutingAIMotion()` | state for UI |

#### How it works inside

**Voice capture.** It uses `Audio::FAudioCaptureSynth` from the AudioCaptureCore module. The stream is opened on every
key press and closed with `AbortCapturing()` on release, the same pattern Unreal's own `UAudioCaptureComponent` uses.
This avoids the asserts inside `StopCapturing()`, avoids a shutdown crash (the capture thread outliving the object),
and follows the Windows default mic if it changes. A 50 ms timer moves samples from the capture thread into a
buffer. The mic's real channel count and sample rate come from `GetDefaultCaptureDeviceInfo`. If that fails, it
refuses to record rather than send garbled audio.

**WAV building (`BuildWavFile`).** It mixes all channels down to mono and applies auto-gain (up to 8×, because headset
mics are often very quiet), then writes a standard 44-byte RIFF header plus 16-bit PCM at the mic's native rate.
Whisper resamples to 16 kHz itself. The WAV is sent as `multipart/form-data`, field `file`, to `/command_voice`.

**Request ordering.** Every request gets an increasing number. An answer is applied only if it is newer than the last
applied one, so a slow LLM "move" can never override a newer "stop". `EmergencyStop()`, `ManualOverride()` and
`EndPlay` mark every in-flight request as stale.

**Executing a command (`ApplyAIResult`).**
- `stop` → `RosBridge->StopRobot()`.
- `move`:
  1. Apply the invert flags.
  2. Clamp to ±1, the same as `SetRobotMovement`.
  3. If the value is below the RosBridge `DeadZone`, stop instead: the robot wouldn't move anyway.
  4. Turn `distance_m` or `angle_deg` into seconds using `LinearSpeed` / `TurnSpeed`, then apply `MaxMoveSeconds`.
  5. Start the motion.
- `unknown` → if the robot is moving and `bStopOnUnknownCommand` is set, stop it.

**Motion.** `StartAIMotion` calls `SetRobotMovement(F, S, T)` once, re-sends it every `1/RepublishRateHz` seconds
(robots with a `cmd_vel` watchdog would otherwise stop after a few hundred ms), and sets a timer that calls
`StopRobot()` when the time is up.

**EndPlay.** It closes the mic, drops pending answers, and **sends a stop to the robot**, so ending Play never leaves the
real robot driving.

### `ACPP_RosOrinPawn` (modified)

Adds one component:

```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Robot")
UCPP_AICommandComponent* AICommand;
// constructor:
AICommand = CreateDefaultSubobject<UCPP_AICommandComponent>(TEXT("AICommand"));
```

### `ACPP_PlayerController` (modified)

- New properties: `PushToTalkKey` (T), `AIStopKey` (X), optional `TalkAction` / `AIStopAction`.
- `SetupInputComponent` binds T pressed/released and X with `BindKey`, the same way the existing debug W key is bound,
  plus the optional Enhanced Input actions (`Started` / `Completed` / `Canceled`).
- `AskAI(const FString&)` is a `UFUNCTION(Exec, BlueprintCallable)`. Type `AskAI "move forward"` in the console.
- `UpdateRobotMovement` (keyboard driving) now calls `AICommand->ManualOverride()` first, so **manual input always
  wins**.
- If the AICommand component is missing, the stop key still calls `RosBridge->StopRobot()` directly.

### `UCPP_RosBridgeComponent` (unchanged, for reference)

- On `BeginPlay`, connects to `ws://<RobotIP>:9090` and subscribes to `/odom` (`nav_msgs/msg/Odometry`).
- `SetRobotMovement(Forward, Strafe, Turn)`: clamps each value to ±1, zeroes anything below `DeadZone` (0.10), scales
  by `LinearSpeed` (0.08 m/s) and `TurnSpeed` (0.30 rad/s), then publishes a `geometry_msgs/Twist` to `/cmd_vel` using
  the [rosbridge protocol](https://github.com/RobotWebTools/rosbridge_suite/blob/ros2/ROSBRIDGE_PROTOCOL.md):
  ```json
  {"op":"publish","topic":"/cmd_vel","msg":{"linear":{"x":0.08,"y":0,"z":0},"angular":{"x":0,"y":0,"z":0}}}
  ```
- `StopRobot()` publishes all zeros.
- `/odom` messages move the pawn: the first message is saved as the origin, then
  `NewLocation = Origin + (Δx × 100, −Δy × 100, 0)`. Position only, not rotation.

## Showing answers in VR

`AddOnScreenDebugMessage` only draws on the desktop window, not in the headset. To show answers in VR, bind
**OnAIResponse** on the AICommand component (in the pawn or level Blueprint) to a 3D widget's text. You can also feed
`SpokenResponse` to a text-to-speech system.
