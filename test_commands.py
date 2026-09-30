"""
Quick checks for the command parser (no Unreal or robot needed).

  python test_commands.py                      # rule parser only (instant, no AI)
  python test_commands.py --llm                # also phrases that need the Ollama LLM
  python test_commands.py --say "go forward two meters"
  python test_commands.py --server http://130.39.94.33:8002 --say "turn left"
"""

import argparse
import json
import sys

import requests

import robot_commands


# (phrase, expected command, expected duration_s, distance_m, angle_deg, speed)
RULE_CASES = [
    ("stop", "stop", 0, 0, 0, 0),
    ("Stop!", "stop", 0, 0, 0, 0),
    ("please stop the robot", "stop", 0, 0, 0, 0),
    ("Whoa, whoa.", "stop", 0, 0, 0, 0),
    ("hold on", "stop", 0, 0, 0, 0),
    ("No!", "stop", 0, 0, 0, 0),
    ("move forward and then stop", "stop", 0, 0, 0, 0),
    ("So...", "stop", 0, 0, 0, 0),      # Whisper mishearing a short "Stop!"
    ("Sup?", "stop", 0, 0, 0, 0),
    ("up.", "stop", 0, 0, 0, 0),
    ("Soap", "stop", 0, 0, 0, 0),
    ("So, up.", "stop", 0, 0, 0, 0),    # real Whisper output for "Stop!" -- must never move
    ("Robot, sup?", "stop", 0, 0, 0, 0),
    ("Chop it!", "stop", 0, 0, 0, 0),
    ("job", "stop", 0, 0, 0, 0),
    ("Please", "stop", 0, 0, 0, 0),     # Whisper's "Freeze!"
    ("Salt", "stop", 0, 0, 0, 0),       # Whisper's "Halt!"
    ("Robot Stump", "stop", 0, 0, 0, 0),
    ("Go!", None, 0, 0, 0, 0),          # Whisper's "Stop!" once -- no direction, never move
    ("Let's go now.", None, 0, 0, 0, 0),
    ("so go forward", "forward", 0, 0, 0, 1),
    ("Go back word for two seconds.", "backward", 2, 0, 0, 1),   # real Whisper output
    ("Move forward.", "forward", 0, 0, 0, 1),
    ("go straight", "forward", 0, 0, 0, 1),
    ("All right, go forward.", "forward", 0, 0, 0, 1),
    ("move forward for 2 seconds", "forward", 2, 0, 0, 1),
    ("drive ahead three seconds", "forward", 3, 0, 0, 1),
    ("move forward 1 meter", "forward", 0, 1, 0, 1),
    ("go forward 50 cm", "forward", 0, 0.5, 0, 1),
    ("move forward half a meter", "forward", 0, 0.5, 0, 1),
    ("move forward 1.5 meters", "forward", 0, 1.5, 0, 1),
    ("back up", "backward", 0, 0, 0, 1),
    ("Move backward slowly.", "backward", 0, 0, 0, 0.5),
    ("reverse for a second", "backward", 1, 0, 0, 1),
    ("go back a little", "backward", 1, 0, 0, 1),
    ("turn left", "turn_left", 0, 0, 0, 1),
    ("Turn right 90 degrees.", "turn_right", 0, 0, 90, 1),
    ("rotate 45 degrees to the left", "turn_left", 0, 0, 45, 1),
    ("turn right ninety degrees", "turn_right", 0, 0, 90, 1),
    ("turn around", "turn_left", 0, 0, 180, 1),
    ("spin clockwise", "turn_right", 0, 0, 0, 1),
    ("counterclockwise", "turn_left", 0, 0, 0, 1),
    ("go ahead and turn left", "turn_left", 0, 0, 0, 1),
    ("turn left a little", "turn_left", 1, 0, 0, 1),
    ("strafe left", "strafe_left", 0, 0, 0, 1),
    ("move right", "strafe_right", 0, 0, 0, 1),
    ("slide to the left for 2 seconds", "strafe_left", 2, 0, 0, 1),
    ("go right 30 centimeters", "strafe_right", 0, 0.3, 0, 1),
]

# The rules must NOT guess these; they go to the LLM.
LLM_ONLY_CASES = [
    "go up",            # "up" is never a direction in the rules (too close to a misheard "stop")
    "go forward then turn left",
    "move forward and turn left",
    "don't go forward",
    "what can you do?",
    "dance for me",
]

# (phrase, allowed commands) for the LLM
LLM_CASES = [
    ("scoot up a tiny bit", {"forward", "unknown"}),
    ("so up top", {"stop", "unknown"}),
    ("could you kindly back off from me", {"backward"}),
    ("go forward then turn left", {"forward"}),
    ("don't go forward", {"stop", "unknown"}),
    ("what's the weather like today?", {"unknown"}),
    ("rotate so you're facing the opposite direction", {"turn_left", "turn_right"}),
    ("move sideways to your left for three seconds", {"strafe_left"}),
    ("scoot over a tad", {"forward", "unknown"}),   # no side given -> must not strafe
    ("wiggle a little", {"unknown", "forward", "backward"}),
]


def close(a: float, b: float) -> bool:
    return abs(a - b) < 1e-6


def run_rule_tests() -> int:
    failures = 0

    for phrase, command, duration, distance, angle, speed in RULE_CASES:
        result = robot_commands.parse_with_rules(phrase)
        got = None if result is None else result["command"]
        ok = result is not None and got == command

        if ok and command != "stop":
            magnitude = max(abs(result["forward"]), abs(result["strafe"]), abs(result["turn"]))
            ok = (
                close(result["duration_s"], duration)
                and close(result["distance_m"], distance)
                and close(result["angle_deg"], angle)
                and close(magnitude, speed)
            )

        failures += not ok
        detail = "" if ok else f"   <-- got {json.dumps(result)}"
        print(f"{'PASS' if ok else 'FAIL'}  {phrase!r:42} -> {got}{detail}")

    for phrase in LLM_ONLY_CASES:
        result = robot_commands.parse_with_rules(phrase)
        ok = result is None
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {phrase!r:42} -> (sent to LLM){'' if ok else '  <-- rules guessed ' + result['command']}")

    return failures


def run_llm_tests() -> int:
    failures = 0

    for phrase, allowed in LLM_CASES:
        result = robot_commands.parse_command(phrase)
        ok = result["command"] in allowed or (result["command"] is None and "unknown" in allowed)
        failures += not ok
        print(
            f"{'PASS' if ok else 'FAIL'}  {phrase!r:52} -> {result['action']:7} {result['command']} "
            f"[{result['source']}] \"{result['spoken_response']}\""
        )

    return failures


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--llm", action="store_true", help="also test the Ollama LLM fallback")
    parser.add_argument("--say", help="parse one phrase and print the full result")
    parser.add_argument("--server", help="send --say to a running bridge instead of parsing locally")
    args = parser.parse_args()

    if args.say:
        if args.server:
            response = requests.post(f"{args.server.rstrip('/')}/command_text", json={"text": args.say}, timeout=60)
            print(json.dumps(response.json(), indent=2))
        else:
            print(json.dumps(robot_commands.parse_command(args.say), indent=2))
        return

    failures = run_rule_tests()

    if args.llm:
        robot_commands.warm_up_llm()
        failures += run_llm_tests()

    print(f"\n{'ALL PASSED' if failures == 0 else f'{failures} FAILED'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
