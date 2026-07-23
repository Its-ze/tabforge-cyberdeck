# Tab5 Source Preservation Record

This record resolves the two existing Tab5 codebases without deleting either one.

## Architecture Decision

- `F:\Dropbox\Dev Ops\TabForge Cyberdeck` is the forward Tab5 product and receives the new layered architecture.
- `F:\Dropbox\Dev Ops\TabOS` remains the preserved legacy UI/HAL reference. It is not deleted, rewritten, or flashed as part of this pass.
- The known physical COM25 flash backup remains unchanged.

## Pre-change Revisions

- TabForge Cyberdeck: `af9467d40ce27ab90600738dfc63963330324d5c`
- TabOS: `301df1537a9bc6d139f6f75176da90145963e66e`
- Both repositories have the local annotated tag `tab5-pre-layered-20260723`.

## Verified Archives

The complete Git histories were exported to:

- `F:\Dropbox\Dev Ops\Tab5 Source Archives\2026-07-23-pre-layered\TabForge-Cyberdeck-pre-layered-20260723.bundle`
  - Size: `14,414,754` bytes
  - SHA256: `14A8A8A1CDA5AB53FD3FA49E4E642657250074F3D2756C0B1DBF0CB93B27589F`
- `F:\Dropbox\Dev Ops\Tab5 Source Archives\2026-07-23-pre-layered\TabOS-pre-layered-20260723.bundle`
  - Size: `85,224` bytes
  - SHA256: `2F83E1BD52D7E86CC5F2E50A78383D63C5F7439FCEB8D4FED441907BACD439B0`

`git bundle verify` reported complete history for both archives.

## Physical Flash Evidence

- Existing read-only image: `F:\Dropbox\Dev Ops\CYD Sheet Music Reader\backups\com25-readonly-20260628-012546-4mb.bin`
- Size: `4,194,304` bytes
- SHA256: `6D4015A065200C5019DA21DDB5A4BD26D77F4E656FED6EA57DBE65D6CF13BEB8`

No serial port was opened, erased, or flashed during this architecture pass. COM25 must still be positively identified and backed up again before any future physical update.
