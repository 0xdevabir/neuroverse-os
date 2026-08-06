# Security Policy

## Supported Versions

The NeuroVerse OS project is currently in **Phase 0 — Concept / Scaffold**
(see `README.md` §10). At this stage there is **no supported release** and
**no security patches** are issued. Do not run this code in production.

Once Phase 1 begins (microkernel bring-up), supported versions will be
listed here.

## Reporting a Vulnerability

If you discover a security vulnerability in this project, please report it
privately to:

**security@neuroverse-os.example** (replace with real contact before publishing)

Please **do not** open a public GitHub issue for security vulnerabilities.

### What to include

- A clear description of the vulnerability and its impact.
- Steps to reproduce, ideally with a minimal proof of concept.
- Affected versions / commits.
- Your assessment of severity (informational / low / medium / high / critical).
- Your name / handle if you'd like to be credited (otherwise reports are
  anonymous by default).

### Response

We will acknowledge receipt within 7 days and provide a triage assessment
within 30 days. Critical issues will be addressed faster.

We follow [coordinated disclosure](https://en.wikipedia.org/wiki/Coordinated_vulnerability_disclosure):
please give us a reasonable window (typically 90 days) before public
disclosure.

## Out of Scope (for now)

Because the project is pre-alpha:

- Compiler bugs in third-party dependencies (report upstream).
- Issues in sample/demo code not in the `src/` tree.
- Theoretical issues without a concrete attack path.

## Recognition

We maintain a [Hall of Fame](./docs/security/hall-of-fame.md) (to be added)
for security researchers who help improve NeuroVerse OS.