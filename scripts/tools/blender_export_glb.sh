#!/usr/bin/env bash
# blender_export_glb.sh — Neo 白名单：Blender 导出 GLB 样例。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$ROOT/capabilities/local/blender_export_glb.py"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
OUT_DIR="${NEO_BLENDER_OUT:-$ROOT/.neo/blender_export_glb}"

if [[ ! -x "$BLENDER" ]]; then
  if command -v blender >/dev/null 2>&1; then
    BLENDER="$(command -v blender)"
  else
    echo "neo blender_export_glb: Blender not found (set BLENDER=...)" >&2
    exit 1
  fi
fi

if [[ ! -t 0 ]] && command -v python3 >/dev/null 2>&1; then
  parsed="$(python3 -c '
import json,sys
raw=sys.stdin.read().strip()
if not raw:
  print(""); raise SystemExit(0)
try:
  o=json.loads(raw)
except Exception:
  print(""); raise SystemExit(0)
print(o.get("out") or "")
' || true)"
  if [[ -n "${parsed}" ]]; then
    OUT_DIR="$parsed"
  fi
fi

mkdir -p "$OUT_DIR"
echo "neo blender_export_glb: blender=$BLENDER out=$OUT_DIR"
exec "$BLENDER" --background --factory-startup --python "$SCRIPT" -- --out "$OUT_DIR"
