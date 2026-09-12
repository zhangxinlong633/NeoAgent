# Neo Agent

Neo Agent is a portable, goal-oriented agent runtime. It completes bounded work—status checks, material organization, SOP-style ops, repo-side execution—inside explicit policy: goals land on an allow-listed capability set and run by graph. Not a chat toy. Not the strongest coding IDE. Its core is orchestration you can trust: goals stay composable, capabilities stay governed, boundaries stay enforced, memory stays on-host. The same contract spans a single-machine CLI through industrial edge, embodied platforms, and plant linkage—cloud intelligence wired into real systems. What ships today is the lightweight on-host runtime.

<p align="center">
  <img src="docs/neo-architecture-en.webp" alt="Neo Agent closed loop: LLM, DAG, Capability Matrix, Memory" width="920" />
</p>

What each piece does in the loop:

1. **LLM** — decision brain: understand the goal, plan steps, generate or select a DAG.
2. **DAG** — scheduler: advance by agreed topology; paths stay reviewable.
3. **Capability Matrix** — arsenal: allow-listed tools (builtins / commands / MCP / directory packs).
4. **Policy** — guardrails: defaults-deny, allow-lists, and resource limits.
5. **Memory** — autonomous retention: on-host store and recall that feed later decisions.

The execution spine is **DAG ∥ Capability Matrix ∥ Policy**; LLM and Memory close the loop on decision and cross-session retention. Deployment stays light: change config per environment.

---

## Features

1. Single-turn chat — `./neo "…"` (OpenAI-compatible API)
2. Plan / plan+run — `neo plan` / `neo run "goal"`
3. Named DAG — `neo dag run NAME` (`dags/` catalog)
4. Declarative DAG — `tool` / `llm` / `loop` / `route`
5. Capability Matrix — builtins, allow-listed commands, packs under `capabilities/`
6. MCP (stdio) — `mcp_servers` → `mcp_<server>_<tool>`
7. Policy — shell / HTTPS allow-list / path sandbox / turn and byte limits
8. Named sessions — `-S ID` on disk; `--session-list` / `--session-clear`
9. Multi-session mix — `-S a,b` loads in order; writes the turn to the **first** ID only
10. Daemon multi-turn — `neo daemon` / `--socket`
11. On-host vector memory — recall, heuristic auto-store, `memory_add`, `memory store|recall`
12. Terminal Markdown — `-R` (vendored md4c)
13. Profiles / diagnostics — `-p` / `-m` / `-v` / `-d`
14. Claw prompt blocks — soul / bootstrap / rules / memory ([`docs/claw.md`](docs/claw.md))
15. JSON5 config — top-level `capability_matrix`

---

## Install

macOS or Linux, plus a reachable LLM API.

```bash
make
cp config/config.json5.example config/config.json5
# Set api_key and model; enable memory.vector if you want autonomous memory
./neo "Who are you?"
```

---

## Usage

```bash
./neo "In three sentences, what is Neo Agent good for?"
./neo -R "Same, with terminal Markdown"

./neo -S ship -R "How do I build a spaceship?"
./neo -S ship -R "Option 4 in detail"
./neo -S ship,cook -R "Combine both threads"
./neo --session-list
./neo --session-clear ship

./neo dag run show_time
./neo run "show the system time"
./neo plan "summarize recent work in this repo"

./neo memory store "User prefers dark mode"
./neo memory recall "dark mode"
./neo -v "Please remember I prefer large fonts"
```

Common entry points:

1. Chat — `./neo "…"` / `./neo -R "…"`
2. Sessions — `./neo -S id` / `-S a,b`
3. DAG — `./neo dag run <name>` / `./neo run <name|"goal">` / `./neo plan "goal"`
4. Multi-turn — `./neo daemon` / `--socket PATH`
5. Memory — `./neo memory store|recall "…"`
6. Profile — `./neo -p demo …`

More examples: [`docs/examples.md`](docs/examples.md). Matrix and DAG: [`docs/tool.md`](docs/tool.md), [`docs/dag.md`](docs/dag.md).

---

## Memory

On-host vector memory; no external embedding service. With `memory.vector` enabled, remember/prefer-style intents can auto-store; the model may also call `memory_add`.

```json5
memory: {
  path: "MEMORY.md",
  max_chars: 4000,
  vector: {
    enabled: true,
    store: ".neo/memory.vdb",
    top_k: 5,
    dims: 64,
    auto_store: { enabled: true, max_chars: 500 },
  },
}
```

1. Recall injects `## Memory` (no full `MEMORY.md` fallback)
2. Heuristic `auto_store` (`-v` logs)
3. Data under `vector.store` (default `.neo/`); in-process embeddings

Details: [`docs/claw.md`](docs/claw.md).

---

## Build

```bash
make
make test
```

Contributing: [`AGENTS.md`](AGENTS.md). Architecture: [`docs/architecture.md`](docs/architecture.md).

中文：[`README_zh.md`](README_zh.md)
