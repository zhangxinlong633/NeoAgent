#!/usr/bin/env bash
# neo weather-wttr：按城市查当前天气（wttr.in），仅读 NEO_TOOL_ARGS.city，不拼 shell。
# 能力：capabilities/local/weather_wttr.json5
set -euo pipefail
exec python3 - <<'PY'
import json, os, re, subprocess, sys, urllib.parse

raw = os.environ.get("NEO_TOOL_ARGS") or "{}"
try:
    obj = json.loads(raw)
except json.JSONDecodeError:
    print("ERROR: bad NEO_TOOL_ARGS JSON", file=sys.stderr)
    sys.exit(2)

city = obj.get("city") if isinstance(obj, dict) else None
if not isinstance(city, str) or not city.strip():
    print("ERROR: city (string) required", file=sys.stderr)
    sys.exit(2)
city = city.strip()
if len(city) > 64:
    print("ERROR: city too long", file=sys.stderr)
    sys.exit(2)
# 允许字母数字、空白、连字符、点、常见中日韩统一表意文字
if not re.fullmatch(r"[0-9A-Za-z_\-\.\s\u3400-\u9fff]+", city):
    print("ERROR: invalid city characters", file=sys.stderr)
    sys.exit(2)

url = "https://wttr.in/" + urllib.parse.quote(city) + "?format=3"
curl = "/usr/bin/curl" if os.path.isfile("/usr/bin/curl") else "curl"
try:
    r = subprocess.run(
        [curl, "-fsSL", "--max-time", "15", url],
        capture_output=True,
        text=True,
        timeout=20,
    )
except FileNotFoundError:
    print("ERROR: curl not found", file=sys.stderr)
    sys.exit(2)
except subprocess.TimeoutExpired:
    print("ERROR: weather request timed out", file=sys.stderr)
    sys.exit(2)

if r.returncode != 0:
    err = (r.stderr or r.stdout or "").strip() or ("exit %d" % r.returncode)
    print("ERROR: weather fetch failed: " + err[:400], file=sys.stderr)
    sys.exit(1)

out = (r.stdout or "").strip()
if not out:
    print("ERROR: empty weather response", file=sys.stderr)
    sys.exit(1)
print(out)
PY
