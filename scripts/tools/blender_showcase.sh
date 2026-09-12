#!/usr/bin/env bash
# blender_showcase.sh — Neo 白名单入口：后台启动本机 Blender 渲染一帧展示图。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$ROOT/capabilities/local/blender_showcase.py"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
OUT_DIR="${NEO_BLENDER_OUT:-$ROOT/.neo/blender_showcase}"

if [[ ! -x "$BLENDER" ]]; then
  # 常见 Linux 路径回退
  if command -v blender >/dev/null 2>&1; then
    BLENDER="$(command -v blender)"
  else
    echo "neo blender_showcase: Blender not found (set BLENDER=...)" >&2
    exit 1
  fi
fi

mkdir -p "$OUT_DIR"
# 可选：从 stdin JSON 读 out（Neo pass_args=stdin_json）
if [[ -n "${NEO_TOOL_ARGS:-}" ]]; then
  :
fi
if [[ ! -t 0 ]] && command -v python3 >/dev/null 2>&1; then
  parsed="$(python3 -c '
import json,sys
raw=sys.stdin.read().strip()
if not raw:
  print("")
  raise SystemExit(0)
try:
  o=json.loads(raw)
except Exception:
  print("")
  raise SystemExit(0)
print(o.get("out") or "")
' || true)"
  if [[ -n "${parsed}" ]]; then
    OUT_DIR="$parsed"
    mkdir -p "$OUT_DIR"
  fi
fi

echo "neo blender_showcase: blender=$BLENDER out=$OUT_DIR"
exec "$BLENDER" --background --factory-startup --python "$SCRIPT" -- --out "$OUT_DIR"
