# Local Vector Memory Implementation Plan

> **For agentic workers:** Implement task-by-task. No agent Co-authored-by trailers (AGENTS §8).

**Goal:** Add `src/memory/` over `vdb.h` with local-only embeddings; optional recall into claw Memory section.

**Architecture:** `neo_embed` (hash bag) → `neo_memory` (chunk + vdb) → CLI/daemon call recall only.

**Tech Stack:** C99, vendored `vdb.h`, yyjson config, `make test`.

**Spec:** [`docs/superpowers/specs/2026-09-12-memory-vdb-design.md`](../specs/2026-09-12-memory-vdb-design.md)

## Global Constraints

- No HTTP embeddings.
- Only `src/memory/**` includes `vdb.h`.
- `vector.enabled` default false.
- Directory README required for `src/memory/`.
- Chinese comments on non-trivial logic.

---

### Task 1: embed + memory library + tests

**Files:** create `src/memory/*`, modify `Makefile`, `config.h`/`config.c`, tests.

- [x] Implement `neo_embed_text`, unit test.
- [x] Implement `neo_memory_open/close/recall/reindex`.
- [x] Parse `memory.vector.*`.
- [x] `make test` for new tests.

### Task 2: Wire CLI/daemon + docs

- [x] Replace raw MEMORY read in `main.c` / `daemon.c`.
- [x] Update `docs/claw.md` briefly.
- [x] Full `make test`; commit without trailers.
