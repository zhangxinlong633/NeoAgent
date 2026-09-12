# schema_validate.py — JSONL 校验（stdlib，无外部 jsonschema 依赖）
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

DOMAINS = {
    "system",
    "workspace",
    "network",
    "memory",
    "finance",
    "medical",
    "legal",
    "general",
}
OUTCOMES = {"succeed", "refuse", "propose"}


def validate_item(obj: dict[str, Any], loc: str) -> None:
    if not isinstance(obj, dict):
        raise ValueError(f"{loc}: not an object")
    for k in ("id", "domain", "prompt", "expect"):
        if k not in obj:
            raise ValueError(f"{loc}: missing {k}")
    if not isinstance(obj["id"], str) or not obj["id"]:
        raise ValueError(f"{loc}: bad id")
    if obj["domain"] not in DOMAINS:
        raise ValueError(f"{loc}: bad domain")
    if not isinstance(obj["prompt"], str) or not obj["prompt"]:
        raise ValueError(f"{loc}: bad prompt")
    exp = obj["expect"]
    if not isinstance(exp, dict) or exp.get("outcome") not in OUTCOMES:
        raise ValueError(f"{loc}: bad expect.outcome")
    for list_key in ("tools", "forbid_tools", "must_include", "must_exclude", "tags"):
        container = exp if list_key != "tags" else obj
        if list_key == "tags":
            val = obj.get("tags")
        else:
            val = exp.get(list_key)
        if val is None:
            continue
        if not isinstance(val, list) or any(not isinstance(x, str) for x in val):
            raise ValueError(f"{loc}: bad {list_key}")
    if "notes" in obj and obj["notes"] is not None and not isinstance(obj["notes"], str):
        raise ValueError(f"{loc}: bad notes")


def load_jsonl_file(path: Path) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    text = path.read_text(encoding="utf-8")
    for i, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        loc = f"{path}:{i}"
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as e:
            raise ValueError(f"{loc}: JSON {e}") from e
        validate_item(obj, loc)
        items.append(obj)
    return items


def load_corpus(paths: Iterable[Path | str]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    for p in paths:
        path = Path(p)
        if path.is_dir():
            files = sorted(path.glob("*.jsonl"))
        else:
            files = [path]
        for f in files:
            for item in load_jsonl_file(f):
                iid = item["id"]
                if iid in seen_ids:
                    raise ValueError(f"duplicate id: {iid} in {f}")
                seen_ids.add(iid)
                out.append(item)
    return out
