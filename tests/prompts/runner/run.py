#!/usr/bin/env python3
"""Prompt domain eval runner — dry-run validate or live ./neo scoring."""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from neo_invoke import invoke_neo  # noqa: E402
from schema_validate import load_corpus  # noqa: E402
from score import score_case  # noqa: E402

SECRET_RE = re.compile(r"(api[_-]?key|authorization)\s*[:=]\s*\S+", re.I)


def redact(s: str) -> str:
    return SECRET_RE.sub(r"\1=***", s or "")


def main() -> int:
    ap = argparse.ArgumentParser(description="Neo prompts domain eval")
    ap.add_argument("--corpus", default="tests/prompts/corpus", help="JSONL file or directory")
    ap.add_argument("--limit", type=int, default=0, help="Max cases (0 = all)")
    ap.add_argument("--domain", default="", help="Filter domain")
    ap.add_argument("--dry-run", action="store_true", help="Validate only")
    ap.add_argument("--neo", default="./neo", help="Path to neo binary")
    ap.add_argument("--out", default="tests/prompts/reports", help="Report root")
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--judge", action="store_true", help="LLM judge for gray (optional module)")
    ap.add_argument("--gaps", action="store_true", help="Write gap drafts after run")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    corpus_path = Path(args.corpus)
    if not corpus_path.is_absolute():
        corpus_path = ROOT / corpus_path
    items = load_corpus([corpus_path])
    if args.domain:
        items = [x for x in items if x.get("domain") == args.domain]
    if args.limit and args.limit > 0:
        items = items[: args.limit]

    if args.dry_run:
        print(f"validated {len(items)} items")
        return 0

    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_dir = Path(args.out)
    if not out_dir.is_absolute():
        out_dir = ROOT / out_dir
    out_dir = out_dir / run_id
    out_dir.mkdir(parents=True, exist_ok=True)

    results = []
    counts = {"pass": 0, "fail": 0, "skip": 0, "gray": 0}

    judge_fn = None
    if args.judge:
        try:
            from judge import judge_case  # type: ignore

            judge_fn = judge_case
        except Exception as e:
            print(f"neo prompts: --judge unavailable: {e}", file=sys.stderr)
            return 2

    for item in items:
        iid = item["id"]
        try:
            rc, stdout, stderr = invoke_neo(
                item["prompt"],
                neo_bin=args.neo if Path(args.neo).is_absolute() else str(ROOT / args.neo),
                render=args.render,
                timeout=args.timeout,
                cwd=str(ROOT),
            )
        except Exception as e:
            row = {
                "id": iid,
                "status": "skip",
                "reason": f"invoke error: {e}",
                "tools_seen": [],
            }
            results.append(row)
            counts["skip"] += 1
            continue

        stdout_r, stderr_r = redact(stdout), redact(stderr)
        if rc != 0:
            blob = (stderr_r + stdout_r).lower()
            if "api_key" in blob or "llm request failed" in blob or "failed to load config" in blob:
                status_row = {
                    "id": iid,
                    "status": "skip",
                    "reason": f"neo exit {rc} (config/llm)",
                    "tools_seen": [],
                }
            else:
                status_row = {
                    "id": iid,
                    "status": "fail",
                    "reason": f"neo exit {rc}",
                    "tools_seen": [],
                    "stdout": stdout_r[:2000],
                    "stderr": stderr_r[:2000],
                }
            results.append(status_row)
            counts[status_row["status"]] += 1
            continue

        scored = score_case(item, stdout_r, stderr_r)
        if scored["status"] == "gray" and judge_fn:
            judged = judge_fn(item, stdout_r, stderr_r, scored)
            jst = judged.get("status")
            if jst == "pass":
                scored["status"] = "pass"
                scored["reason"] = "judge:" + judged.get("reason", "")
            elif jst == "fail":
                scored["status"] = "fail"
                scored["reason"] = "judge:" + judged.get("reason", "")
            else:
                scored["reason"] = "judge uncertain:" + judged.get("reason", scored.get("reason", ""))

        row = {
            "id": iid,
            "domain": item.get("domain"),
            "expect": item.get("expect"),
            "status": scored["status"],
            "reason": scored.get("reason"),
            "tools_seen": scored.get("tools_seen"),
            "stdout": stdout_r[:4000],
            "stderr": stderr_r[:4000],
        }
        results.append(row)
        counts[scored["status"]] = counts.get(scored["status"], 0) + 1
        time.sleep(0)  # yield

    summary = {
        "run_id": run_id,
        "total": len(results),
        "counts": counts,
        "corpus": str(corpus_path),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (out_dir / "results.jsonl").open("w", encoding="utf-8") as f:
        for r in results:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    failures = [r for r in results if r["status"] == "fail"]
    with (out_dir / "failures.jsonl").open("w", encoding="utf-8") as f:
        for r in failures:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

    print(json.dumps(summary, ensure_ascii=False))
    print(f"reports: {out_dir}")

    if args.gaps and failures:
        try:
            from gaps import process_failures

            process_failures(failures, run_id=run_id, open_pr=False)
        except Exception as e:
            print(f"neo prompts: gaps failed: {e}", file=sys.stderr)

    return 0 if counts.get("fail", 0) == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
