# neo_invoke.py — 子进程调用 ./neo
from __future__ import annotations

import subprocess
from pathlib import Path


def invoke_neo(
    prompt: str,
    neo_bin: str = "./neo",
    render: bool = False,
    timeout: int = 180,
    cwd: str | None = None,
) -> tuple[int, str, str]:
    cmd = [neo_bin, "-v"]
    if render:
        cmd.append("-R")
    cmd.append(prompt)
    work = cwd or str(Path.cwd())
    p = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=work,
    )
    return p.returncode, p.stdout or "", p.stderr or ""
