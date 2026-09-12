# Neo Agent

**A portable, goal-oriented agent runtime.**

[中文版 README](README_zh.md)

Neo Agent accepts concrete goals (status checks, material organization, fixed operational flows) and executes them under explicit constraints. It is not positioned as an open-ended chatbot; it is built to **complete a defined class of work reliably**.

### Runtime architecture

Neo Agent runs as a closed-loop agent system: the **LLM is the decision brain** (understands the goal and generates or selects a DAG); the **DAG is the scheduler** (advances by graph, reviewable); the **Capability Matrix is the arsenal** (governed tools). Together they finish one job. **Memory** runs through the loop—auto-storing and deliberately storing valuable information, then recalling it to feed later decisions—until the business goal is met.

<p align="center">
  <img src="docs/neo-architecture-en.webp" alt="Neo Agent closed loop: LLM decision brain, DAG scheduler, Capability Matrix arsenal, Memory" width="920" />
</p>

| Dimension | Component | Business meaning |
|-----------|-----------|------------------|
| Decide | **LLM** | Decision brain: plan steps; generate or select a flow |
| Schedule | **DAG** | Scheduler: advance by agreed topology |
| Act | **Capability Matrix** | Arsenal: allow-listed tools and skills |
| Bound | **Policy** | Guardrails: denials and resource limits |
| Retain | **Memory** | Autonomous memory: auto-store + deliberate writes; recall feeds decisions |

The product triad **DAG ∥ Capability Matrix ∥ Policy** remains the execution spine; LLM and Memory supply decision and cross-session retention that close the loop. Deployment stays light: adjust configuration per environment.

Scenarios and positioning: [`docs/applications.md`](docs/applications.md). Commands: below and [`docs/examples.md`](docs/examples.md).

---

## Business extension layers

Neo Agent’s value is not “which model is smarter,” but whether a goal-oriented agent can **schedule stably, invoke from an allow-list, and control cost and data boundaries**. Layers below align with [`docs/applications.md`](docs/applications.md): deliver what works today first; treat later layers as direction and vision (not fully shipped).

### Delivered today: finish tasks, keep them reviewable

| Need | How Neo Agent addresses it |
|------|----------------------|
| Multi-step goals must complete with a clear path | Encode fetch → tidy → decide → notify/persist as a repeatable DAG |
| Internal skills must be usable by agents and governed | Register scripts, commands, and APIs in the matrix; tasks may call only listed capabilities |
| Reduce cost, latency, and exposure from “everything via the cloud” | Prefer local capabilities for simple steps; call the LLM for hard reasoning |
| **Retain preferences and facts across sessions (autonomous memory)** | Local vector memory: heuristic auto-store during chat, on-demand recall, and model-driven `memory_add`; data stays on-host by default |

Typical uses: repo/doc sidekick, scheduled goals, SOP-style checks, and lightweight assistants that must **remember user preferences across turns**.

### Direction: field and edge

When near-data, low-latency, weak-network loops become hard requirements, the same DAG + capability matrix can extend on-site (full form is on the architecture roadmap; today mostly reserved contracts).

| Direction | Picture | Neo Agent’s role |
|-----------|---------|------------|
| Industrial edge | Inspection, interlocking, anomaly handling close on the line | Schedule on industrial hosts; run registered capabilities on nodes |
| Embodied / mobile | Patrol, service robots, onboard orchestration | Task-level scheduling and capability governance (not hard real-time motion control) |
| Building / plant linkage | Sensor-triggered short-path actions | Regional graph scheduling; less “everything via the public cloud” |

### Vision: intelligent infrastructure

As the architecture matures, the product may grow from “an assistant” into “a layer of infrastructure” (**not a current feature list**): capability packs, private digital assistants on gateways, and governable cross-system capability distribution.

### Ecological niche

Cloud LLMs supply deep cognition; **Neo Agent is the scheduling and execution spine that plugs intelligence into real systems**—goals orchestrable, capabilities governable, boundaries enforceable, and memory retainable locally. Detail: [`docs/applications.md`](docs/applications.md) §§5–6.

---

## Audience

- Individuals or small teams that need **goal-oriented** agent work  
- Operations that want the same class of goals to run controllably and reviewably  
- Users who prefer light deploy: change configuration per environment  
- Scenarios that need **on-host autonomous memory** (cross-session preferences/facts without an external embedding service)  

If the goal is “the strongest coding IDE” or a heavyweight workflow middle platform, that is outside Neo Agent’s product direction.

---

## Capability overview

