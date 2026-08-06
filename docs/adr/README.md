# Architecture Decision Records

This directory holds ADRs — short documents recording a single
architectural decision, its context, and its consequences.

## Index

- [0001 — Microkernel Design](./0001-microkernel-design.md)

## Format

Each ADR uses the structure:

- **Title** (one line, verb-shaped where possible)
- **Status** (Proposed | Accepted | Deprecated | Superseded)
- **Date**
- **Context** — what forces created the need for a decision
- **Decision** — the choice made
- **Consequences** — both positive and negative
- **Alternatives Considered** — what we explicitly rejected and why
- **References** — background reading

ADRs are immutable once accepted. To change a decision, write a
new ADR that supersedes the old one and update the index above.

## Conventions

- Filenames are zero-padded, monotonically increasing:
  `NNNN-kebab-case.md`.
- Every ADR is short — typically under 200 lines. If a decision
  needs more than that, split it into multiple ADRs.
- Every ADR links the README sections it touches.