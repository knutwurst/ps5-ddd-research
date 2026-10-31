# PS5 DDD Pairing Research

Research notes and payload sources from an investigation into the PS5 Detachable
Disc Drive (DDD) pairing subsystem on firmware 11.60.

> **Scope**: interoperability research and personal device recovery only.
> This repository does not enable piracy, does not circumvent any disc-media
> copy protection, and does not redistribute any Sony binaries.
> See [DISCLAIMER.md](DISCLAIMER.md) for the full statement.

## Context

A PS5 Slim ended up with its disc drive in the `DELETE_INCOMPLETE` state after a
factory reset was performed without an internet connection. The pairing deletion
was initiated but never completed. The disc drive hardware stopped responding to
all commands; `/dev/cd0` becomes unrecognizable and the drive receives no power.

The pairing keys appear to still be present in ICC NVS (the delete was never
finished), but the system refuses to use the drive because the kernel-level DDD
state is `4` (`DELETE_INCOMPLETE`).

## Goal

Figure out what the DDD pairing subsystem actually does, what state is held
where, and whether an unprivileged user-space payload can convince the system to
treat the drive as paired again without touching persistent storage.

## Outcome

Software-level success, hardware-level failure.

- **Works:** ShellCore can be made to see every pairing-state query return
  `PAIRED (0)`. The global cache `g_PreviousPairingStatus` can be forced to `0`.
  The `turnOffBdDrive()` path can be NOP'ed so the drive is never actively
  powered off. The `sceKernelCheckDipsw` function can be trampolined to always
  return `1` for index 78 (`SCE_KERNEL_DIPSW_ENABLE_DDD_EMU`), system-wide via
  the shared `libSceDipsw.sprx` memory.
- **Works:** `sceKernelIccControlBDPowerState(1)` resolves and returns `0`
  (success).
- **Does not work:** the ICC subsystem (a separate ARM microcontroller) returns
  success for the power-on command but does not actually power the drive. A
  readback of the ICC state shows byte `0x04` reported where there should be a
  state, which matches the kernel's DDD `DELETE_INCOMPLETE` state. The ICC
  firmware appears to internally check the DDD pairing state and silently reject
  power-on requests when it is not `0` or `5`.

The only remaining paths to recovery either require writing to ICC NVS (risky,
possibly MAC-protected) or kernel-level code patches (previous attempts caused
page faults in unmapped kernel memory). Neither is appropriate for a device the
owner cannot afford to lose.

## Repo contents

- [`docs/FINDINGS.md`](docs/FINDINGS.md) — the technical details: offsets,
  NIDs, patch bytes, ioctl matrix, ICC NVS layout.
- [`docs/SHELLCORE.md`](docs/SHELLCORE.md) — ShellCore pairing-related
  functions and globals, with the exact patch sites.
- [`docs/ICC_POWER.md`](docs/ICC_POWER.md) — ICC power control: the two
  relevant NIDs, the device node, and the raw ioctl protocol used by
  mini-syscore.
- [`docs/DDD_IOCTLS.md`](docs/DDD_IOCTLS.md) — observed behavior of each
  `/dev/ddd` ioctl.
- [`docs/ICC_NVS.md`](docs/ICC_NVS.md) — layout of the DDD-related area in
  `/dev/iccnvs4` (sensitive bytes redacted).
- [`payloads/`](payloads) — C sources for the ps5-payload-sdk toolchain.
- [`scripts/`](scripts) — Python helpers for binary analysis.

## Toolchain

All payloads build against the
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and deploy over
TCP via `prospero-deploy` to the PS5's ELF loader.

## Related work

- [`earthonion/ddd_pair_dump`](https://github.com/earthonion/ddd_pair_dump) —
  PS5 DDD 4-stage pairing pipeline dump tool
- [`bc160201983/Troubleshoot-PS5-disc-drive-pairing`](https://github.com/bc160201983/Troubleshoot-PS5-disc-drive-pairing) —
  DDD protocol analysis
- [`ps5-payload-dev/sdk`](https://github.com/ps5-payload-dev/sdk) — the
  payload toolchain used here

## License and disclaimer

- Code and documentation are released under the [MIT License](LICENSE).
- The full statement of purpose, legal framing, and scope is in
  [DISCLAIMER.md](DISCLAIMER.md). Read it before using anything here.

Nothing in this repository redistributes Sony binaries. The analysis is based
on binaries dumped from the owner's own console. All findings are intended for
recovery and interoperability research only.
