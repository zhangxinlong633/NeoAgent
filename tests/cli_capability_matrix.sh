#!/usr/bin/env bash
# CLI-level Capability Matrix checks: drive ./neo as a user would.
# Run from repo root: ./tests/cli_capability_matrix.sh  (or make test-cli / make test)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CFG="tests/fixtures/cli_capability_matrix.json5"
NEO="${NEO:-./neo}"
PASS=0
FAIL=0

ok() {
  PASS=$((PASS + 1))
  printf '  ok %s\n' "$1"
}

bad() {
  FAIL=$((FAIL + 1))
  printf '  FAIL %s\n' "$1" >&2
}

need_neo() {
  if [[ ! -x "$NEO" ]]; then
    make neo
  fi
  if [[ ! -x "$NEO" ]]; then
    echo "neo binary missing" >&2
    exit 1
  fi
}

run_capture() {
  # Sets OUT, ERR, RC from running "$@".
  local _out _err
  _out="$(mktemp)"
  _err="$(mktemp)"
  set +e
  "$@" >"$_out" 2>"$_err"
  local _rc=$?
  set -e
  OUT="$(cat "$_out")"
  ERR="$(cat "$_err")"
  RC=$_rc
  rm -f "$_out" "$_err"
  return 0
}

need_neo

echo "== CLI capability matrix =="

# --- help mentions dag run ---
run_capture "$NEO" -h
if [[ "$RC" -eq 0 ]] && grep -q 'dag run' <<<"$ERR$OUT"; then
  ok "help lists dag run"
else
  bad "help lists dag run (rc=$RC)"
fi

# --- legacy tools key warns via CLI load ---
run_capture "$NEO" -c tests/fixtures/tools_legacy_key.json5 dag run no_such_wf
if [[ "$RC" -ne 0 ]] && grep -q "deprecated" <<<"$ERR" && grep -q "capability_matrix" <<<"$ERR"; then
  ok "legacy tools key deprecation on CLI"
else
  bad "legacy tools key deprecation (rc=$RC err=$ERR)"
fi

# --- matrix command via dag run ---
rm -f tests/fixtures/count.out
run_capture "$NEO" -c "$CFG" -v dag run cli_count
if [[ "$RC" -eq 0 ]] && grep -q 'neo tool: count_run' <<<"$ERR" && [[ -f tests/fixtures/count.out ]]; then
  ok "dag run cli_count (command row)"
else
  bad "dag run cli_count (rc=$RC err=$ERR)"
fi

# unified neo run: exact DAG name → direct run (no planner)
run_capture "$NEO" -c "$CFG" run cli_count
if [[ "$RC" -eq 0 ]]; then
  ok "run cli_count (name hit → DAG)"
else
  bad "run cli_count (rc=$RC err=$ERR)"
fi

# workflow CLI removed
run_capture "$NEO" -c "$CFG" workflow run cli_count
if [[ "$RC" -ne 0 ]] && grep -qi 'removed' <<<"$ERR"; then
  ok "workflow CLI removed"
else
  bad "workflow CLI removed (rc=$RC err=$ERR)"
fi

# --- MCP capability via dag ---
run_capture "$NEO" -c "$CFG" dag run cli_mcp
if [[ "$RC" -eq 0 ]] && grep -q 'cli-mcp-hi' <<<"$OUT"; then
  ok "dag run cli_mcp (mcp row)"
else
  bad "dag run cli_mcp (rc=$RC out=$OUT err=$ERR)"
fi

# --- run_command policy row ---
run_capture "$NEO" -c "$CFG" dag run cli_run_command
if [[ "$RC" -eq 0 ]] && grep -q 'cli-shell-hi' <<<"$OUT"; then
  ok "dag run cli_run_command (shell_enabled)"
else
  bad "dag run cli_run_command (rc=$RC out=$OUT err=$ERR)"
fi

# --- unknown tool name fails ---
run_capture "$NEO" -c "$CFG" dag run cli_bad_tool
if [[ "$RC" -ne 0 ]]; then
  ok "dag run cli_bad_tool fails"
else
  bad "cli_bad_tool should fail (out=$OUT)"
fi

# --- mkdir / append_file / stat via CLI ---
rm -rf tests/fixtures/_cli_fs_extra
run_capture "$NEO" -c "$CFG" dag run cli_fs_extras
if [[ "$RC" -eq 0 ]] && grep -q 'type: file' <<<"$OUT" && [[ -f tests/fixtures/_cli_fs_extra/x.txt ]] &&
  grep -q 'cli-fs' tests/fixtures/_cli_fs_extra/x.txt; then
  ok "dag run cli_fs_extras (mkdir/append/stat)"
else
  bad "cli_fs_extras (rc=$RC out=$OUT err=$ERR)"
fi
rm -rf tests/fixtures/_cli_fs_extra

