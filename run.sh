#!/usr/bin/env bash
# Starts the RosOrinPro AI bridge on the DGX Spark (port 8002, reachable from the Unreal PC).
cd "$(dirname "$0")"
source .venv/bin/activate
exec uvicorn server:app --host 0.0.0.0 --port "${PORT:-8002}"
