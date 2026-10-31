# Technical findings

Firmware: PS5 11.60. Model family: CFI-2016 (PS5 Slim).

All vaddrs refer to `SceShellCore.elf` unless stated otherwise.
Absolute addresses on the running system are `main_executable_base + vaddr`.

## Pairing state enum

The ioctl `0xC0284406` on `/dev/ddd` returns a 4-byte little-endian integer:

| Value | Name                       |
|-------|----------------------------|
| 0     | `PAIRED`                   |
| 1     | `PAIRED_TO_ANOTHER`        |
| 2     | `NOT_PAIRED`               |
| 3     | `REGISTER_INCOMPLETE`      |
| 4     | `DELETE_INCOMPLETE`        |
| 5     | `FACTORY_PAIRED`           |
| 6     | `FACTORY_PAIRED_TO_ANOTHER`|

## ShellCore patches

### Patch 1 — force the switch dispatch in `checkDddPairingStatus`

Function: `_AutoMounter::DiscHandler::checkDddPairingStatus()`
Entry: `0x0036bb00`

Switch dispatch:
```
0x0036bfc4: 8B 85 44 FE FF FF    MOV eax, [rbp-0x1bc]   ; load pairing state
0x0036bfca: 48 83 F8 06           CMP rax, 6
0x0036bfce: 0F 87 C8 FE FF FF    JA err_handler
0x0036bfd4: 48 8D 0D DD F5 A1 01  LEA rcx, [jump_table @ 0x01d8b5b8]
0x0036bfdb: 48 63 04 81           MOVSXD rax, [rcx + rax*4]
0x0036bfdf: 48 01 C8              ADD rax, rcx
0x0036bfe2: FF E0                 JMP rax
```

Jump table at `0x01d8b5b8` (verified by decoding the 7 signed 32-bit offsets):

| Index | Target     | State                      |
|-------|------------|----------------------------|
| 0     | `0x0036bfe4` | `PAIRED`                   |
| 1     | `0x0036c181` | `PAIRED_TO_ANOTHER`        |
| 2     | `0x0036c11b` | `NOT_PAIRED`               |
| 3     | `0x0036c14e` | `REGISTER_INCOMPLETE`      |
| 4     | `0x0036c0e8` | `DELETE_INCOMPLETE`        |
| 5     | `0x0036c1b1` | `FACTORY_PAIRED`           |
| 6     | `0x0036c1f9` | `FACTORY_PAIRED_TO_ANOTHER`|

Patch at vaddr `0x0036bfc4`, 6 bytes:

| Field      | Bytes               |
|------------|---------------------|
| Before     | `8B 85 44 FE FF FF` |
| After      | `31 C0 90 90 90 90` |
| Mnemonic   | `XOR eax, eax; NOP x4` |

Effect: the switch dispatches to case 0 (`PAIRED`) regardless of the real state.

### Patch 2 — stop the write to `g_PreviousPairingStatus`

Function: `_AutoMounter::DiscHandler::getDddPairingStatus(uint32_t&)`
Entry: `0x0036b0b0`

```
0x0036b373: 89 05 E7 9E BC 01    MOV [g_PreviousPairingStatus], eax
```

Patch at vaddr `0x0036b373`, 6 bytes:

| Field      | Bytes               |
|------------|---------------------|
| Before     | `89 05 E7 9E BC 01` |
| After      | `90 90 90 90 90 90` |
| Mnemonic   | `NOP x6`            |

Effect: `g_PreviousPairingStatus` is never overwritten by subsequent queries.

### Patch 3 — zero `g_PreviousPairingStatus` directly

Global at vaddr `0x01f35260`, 4 bytes. Write `00 00 00 00` to force the
cached state to `PAIRED`.

### Patch 4 — NOP the `SetBDPower(0)` call in `turnOffBdDrive`

Function: `_AutoMounter::DiscHandler::turnOffBdDrive()`
Entry: `0x0036b920`

```
0x0036ba23: 31 FF                XOR edi, edi               ; arg0 = 0 (OFF)
0x0036ba25: E8 c6 04 50 01       CALL sceKernelIccControlBDPowerState
```

Patch at vaddr `0x0036ba25`, 5 bytes:

| Field      | Bytes               |
|------------|---------------------|
| Before     | `E8 ?? ?? ?? ??`    |
| After      | `90 90 90 90 90`    |
| Mnemonic   | `NOP x5`            |

Effect: ShellCore can no longer power the drive off programmatically. The CALL
displacement varies by build so the patcher checks only the leading `E8`.

## Dipsw 78 trampoline

Symbol: `sceKernelCheckDipsw`
NID: `JSNgID8Bupk#K#K`
Found in: `libSceDipsw.sprx`

The library is mapped as shared memory across every process that imports it, so
patching once in one process patches system-wide. The ShellCore handle is
usually `0x100`; other processes see various handles.

Trampoline, 11 bytes written over the function prologue:

