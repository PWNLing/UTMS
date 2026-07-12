# Domain Docs

This repo is a single-context project.

## Before exploring, read these

- `docs/PRD.md` for authoritative product requirements.
- `docs/code-standards.md` for authoritative coding standards.
- `CONTEXT.md` at the repo root, if it exists.
- `docs/adr/`, if it exists, for architectural decisions relevant to the area being changed.

If optional domain files do not exist, proceed silently.

## File structure

Expected single-context layout:

```text
/
├── AGENTS.md
├── CONTEXT.md
├── docs/
│   ├── PRD.md
│   ├── code-standards.md
│   ├── agents/
│   │   ├── issue-tracker.md
│   │   ├── triage-labels.md
│   │   └── domain.md
│   └── adr/
└── src/
```

## Consumer rules

Use `docs/PRD.md` to resolve product behavior and scope questions before planning or implementing features.

Use `docs/code-standards.md` for all code style, naming, Qt/C++ design, threading, logging, and testing expectations.

Do not treat missing `CONTEXT.md` or `docs/adr/` as a setup failure; those files can be created later when domain terms or decisions need to be recorded.
