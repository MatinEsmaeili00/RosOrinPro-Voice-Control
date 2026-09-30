# Resources

References used to build this project, grouped by topic. All links were checked on 2026-09-29.

## Robot and ROS 2

| Resource | Used for |
|---|---|
| [Hiwonder ROSOrin Pro](https://www.hiwonder.com/products/rosorin-pro) | the robot (ROS 2, NVIDIA Jetson or Raspberry Pi, mecanum wheels, Ackermann option) |
| [REP 103: Standard Units of Measure and Coordinate Conventions](https://www.ros.org/reps/rep-0103.html) | +x forward, +y left, +z counter-clockwise, used for `forward/strafe/turn` |
| [geometry_msgs/Twist](https://docs.ros2.org/latest/api/geometry_msgs/msg/Twist.html) | the `/cmd_vel` message |
| [nav_msgs/Odometry](https://docs.ros2.org/latest/api/nav_msgs/msg/Odometry.html) | the `/odom` message the pawn follows |
| [ROS 2 Humble documentation](https://docs.ros.org/en/humble/index.html) | general ROS 2 reference |
| [rosbridge_suite](https://github.com/RobotWebTools/rosbridge_suite) | WebSocket bridge on the robot (port 9090) |
| [rosbridge v2 protocol](https://github.com/RobotWebTools/rosbridge_suite/blob/ros2/ROSBRIDGE_PROTOCOL.md) | `publish` / `subscribe` JSON messages used by `CPP_RosBridgeComponent` |
| [roslibjs](https://github.com/RobotWebTools/roslibjs) | reference client for the rosbridge protocol |

## Speech recognition

| Resource | Used for |
|---|---|
| [faster-whisper](https://github.com/SYSTRAN/faster-whisper) | Whisper on CTranslate2: `WhisperModel("base", device="cpu", compute_type="int8")` |
| [OpenAI Whisper](https://github.com/openai/whisper) · [paper (Radford et al., 2022)](https://arxiv.org/abs/2212.04356) | the underlying speech model |
| [Silero VAD](https://github.com/snakers4/silero-vad) | voice activity detection behind `vad_filter=True` |
| [PyAV](https://github.com/PyAV-Org/PyAV) | audio decoding for faster-whisper (pinned to 17.1.0, see [server.md](server.md)) |

## Language model

| Resource | Used for |
|---|---|
| [Ollama](https://github.com/ollama/ollama) | local LLM server on the DGX Spark |
| [Ollama API reference](https://github.com/ollama/ollama/blob/main/docs/api.md) | `/api/chat` (`format`, `think`, `keep_alive`, `options.temperature`), empty-prompt `/api/generate` warm-up |
| [Ollama structured outputs](https://ollama.com/blog/structured-outputs) | forcing the model to answer in a JSON schema |
| [gemma4 on Ollama](https://ollama.com/library/gemma4) · [Google Gemma](https://ai.google.dev/gemma) | the model (`gemma4:e4b`, 8B, Q4_K_M) |

## Server

| Resource | Used for |
|---|---|
| [FastAPI](https://fastapi.tiangolo.com/) | HTTP API, file uploads, lifespan startup hook |
| [Uvicorn](https://github.com/encode/uvicorn) | ASGI server (`uvicorn server:app --host 0.0.0.0 --port 8002`) |
| [NVIDIA DGX Spark](https://www.nvidia.com/en-us/products/workstations/dgx-spark/) | the machine running the server and the LLM |

## Unreal Engine

| Resource | Used for |
|---|---|
| [FAudioCaptureSynth](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AudioCaptureCore/FAudioCaptureSynth) | microphone capture (`OpenDefaultStream`, `StartCapturing`, `GetAudioData`, `AbortCapturing`) |
| [FCaptureDeviceInfo](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AudioCaptureCore/FCaptureDeviceInfo) | the mic's channel count and sample rate |
| [UAudioCaptureComponent](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/AudioCapture/UAudioCaptureComponent) | the engine's own capture component, whose open/abort pattern this code copies |
| [Forum: AudioCapture start/stop asserting](https://forums.unrealengine.com/t/audiocapture-multiple-start-stop-asserting/2099815) | why `StopCapturing()` is avoided and the stream is aborted before destruction |
| [FHttpModule](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/HTTP/FHttpModule) | HTTP requests to the bridge |
| [FJsonSerializer](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Json/FJsonSerializer) | building and reading JSON |
| [IWebSocket](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/WebSockets/IWebSocket) | the rosbridge connection (existing code) |
| [Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) | keyboard and VR input actions |
| [Gameplay Timers](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-timers-in-unreal-engine) | re-publish, move duration, mic polling |

## Testing

| Resource | Used for |
|---|---|
| [Piper TTS](https://github.com/OHF-Voice/piper1-gpl) · [voices](https://huggingface.co/rhasspy/piper-voices) | synthetic speech for the voice tests (`en_US-lessac-medium`) |

## GitHub

| Resource | Used for |
|---|---|
| [Creating diagrams (Mermaid)](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams) | the diagrams in these docs |
