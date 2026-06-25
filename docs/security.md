# Security and Privacy Rules

TabForge is designed to be public by default and private at runtime.

## Never Commit

- Meshtastic PSKs, private keys, admin keys, QR/channel URLs, or full config exports.
- MeshCore channel/contact secrets.
- GitHub tokens.
- Wi-Fi passwords.
- Raw audio recordings.
- Camera captures.
- GPS tracks and field logs.
- SD backups.

## Runtime Guardrails

- Show redacted summaries instead of raw radio exports.
- Store learned IR codes without cloud account context.
- Require a physical/on-screen confirmation before OTA writes.
- Require profile backup before companion firmware changes.
- Keep destructive actions out of mesh-triggered macros unless the action is allowlisted and confirmed locally.

## Public Repo Guardrails

- `.gitignore` blocks common secrets, logs, audio, and backups.
- `tools/verify-project.ps1` scans tracked text for high-risk placeholders and validates public manifests.
- GitHub Pages hosts only docs and manifest metadata.

