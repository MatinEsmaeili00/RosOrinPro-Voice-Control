"""
Turns what the user said ("move forward for 2 seconds", "stop", "turn around")
into ONE robot command that Unreal can execute.

Order:
  1. Fast rules (instant, no AI). "stop" is ALWAYS handled here so it is never
     slowed down by the LLM.
  2. Ollama LLM for anything the rules don't understand ("scoot ahead a tiny bit").

Direction values use the ROS convention (REP 103), same as /cmd_vel:
  forward  +1 = drive forward        -> linear.x
  strafe   +1 = slide LEFT           -> linear.y
  turn     +1 = rotate LEFT (CCW)    -> angular.z
They are -1..1 multipliers. Unreal scales them with LinearSpeed / TurnSpeed
inside CPP_RosBridgeComponent::SetRobotMovement, so the real speed limits
stay in Unreal.
"""

import json
import os
import re
from typing import Optional

import requests


OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434")
LLM_MODEL = os.getenv("LLM_MODEL", "gemma4:e4b")
LLM_KEEP_ALIVE = os.getenv("LLM_KEEP_ALIVE", "1h")   # keep the model in GPU memory
LLM_TIMEOUT_S = float(os.getenv("LLM_TIMEOUT_S", "45"))

NORMAL_SPEED = 1.0
SLOW_SPEED = 0.5
MIN_SPEED = 0.2          # Unreal's DeadZone is 0.1, stay above it
MAX_DURATION_S = 120.0   # Unreal applies its own (smaller) MaxMoveSeconds on top
MAX_DISTANCE_M = 20.0
MAX_ANGLE_DEG = 1080.0
A_LITTLE_S = 1.0         # "move forward a little"

COMMANDS = {
    "forward":      (1.0, 0.0, 0.0),
    "backward":     (-1.0, 0.0, 0.0),
    "strafe_left":  (0.0, 1.0, 0.0),
    "strafe_right": (0.0, -1.0, 0.0),
    "turn_left":    (0.0, 0.0, 1.0),
    "turn_right":   (0.0, 0.0, -1.0),
}

TURN_COMMANDS = {"turn_left", "turn_right"}

DESCRIPTIONS = {
    "forward": "Moving forward",
    "backward": "Moving backward",
    "strafe_left": "Sliding left",
    "strafe_right": "Sliding right",
    "turn_left": "Turning left",
    "turn_right": "Turning right",
}

llm_ready = False


# ---------------------------------------------------------------------------
# Response helpers
# ---------------------------------------------------------------------------

