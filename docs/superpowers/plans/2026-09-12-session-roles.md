# Session roles (`--role`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Same `-S` session can switch roles via `--role NAME` using config `roles` prompts.

**Architecture:** Parse `roles` into `agent_config_t`; CLI looks up name, appends `## Role` to system prompt, prefixes saved assistant text with `[NAME] `.

**Tech Stack:** C99, yyjson JSON5, existing `neo_session` / CLI claw assembly.

**Spec:** [`docs/superpowers/specs/2026-09-12-session-roles-design.md`](../specs/2026-09-12-session-roles-design.md)

## Global Constraints

- Role id charset `[A-Za-z0-9_-]`, length 1..64
- No new session JSON fields; prefix only on assistant content when `--role` set
- No Co-authored-by / agent trailers on commits
- `make test` must pass
- Chinese comments on non-trivial config/CLI glue

## File map

| File | Role |
|------|------|
| `src/core/config.h` / `config.c` | `neo_role_t`, parse/free/`config_find_role` |
| `src/cli/main.c` | `--role`, inject prompt, prefix on save |
| `tests/test_config_roles.c` (+ Makefile) | Parse + find |
| `config/config.json5.example` | Commented example |
| `README.md` / `README_zh.md` / `docs/examples.md` | Usage |

---

### Task 1: Config parse + unit test

- [ ] Add `neo_role_t { char *name, *description, *prompt; }` and array on `agent_config_t`
- [ ] Parse top-level `roles` object; validate id; require `prompt`
- [ ] `config_find_role(c, name)` → pointer or NULL
- [ ] Free in `config_free`
- [ ] Fixture + `tests/test_config_roles.c`; wire Makefile; `make test`

### Task 2: CLI `--role`

- [ ] Parse `--role NAME`
- [ ] After claw blocks, if role set: lookup or error; append `## Role: NAME\n\n` + prompt
- [ ] On successful session save: prefix assistant with `[NAME] ` (allocate temp buffer)
- [ ] Help text; daemon leave alone (per spec follow-up)

### Task 3: Docs + example config

- [ ] `config.json5.example` commented `roles`
- [ ] README Features / Usage one-liners; `docs/examples.md` short subsection
- [ ] Spec status → 已批准 / 已实施
- [ ] Commit via `commit-tree` (no Co-authored-by); push if user asked