1. **Goal intake and execution**: natural language in; plan then run, or plan only.  
2. **Declarative DAG scheduling**: encode common goals as graphs; run with one command.  
3. **Capability-matrix invocation**: file I/O, search, and allow-listed commands within policy.  
4. **Multiple entry points**: interactive CLI, daemon, cron, and pipes.  
5. **Autonomous local memory**: store, recall, heuristic auto-store, and `memory_add`; no external embedding API.  

---

## Quick start

1. macOS or Linux with network access (to reach your chosen LLM API).  
2. From the repository root: `make` (produces the `neo` binary).  
3. Copy and edit configuration:

```bash
cp config/config.json5.example config/config.json5
# Set api_key and model name; enable memory.vector for autonomous memory (see below)
```

4. Verify:

```bash
./neo "Who are you?"
```

---

## Common commands

From the repository root (valid API credentials required):

```bash
# Product positioning
./neo "In three sentences, what goal-oriented tasks is Neo Agent good for?"

# Same reply with terminal Markdown rendering (md4c)
./neo -R "In three sentences, what goal-oriented tasks is Neo Agent good for?"

# Named session (persist turns under .neo/sessions/<ID>.json)
./neo -S ship -R "How do I build a spaceship?"
./neo -S ship -R "Option 4: real crewed / cargo spacecraft"
./neo --session-clear ship

# Run catalog DAGs
./neo dag run show_time
./neo dag run workspace_brief

# Natural-language goal: plan+execute / plan only
./neo run "show the system time"
./neo plan "summarize recent work in this repository"

# Read via the capability matrix, then summarize
./neo "Read README.md and summarize the product value in three English sentences."

# —— Autonomous memory (requires memory.vector.enabled) ——
./neo memory store "User prefers dark mode"
./neo memory recall "dark mode"
./neo -v "Please remember I prefer large fonts"   # auto-store when enabled
```

Further examples: [`docs/examples.md`](docs/examples.md). Memory and claw assembly: [`docs/claw.md`](docs/claw.md).

---

## Usage summary

| Intent | Command |
|--------|---------|
| Single question | `./neo "question"` |
| Run a named DAG | `./neo dag run <name>` or `./neo run <name>` |
| Natural-language goal (plan and execute) | `./neo run "goal"` |
| Plan only | `./neo plan "goal"` |
| Memory write / recall (no LLM) | `./neo memory store "…"` / `./neo memory recall "…"` |

---

## Autonomous memory

Neo Agent provides **on-host autonomous memory**: preferences and facts persist across sessions, retrieval augments the prompt, and **no external embedding service is required**. When enabled, Neo Agent can detect remember/prefer-style user intents and write them locally; the model may also call `memory_add` from the capability matrix.

### Configuration

```json5
memory: {
  path: "MEMORY.md",           // when vector is off: truncated file injection
  max_chars: 4000,
  vector: {
    enabled: true,             // enable local vector memory
    store: ".neo/memory.vdb",
    top_k: 5,
    dims: 64,
    auto_store: {
      enabled: true,           // heuristic autonomous write (default false)
      max_chars: 500,
    },
  },
}
```

### Behavior

| Mechanism | Description |
|-----------|-------------|
| **Recall injection** | With `vector.enabled`, retrieve chunks for the user query into `## Memory`; do not fall back to full `MEMORY.md` |
| **Heuristic auto-store** | With `auto_store.enabled`, user lines containing remember / prefer / 记住 / 偏好 cues are written to the local DB (`-v` logs `neo memory: auto-store`) |
| **Model-driven store** | Matrix tool `memory_add` for explicit persistence of durable facts |
| **CLI operations** | `memory store` / `memory recall` for write and dry-run recall without an LLM call |
| **Data boundary** | Vectors and text sidecar under `vector.store` (default `.neo/`); not written to `MEMORY.md`; embeddings computed in-process |

Authoritative claw and memory notes: [`docs/claw.md`](docs/claw.md).

---

## Documentation index

| Topic | Document |
|-------|----------|
| Scenarios and positioning | [`docs/applications.md`](docs/applications.md) |
| Worked examples | [`docs/examples.md`](docs/examples.md) |
| Capability matrix | [`docs/tool.md`](docs/tool.md) |
| DAG scheduling | [`docs/dag.md`](docs/dag.md) |
| Identity, rules, and memory | [`docs/claw.md`](docs/claw.md) |
| Contributing | [`AGENTS.md`](AGENTS.md) |
| Chinese README | [`README_zh.md`](README_zh.md) |

---

## For developers

Build and test: `make` / `make test`. Product triad: **DAG ∥ Capability Matrix ∥ Policy**. Conventions: [`AGENTS.md`](AGENTS.md). Architecture: [`docs/architecture.md`](docs/architecture.md).