def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def to_float(value, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def unknown_command(spoken_response: str, source: str) -> dict:
    return {
        "action": "unknown",
        "command": None,
        "forward": 0.0,
        "strafe": 0.0,
        "turn": 0.0,
        "duration_s": 0.0,
        "distance_m": 0.0,
        "angle_deg": 0.0,
        "spoken_response": spoken_response,
        "source": source,
    }


def describe(command: str, speed: float, duration_s: float, distance_m: float, angle_deg: float) -> str:
    text = DESCRIPTIONS[command]

    if speed < NORMAL_SPEED:
        text += " slowly"

    if angle_deg > 0:
        text += f" {angle_deg:g} degrees."
    elif distance_m > 0:
        unit = "meter" if distance_m == 1 else "meters"
        text += f" {distance_m:g} {unit}."
    elif duration_s > 0:
        unit = "second" if duration_s == 1 else "seconds"
        text += f" for {duration_s:g} {unit}."
    else:
        text += ". Say stop when you want me to stop."

    return text


def build_command(
    command: str,
    speed: float = NORMAL_SPEED,
    duration_s: float = 0.0,
    distance_m: float = 0.0,
    angle_deg: float = 0.0,
    spoken_response: Optional[str] = None,
    source: str = "rules",
) -> dict:
    if command == "stop":
        result = unknown_command(spoken_response or "Stopping.", source)
        result["action"] = "stop"
        result["command"] = "stop"
        return result

    if command not in COMMANDS:
        return unknown_command(spoken_response or "I did not understand that command.", source)

    speed = clamp(speed if speed > 0 else NORMAL_SPEED, MIN_SPEED, 1.0)
    duration_s = clamp(duration_s, 0.0, MAX_DURATION_S)

    # Distance only makes sense for driving, angles only for turning.
    if command in TURN_COMMANDS:
        distance_m = 0.0
        angle_deg = clamp(angle_deg, 0.0, MAX_ANGLE_DEG)
    else:
        distance_m = clamp(distance_m, 0.0, MAX_DISTANCE_M)
        angle_deg = 0.0

    duration_s, distance_m, angle_deg = round(duration_s, 3), round(distance_m, 3), round(angle_deg, 3)
    forward, strafe, turn = (value * speed for value in COMMANDS[command])

    return {
        "action": "move",
        "command": command,
        "forward": round(forward, 3),
        "strafe": round(strafe, 3),
        "turn": round(turn, 3),
        "duration_s": round(duration_s, 3),
        "distance_m": round(distance_m, 3),
        "angle_deg": round(angle_deg, 3),
        "spoken_response": spoken_response or describe(command, speed, duration_s, distance_m, angle_deg),
        "source": source,
    }


# ---------------------------------------------------------------------------
# 1) Fast rules
# ---------------------------------------------------------------------------

STOP_RE = re.compile(
    r"\b(?:stop\w*|halt|freeze|brake|break|whoa|woah|wait|pause|stay|enough|abort|cancel|"
    r"emergency|kill|quit|hold on|hold it|hold up|dont move|do not move)\b"
)
STOP_ONLY = {"no", "no no", "no no no", "nope", "not that"}

# What Whisper makes of a short, shouted "Stop!" / "Freeze!" (seen in testing: "So...",
# "Sup?", "up.", "Soap", "So, up.", "job", "Jump!", "Hope.", "Chop it!", "Please").
# Only counts when the WHOLE utterance is made of these, so "so go forward" is not
# affected. "up" is also never used as a direction for this reason.
STOP_MISHEARD = {
    "so", "sup", "soap", "up", "sub", "top", "stob", "stap", "stopp", "stomp", "shop", "stock", "stuff",
    "job", "jump", "hope", "chop", "please", "salt", "stump", "dope",
}
STOP_FRAGMENT_OK = STOP_MISHEARD | {"robot", "no", "oh", "hey", "it"}

# A bare "Go!" has no direction, and Whisper has heard a shouted "Stop!" as "Go!".
# Never guess here: answer unknown (which also stops a moving robot in Unreal).
NO_DIRECTION_ONLY = {"go", "go go", "move", "drive", "start", "start moving"}


def sounds_like_stop(text: str) -> bool:
    words = text.split()
    return 0 < len(words) <= 3 and all(word in STOP_FRAGMENT_OK for word in words) and any(
        word in STOP_MISHEARD for word in words
    )

# Words people/Whisper add that would otherwise confuse the rules
# ("All right, go forward" must not become "strafe right").
FILLER_RE = re.compile(
    r"\b(?:all right|alright|right now|right away|right there|thats right|okay|ok|please|hey|robot|"
    r"buddy|now|can you|could you|would you|will you|i want you to|i need you to|go ahead and|lets)\b"
)

# These need real understanding -> send to the LLM.
MULTI_STEP_RE = re.compile(r"\b(?:then|after that|afterwards|followed by|and next)\b")
NEGATION_RE = re.compile(r"\b(?:dont|do not|never|not|without)\b")

SLOW_RE = re.compile(r"\b(?:slow|slowly|slower|gently|carefully|careful|easy)\b")
A_LITTLE_RE = re.compile(r"\b(?:a little|a bit|a tiny bit|a little bit|slightly|a touch|an inch)\b")

TURN_VERBS = r"(?:turn|turning|rotate|rotating|spin|spinning|pivot|pivoting|face|facing|look|swing|veer|steer)"
SLIDE_VERBS = r"(?:strafe|strafing|slide|sliding|shift|shifting|sidestep|side step|crab|scoot)"
GAP = r"(?:\s+\w+){0,4}?\s+"   # "rotate 90 degrees to the left"

TURN_AROUND_RE = re.compile(rf"\b{TURN_VERBS}\s+(?:yourself\s+)?around\b|\babout face\b|\bu turn\b")
FULL_CIRCLE_RE = re.compile(r"\b(?:full (?:circle|turn|spin|rotation)|do a spin|three sixty|360)\b")

# Checked in order; each match is removed before the next pattern runs,
# so "turn left" is never also counted as "strafe left".
DIRECTION_PATTERNS = [
    ("turn_left",    re.compile(r"\b(?:counter|anti)\s?clockwise\b")),
    ("turn_right",   re.compile(r"\bclockwise\b")),
    ("turn_left",    re.compile(rf"\b{TURN_VERBS}{GAP}left\b")),
    ("turn_right",   re.compile(rf"\b{TURN_VERBS}{GAP}right\b")),
    ("turn_left",    re.compile(r"\bleft turn\b")),
    ("turn_right",   re.compile(r"\bright turn\b")),
    ("strafe_left",  re.compile(rf"\b{SLIDE_VERBS}{GAP}left\b")),
    ("strafe_right", re.compile(rf"\b{SLIDE_VERBS}{GAP}right\b")),
    ("strafe_left",  re.compile(r"\bleft\b")),
    ("strafe_right", re.compile(r"\bright\b")),
    ("backward",     re.compile(r"\b(?:backwards?|reverse|reversing|retreat|back up|backup|back)\b")),
    ("forward",      re.compile(r"\b(?:forwards?|ahead|straight|advance|onwards?|front)\b")),
]

# The LLM may only pick sideways / turning moves if the user said something that
# points that way. It must never invent a direction ("scoot up" -> strafe_right).
SIDEWAYS_HINT_RE = re.compile(r"\b(?:left|right|side|sideways|sidestep|strafe\w*|slide\w*|lateral\w*|crab)\b")
TURN_HINT_RE = re.compile(
    r"\b(?:left|right|turn\w*|rotat\w*|spin\w*|pivot\w*|around|clockwise|counterclockwise|anticlockwise|"
    r"opposite|face|facing|degrees?|u turn)\b"
)

NUMBER_WORDS = {
    "a": 1, "an": 1, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5,
    "six": 6, "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
    "twelve": 12, "fifteen": 15, "twenty": 20, "thirty": 30, "forty": 40,
    "forty five": 45, "sixty": 60, "ninety": 90, "one eighty": 180,
    "a hundred and eighty": 180, "one hundred eighty": 180, "three sixty": 360,
    "half": 0.5, "half a": 0.5, "half an": 0.5, "a half": 0.5,
    "one and a half": 1.5, "two and a half": 2.5,
    "a couple": 2, "a couple of": 2, "couple of": 2, "a few": 3,
}

NUMBER_RE = (
    r"(\d+(?:\.\d+)?|"
    + "|".join(re.escape(word) for word in sorted(NUMBER_WORDS, key=len, reverse=True))
    + r")"
)

DURATION_RE = re.compile(rf"\b{NUMBER_RE}\s*(seconds?|secs?|s|minutes?|mins?)\b")
DISTANCE_RE = re.compile(
    rf"\b{NUMBER_RE}\s*(meters?|metres?|m|centimeters?|centimetres?|cm|feet|foot|ft|inch|inches|yards?)\b"
)
ANGLE_RE = re.compile(rf"\b{NUMBER_RE}\s*(?:degrees?|degs?)\b")

METERS_PER_UNIT = {
    "c": 0.01,   # centimeter(s) / cm
    "f": 0.3048,  # feet / foot / ft
    "i": 0.0254,  # inch(es)
    "y": 0.9144,  # yard(s)
}


def normalize(text: str) -> str:
    text = text.lower().replace("°", " degrees ").replace("-", " ").replace("'", "").replace("’", "")
    text = re.sub(r"[^a-z0-9. ]", " ", text)
    text = re.sub(r"(?<!\d)\.|\.(?!\d)", " ", text)   # keep dots only inside numbers
    return re.sub(r"\s+", " ", text).strip()


def number_value(token: str) -> float:
    if token[0].isdigit():
        return float(token)
    return float(NUMBER_WORDS[token])


def extract_amounts(text: str) -> tuple[float, float, float]:
    duration_s = distance_m = angle_deg = 0.0

    match = ANGLE_RE.search(text)
    if match:
        angle_deg = number_value(match.group(1))

    match = DISTANCE_RE.search(text)
    if match:
        unit = match.group(2)
        distance_m = number_value(match.group(1)) * METERS_PER_UNIT.get(unit[0], 1.0)

    match = DURATION_RE.search(text)
    if match:
        duration_s = number_value(match.group(1))
        if match.group(2).startswith("m"):
            duration_s *= 60.0

    return duration_s, distance_m, angle_deg


def parse_with_rules(user_text: str) -> Optional[dict]:
    """Returns a command, or None if the LLM should handle it."""

    text = normalize(user_text)

    if not text:
        return None

    # Safety first: any stop word anywhere means stop.
    if STOP_RE.search(text) or text in STOP_ONLY or sounds_like_stop(text):
        return build_command("stop", spoken_response="Stopping.")

    text = re.sub(r"\s+", " ", FILLER_RE.sub(" ", text)).strip()

    if text in NO_DIRECTION_ONLY:
        return unknown_command("Which way? Say go forward, back, left, right, or turn.", "rules")

    if not text or MULTI_STEP_RE.search(text) or NEGATION_RE.search(text):
        return None

    speed = SLOW_SPEED if SLOW_RE.search(text) else NORMAL_SPEED
    duration_s, distance_m, angle_deg = extract_amounts(text)

    if A_LITTLE_RE.search(text) and not (duration_s or distance_m or angle_deg):
        duration_s = A_LITTLE_S

    if TURN_AROUND_RE.search(text) or FULL_CIRCLE_RE.search(text):
        command = "turn_right" if re.search(r"\b(?:right|clockwise)\b", text) and "counter" not in text else "turn_left"
        default_angle = 180.0 if TURN_AROUND_RE.search(text) else 360.0
        return build_command(command, speed, 0.0, 0.0, angle_deg or default_angle)

    found = []
    remaining = text

    for command, pattern in DIRECTION_PATTERNS:
        if pattern.search(remaining):
            remaining = pattern.sub(" ", remaining)
            if command not in found:
                found.append(command)

    if len(found) != 1:
        return None

    return build_command(found[0], speed, duration_s, distance_m, angle_deg)


# ---------------------------------------------------------------------------
# 2) Ollama LLM
# ---------------------------------------------------------------------------

LLM_SCHEMA = {
    "type": "object",
    "properties": {
        "command": {"type": "string", "enum": list(COMMANDS) + ["stop", "unknown"]},
        "speed": {"type": "number"},
        "duration_s": {"type": "number"},
        "distance_m": {"type": "number"},
        "angle_deg": {"type": "number"},
        "spoken_response": {"type": "string"},
    },
    "required": ["command", "speed", "duration_s", "distance_m", "angle_deg", "spoken_response"],
}

SYSTEM_PROMPT = """You control a small mobile robot with mecanum wheels. It can drive forward/backward,
slide sideways without turning, and rotate in place. Convert the user's instruction into ONE JSON command.

command:
- forward / backward: drive forward or backward
- strafe_left / strafe_right: slide sideways without turning
- turn_left / turn_right: rotate in place (counterclockwise = left, clockwise = right)
- stop: stop moving (also use this for "don't move", "no", or anything that sounds like the user wants it to stop)
- unknown: not a movement request, unclear, or impossible

fields:
- speed: 1.0 normally, 0.5 if the user says slowly / carefully / gently
- duration_s: seconds to move if the user said a time, otherwise 0
- distance_m: meters if the user said a distance (convert cm, feet, inches), otherwise 0
- angle_deg: degrees for turns if the user said an angle ("turn around" = 180), otherwise 0
- spoken_response: one short friendly sentence for the user

Rules: if several moves are requested, return only the FIRST one. "A little" / "a bit" means duration_s 1.
"Closer" and "ahead" mean forward. "Scoot" just means a small move, not sideways.
Only use strafe or turn when the user says left, right, sideways, turn, rotate, around or similar.
The text comes from speech recognition: short fragments like "so", "sup", "up", "top", "soap" are
probably a misheard "stop" -> use stop. When unsure whether to move, do NOT move.
Never invent numbers the user did not say. If the request is not about moving, use unknown and reply briefly."""


def extract_json(model_text: str) -> dict:
    try:
        return json.loads(model_text)
    except json.JSONDecodeError:
        pass

    match = re.search(r"\{.*\}", model_text, flags=re.DOTALL)

    if match:
        try:
            return json.loads(match.group(0))
        except json.JSONDecodeError:
            pass

    return {"command": "unknown", "spoken_response": "I could not understand that."}


def ollama_chat(payload: dict) -> requests.Response:
    response = requests.post(f"{OLLAMA_URL}/api/chat", json=payload, timeout=LLM_TIMEOUT_S)

    # Models without a thinking mode may reject the "think" flag.
    if response.status_code == 400 and "think" in response.text.lower():
        payload = {key: value for key, value in payload.items() if key != "think"}
        response = requests.post(f"{OLLAMA_URL}/api/chat", json=payload, timeout=LLM_TIMEOUT_S)

    response.raise_for_status()
    return response


def call_llm(user_text: str) -> dict:
    global llm_ready

    payload = {
        "model": LLM_MODEL,
        "stream": False,
        "think": False,
        "keep_alive": LLM_KEEP_ALIVE,
        "format": LLM_SCHEMA,
        "options": {"temperature": 0},
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_text},
        ],
    }

    model_text = ollama_chat(payload).json()["message"]["content"]
    llm_ready = True

    print("LLM raw response:", model_text)

    result = extract_json(model_text)
    command = str(result.get("command", "unknown")).strip().lower()
    spoken = str(result.get("spoken_response") or "").strip()

    if command == "stop":
        return build_command("stop", spoken_response=spoken or "Stopping.", source="llm")

    if command not in COMMANDS:
        return unknown_command(spoken or "Sorry, I did not understand that command.", "llm")

    heard = normalize(user_text)
    invented_sideways = command.startswith("strafe") and not SIDEWAYS_HINT_RE.search(heard)
    invented_turn = command in TURN_COMMANDS and not TURN_HINT_RE.search(heard)

    if invented_sideways or invented_turn:
        print(f"LLM picked {command} but the user never said which way -> ignored")
        return unknown_command(
            "I'm not sure which way to go. Say forward, back, left, right, or turn.",
            "llm",
        )

    # For real moves we write our own sentence so it always matches what the robot will do.
    return build_command(
        command,
        speed=to_float(result.get("speed"), NORMAL_SPEED),
        duration_s=max(0.0, to_float(result.get("duration_s"))),
        distance_m=max(0.0, to_float(result.get("distance_m"))),
        angle_deg=max(0.0, to_float(result.get("angle_deg"))),
        source="llm",
    )