```
83 FF 4E          CMP edi, 78
75 06             JNE +6
B8 01 00 00 00    MOV eax, 1
C3                RET
```

Return value `1` for index `78` enables the `SCE_KERNEL_DIPSW_ENABLE_DDD_EMU`
code path in ShellCore. ShellCore's log strings confirm the intent:

- `[DDD][Dipsw] SCE_KERNEL_DIPSW_ENABLE_DDD_EMU is enabled. Success DDD pairing.`
- `[DDD][Dipsw] SCE_KERNEL_DIPSW_ENABLE_DDD_EMU is enabled. Success DDD unpairing.`
- `[DDD][Dipsw] checkDddPairingStatus() returned SCE_OK.`

Patching this does not change the kernel-level DDD state.

## Direct ICC power-on

Two kernel-side NIDs, resolvable in-process via `kernel_dynlib_resolve`:

| Function                             | NID                    | Library |
|--------------------------------------|------------------------|---------|
| `sceKernelIccGetBDPowerState`        | `zLEuSU+hl-w#Z#X`     | libSceKernel |
| `sceKernelIccControlBDPowerState`    | `1wxFFk3xIhA#Z#X`     | libSceKernel |

Signatures (inferred from calling convention and ShellCore use):

```c
int sceKernelIccGetBDPowerState(uint8_t *state_out);   // writes 1 byte
int sceKernelIccControlBDPowerState(int state);        // 0 = OFF, 1 = ON
```

ShellCore uses these in `_AutoMounter::DiscHandler::turnOnBdDrive()` at vaddr
`0x0036b430`:

```
0x0036b4e1: CALL sceKernelIccGetBDPowerState     ; probe
0x0036b6d5: MOV  edi, 1                          ; arg0 = ON
0x0036b6da: CALL sceKernelIccControlBDPowerState
```

Calling `sceKernelIccControlBDPowerState(1)` directly from a payload returns `0`
(SCE_OK). The ICC subsystem does not actually power the drive — a subsequent
`Get` still shows byte `0x00`. See `docs/ICC_POWER.md` for the raw-ioctl path.

## /dev/ddd ioctl matrix (observed behavior)

See `docs/DDD_IOCTLS.md` for full detail. Summary:

| Code         | Behavior with zero input                       |
|--------------|------------------------------------------------|
| `0xC0284401` | OK; returns 16-byte nonce. Likely `GetNonce`.  |
| `0xC0284402` | status `0x800F140E` — needs crypto response.   |
| `0xC0284403` | status `0x800F140E`.                           |
| `0xC0284404` | status `0x800F1428` — different precondition.  |
| `0xC0284405` | status `0x800F140E`.                           |
| `0xC0284406` | OK; returns 4-byte pairing state.              |
| `0xC0284407` | OK; returns all zeros.                         |

ShellCore itself uses codes `01, 02, 03, 04, 05, 07` but never `06` — the
`DELETE_INCOMPLETE`-returning status code is apparently a factory/debug ioctl.

## Device inventory (DDD-related)

Opens observed:

- `/dev/ddd` — the main DDD interface. `O_RDWR` succeeds.
- `/dev/sbl_srv` — SBL service IPC. `O_RDWR` succeeds but no ioctl group in the
  obvious ranges (`0x44`, `0x53`, `0x62`, `0x99`, `0x5B`) responds. Writes and
  reads return `ENODEV`.
- `/dev/iccnvs1`, `/dev/iccnvs2`, `/dev/iccnvs4` — ICC NVS partitions. All three
  open in `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_SYNC`.
- `/dev/icc_power` — ICC power. Opens with mode `0x00010002` (as per
  mini-syscore).
- `/dev/icc_configuration` — opens but `read()` returns `ENODEV`.
- `/dev/bdemuctl` — BD drive emulator control. **Returns `ENOENT`.** The
  device node only exists when dipsw 78 was already enabled at boot time, which
  is not possible to arrange from user space after boot.
- `/dev/cd0` — CAM/SCSI node for the BD drive. `ENOENT` while the drive is
  unpowered.

## Why nothing further is safe

The ICC subsystem is a separate ARM microcontroller. It makes its own decision
about whether to actually route power to the drive based on either its own
cached state or a direct read from ICC NVS. Our user-space patches do not touch
either, so the ICC continues to see `DELETE_INCOMPLETE` and silently refuses.

The only remaining options are:

1. Write a `0x00` byte to ICC NVS at the DDD state offset. The region likely
   has a MAC or signature covering the state, so breaking that could cause the
   DDD section to be reset to factory on next boot, which would invalidate the
   stored keys.
2. Patch the kernel-level DDD driver's ioctl handler. Earlier attempts to read
   kernel text/heap pages via `kernel_copyout` triggered page faults on
   unmapped pages, causing kernel panics.

Neither option is appropriate for a device the owner needs working. The
research stops here.
