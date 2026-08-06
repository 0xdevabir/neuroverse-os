# Contributing to NeuroVerse OS

Thank you for your interest in contributing to **NeuroVerse OS**. This project
is a multi-decade effort to build a self-evolving, capability-secure,
heterogeneous, distributed operating system from first principles.

## License & CLA

NeuroVerse OS is dual-licensed under **MIT** and **Apache 2.0**. All
contributors must sign a **Contributor License Agreement (CLA)** before their
contributions can be merged. A bot will guide you through the CLA process
on your first pull request.

## Code of Conduct

By participating, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

## How to Contribute

### Reporting Bugs

- Use the GitHub issue tracker.
- Search existing issues first.
- Include: OS, compiler, build flags, minimal reproduction, expected vs actual.

### Proposing Features / Design Changes

- Open an issue with the `proposal` label **before** writing code.
- Significant changes require an **Architecture Decision Record (ADR)** in
  `docs/adr/`. Use `docs/adr/0000-template.md` (when added) as a starting
  point.
- Discussion happens in the issue; consensus is required before merging.

### Pull Requests

1. Fork the repository and create a topic branch (`feature/<name>` or
   `fix/<name>`).
2. Follow the [code style](#code-style).
3. Add tests for new functionality.
4. Ensure CI passes (build + lint + tests).
5. Keep commits small and use [Conventional Commits](https://www.conventionalcommits.org/)
   (`feat:`, `build:`, `chore:`, `test:`, `docs:`, `fix:`).
6. Update relevant docs and ADRs.

## Code Style

- **Language:** C++20/23 (C++26 features may be used behind `__has_cpp_attribute`
  guards).
- **Formatting:** `clang-format` with the project config (LLVM base + project
  overrides). Run `just format` before committing.
- **Linting:** `clang-tidy` with the project config. Run `just lint`.
- **Includes:** `include-what-you-use` is enforced.
- **Naming:**
  - Types: `PascalCase`
  - Functions: `snake_case`
  - Constants: `kPascalCase` or `UPPER_SNAKE` for macros
  - Members (private): trailing underscore `name_`

## Subsystem Ownership

Each subsystem has a designated directory under `src/` and `include/neuro/`.
Proposals affecting a subsystem should be reviewed by at least one
subsystem maintainer.

## Commit Hygiene

- One logical change per commit.
- Commit messages: short summary (≤72 chars), blank line, optional body.
- Reference issues / ADRs in the body.

## Testing

- Unit tests live in `tests/unit/<subsystem>/`.
- Integration tests live in `tests/integration/`.
- Run `just test` before pushing.

## Out-of-Scope Contributions (for now)

- Real kernel boot (until Phase 1 begins).
- Hardware drivers (until `NeuroDev` is bootstrapped).
- Real JIT / ML (until Phase 3).

Thanks for helping build the OS you wish existed!