def warm_up_llm() -> None:
    """Load the model into memory at startup so the first real command isn't slow."""

    global llm_ready

    try:
        print(f"Loading LLM {LLM_MODEL} ...")
        requests.post(
            f"{OLLAMA_URL}/api/generate",
            json={"model": LLM_MODEL, "prompt": "", "keep_alive": LLM_KEEP_ALIVE},
            timeout=180,
        ).raise_for_status()
        llm_ready = True
        print(f"LLM {LLM_MODEL} ready.")
    except requests.RequestException as error:
        print(f"WARNING: could not load LLM {LLM_MODEL} from {OLLAMA_URL}: {error!r}")
        print("Simple commands (forward/back/left/right/turn/stop) still work without it.")


# ---------------------------------------------------------------------------
# Shared pipeline (used by both /command_text and /command_voice)
# ---------------------------------------------------------------------------

def parse_command(user_text: str) -> dict:
    user_text = (user_text or "").strip()

    if not user_text:
        return {"transcript": "", **unknown_command("I did not hear a command.", "rules")}

    result = parse_with_rules(user_text)

    if result is None:
        try:
            result = call_llm(user_text)
        except (requests.RequestException, KeyError, ValueError) as error:
            print("LLM error:", repr(error))
            result = unknown_command(
                "My AI brain is not responding. Simple commands like move forward or stop still work.",
                "error",
            )

    return {"transcript": user_text, **result}
