#!/usr/bin/env python3
"""Cluster prompt-eval failures into capability drafts; gated auto-PR."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def is_dangerous(row: dict) -> bool:
    domain = row.get("domain") or ""
    tags = row.get("tags") or []
    if domain in ("medical", "legal"):
        return True
    if "sensitive" in tags and domain == "finance":
        # finance propose drafts may be OK; refuse failures stay report-only
        exp = (row.get("expect") or {}).get("outcome")
        if exp == "refuse":
            return True
    reason = (row.get("reason") or "") + (row.get("stdout") or "")
    if re.search(r"shell_enabled|任意主机|force push|清空整盘", reason):
        return True
    return False


def slugify(s: str) -> str:
    s = re.sub(r"[^a-zA-Z0-9_-]+", "_", s)
    return s.strip("_")[:40] or "gap"


def draft_capability(cluster_key: str, rows: list[dict], dest: Path) -> Path | None:
    if any(is_dangerous(r) for r in rows):
        return None
    domain = rows[0].get("domain") or "general"
    name = f"auto_{slugify(domain)}_{slugify(cluster_key)}"
    samples = ", ".join(str(r.get("id")) for r in rows[:5])
    body = {
        "name": name,
        "description": f"Auto-drafted from prompt eval failures ({cluster_key}). Review before enabling.",
        "when": [
            f"Prompt-eval gap cluster {cluster_key}",
            f"Failed ids: {samples}",
        ],
        "when_not": [
            "Do not enable until Policy review",
            "Not for medical diagnosis or legal advice",
        ],
        "tags": ["proposed", "auto", domain],
        "outcome": "Placeholder — replace argv with a concrete allow-listed command",
        "argv": ["./scripts/tools/echo-args.sh"],
        "timeout_sec": 10,
        "max_output_bytes": 4096,
        "pass_args": "env",
        "parameters": {"type": "object", "properties": {"note": {"type": "string"}}},
    }
    # JSON5-ish: dump JSON (valid JSON5)
    dest.mkdir(parents=True, exist_ok=True)
    path = dest / f"{name}.json5"
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path


def build_gap_clusters(failures: list[dict]) -> dict[str, list[dict]]:
    clusters: dict[str, list[dict]] = defaultdict(list)
    for row in failures:
        domain = row.get("domain") or "general"
        reason = row.get("reason") or "unknown"
        if "missing tools" in reason:
            kind = "missing_tools"
        elif "must_exclude" in reason:
            kind = "hallucination"
        else:
            kind = "other"
        clusters[f"{domain}:{kind}"].append(row)
    return clusters


def process_failures(
    failures: list[dict],
    run_id: str = "manual",
    open_pr: bool = False,
    proposed_dir: Path | None = None,
) -> list[Path]:
    clusters = build_gap_clusters(failures)
    dest = proposed_dir or (ROOT / "tests" / "prompts" / "gaps" / "drafts")
    written: list[Path] = []
    skipped_dangerous = 0
    for key, rows in clusters.items():
        if any(is_dangerous(r) for r in rows):
            skipped_dangerous += 1
            continue
        p = draft_capability(key, rows, dest)
        if p:
            written.append(p)
    print(f"gaps: wrote {len(written)} drafts, skipped_dangerous_clusters={skipped_dangerous}")

    if open_pr and written:
        if os.environ.get("NEO_PROMPTS_AUTOPR") != "1":
            print("gaps: NEO_PROMPTS_AUTOPR!=1; skip PR", file=sys.stderr)
            return written
        maybe_open_pr(written, run_id)
    return written


def maybe_open_pr(paths: list[Path], run_id: str) -> None:
    branch = f"prompts-gap/{run_id}"
    try:
        subprocess.run(["git", "checkout", "-b", branch], cwd=str(ROOT), check=False)
        subprocess.run(["git", "add"] + [str(p) for p in paths], cwd=str(ROOT), check=False)
        subprocess.run(
            ["git", "commit", "-m", f"chore: prompt-eval gap drafts {run_id}"],
            cwd=str(ROOT),
            check=False,
        )
        body = "Auto-drafted capability proposals from prompt eval.\n\n**Do not merge without review.**\n"
        subprocess.run(
            ["gh", "pr", "create", "--title", f"prompt-eval gaps {run_id}", "--body", body],
            cwd=str(ROOT),
            check=False,
        )
    except FileNotFoundError as e:
        print(f"gaps: pr helper failed: {e}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--failures", required=True, help="failures.jsonl path")
    ap.add_argument("--no-pr", action="store_true")
    ap.add_argument("--run-id", default="manual")
    args = ap.parse_args()
    path = Path(args.failures)
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    process_failures(rows, run_id=args.run_id, open_pr=not args.no_pr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