# --- capability directory load + propose ---
rm -f tests/fixtures/cap_pack/proposed/cli_prop.json5
run_capture "$NEO" -c tests/fixtures/tools_cap_dir.json5 dag run cli_dir_echo
if [[ "$RC" -eq 0 ]]; then
  ok "dag run cli_dir_echo (directory pack)"
else
  bad "cli_dir_echo (rc=$RC out=$OUT err=$ERR)"
fi
run_capture "$NEO" -c tests/fixtures/tools_cap_dir.json5 dag run cli_propose
if [[ "$RC" -eq 0 ]] && grep -q 'proposed:' <<<"$OUT" && [[ -f tests/fixtures/cap_pack/proposed/cli_prop.json5 ]]; then
  ok "dag run cli_propose (draft only)"
else
  bad "cli_propose (rc=$RC out=$OUT err=$ERR)"
fi
rm -f tests/fixtures/cap_pack/proposed/cli_prop.json5

# --- DAG directory ---
rm -f tests/fixtures/count.out
run_capture "$NEO" -c tests/fixtures/tools_dag_dir.json5 dag run dir_count
if [[ "$RC" -eq 0 ]] && [[ -f tests/fixtures/count.out ]]; then
  ok "dag run dir_count (dag_directory)"
else
  bad "dir_count (rc=$RC out=$OUT err=$ERR)"
fi

# --- matrix disabled: no tool rows / workflow using command should fail or error ---
run_capture "$NEO" -c tests/fixtures/tools_matrix_off.json5 dag run demo_loop
if [[ "$RC" -ne 0 ]]; then
  ok "matrix off rejects unknown/missing workflow"
else
  bad "matrix off unexpected success"
fi

# --- help mentions memory recall ---
run_capture "$NEO" -h
if [[ "$RC" -eq 0 ]] && grep -q 'memory recall' <<<"$ERR$OUT" && grep -q 'memory store' <<<"$ERR$OUT"; then
  ok "help lists memory recall/store"
else
  bad "help lists memory recall/store (rc=$RC)"
fi

# --- memory store + recall (vector DB, no MEMORY.md, no LLM) ---
rm -f tests/fixtures/.tmp_cli_memory.vdb tests/fixtures/.tmp_cli_memory.vdb.txts
run_capture "$NEO" -c tests/fixtures/memory_vector.json5 memory store "User prefers dark mode in the editor"
if [[ "$RC" -eq 0 ]] && grep -q 'neo memory: store ok' <<<"$ERR"; then
  ok "memory store"
else
  bad "memory store (rc=$RC err=$ERR)"
fi
run_capture "$NEO" -c tests/fixtures/memory_vector.json5 memory recall "dark mode preference"
if [[ "$RC" -eq 0 ]] && grep -q 'neo memory: vector' <<<"$ERR" && grep -qi 'dark' <<<"$OUT"; then
  ok "memory recall after store"
else
  bad "memory recall after store (rc=$RC err=$ERR out=$OUT)"
fi
rm -f tests/fixtures/.tmp_cli_memory.vdb tests/fixtures/.tmp_cli_memory.vdb.txts

# --- profile path still works (demo_loop uses matrix commands) ---
if [[ -f config/profiles/demo/neo.json5 ]]; then
  rm -f config/profiles/demo/count.out tests/fixtures/count.out 2>/dev/null || true
  # profile chdir into profile dir; count.sh path is relative to profile
  run_capture "$NEO" -p demo -v dag run demo_loop
  if [[ "$RC" -eq 0 ]] && grep -q 'neo tool: count_run' <<<"$ERR"; then
    ok "profile -p demo dag run demo_loop"
  else
    bad "profile demo dag (rc=$RC err=$ERR)"
  fi
else
  ok "profile demo skipped (missing)"
fi

# --- daemon aliases + unknown options must not become chat text ---
run_capture "$NEO" -h
if [[ "$RC" -eq 0 ]] && grep -qE -- '-D|--daemon' <<<"$ERR$OUT"; then
  ok "help lists -D/--daemon"
else
  bad "help lists -D/--daemon (rc=$RC)"
fi

_out="$(mktemp)"; _err="$(mktemp)"
set +e
printf 'exit\n' | "$NEO" -c "$CFG" -D >"$_out" 2>"$_err"
_rc=$?
set -e
OUT="$(cat "$_out")"; ERR="$(cat "$_err")"; RC=$_rc
rm -f "$_out" "$_err"
if [[ "$RC" -eq 0 ]] && grep -q 'neo daemon: stdin mode' <<<"$ERR"; then
  ok "-D enters daemon stdin mode"
else
  bad "-D enters daemon (rc=$RC err=$ERR)"
fi

run_capture "$NEO" --not-a-real-flag
if [[ "$RC" -ne 0 ]] && grep -q "unknown option" <<<"$ERR"; then
  ok "unknown option rejected"
else
  bad "unknown option rejected (rc=$RC err=$ERR)"
fi

echo "== $PASS passed, $FAIL failed =="
[[ "$FAIL" -eq 0 ]]
