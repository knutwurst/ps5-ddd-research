# ICC BD-drive power control

## Via libSceKernel

Two kernel-side functions provide the typed interface:

| Symbol                               | NID                 |
|--------------------------------------|---------------------|
| `sceKernelIccGetBDPowerState`        | `zLEuSU+hl-w#Z#X`  |
| `sceKernelIccControlBDPowerState`    | `1wxFFk3xIhA#Z#X`  |

Inferred signatures:

```c
int sceKernelIccGetBDPowerState(uint8_t *state_out);
int sceKernelIccControlBDPowerState(int state);
```

Observed:

- `state_out` receives a single byte; the caller buffer's upper bytes are left
  unchanged, so reading as `int32_t` yields garbage in the high bits.
- `state` of `0` or `1` is accepted and returns `0` (SCE_OK).
- `state` of `2` or `3` returns `0x80020016` — invalid input.
- Even though `Set(1)` returns success, `Get` continues to return a byte of
  `0x00`. The ICC subsystem accepts the request and silently discards it.

Resolution in a payload: iterate the process's library handles with
`kernel_dynlib_mapbase_addr` and call `kernel_dynlib_resolve(pid, handle, nid)`
until one returns an address. In practice the handle holding libSceKernel is
small (observed as `1`).

## Via /dev/icc_power

mini-syscore does the equivalent work at a lower level by writing a single byte
to the device.

Open flags: `0x00010002`.

Observed ioctls, all with group `0x99` (which also serves many other ICC
operations):

| Code         | Direction | Size | Observed behavior |
|--------------|-----------|------|-------------------|
| `0xC0019901` | INOUT     | 1    | Returns the byte you pass in. Likely a device-select echo. |
| `0xC0099902` | INOUT     | 9    | Reads a 9-byte status structure (see below). |
| `0x40019907` | IN        | 1    | Accepts a 1-byte command. Returns `0`. Output buffer is overwritten with the current DDD state byte on exit (observed: `0x04` while DDD is `DELETE_INCOMPLETE`). |

mini-syscore disassembly of the sequence at `vaddr 0x0046fc80`:

```
LEA  rdi, ["/dev/icc_power"]
MOV  esi, 0x00010002
CALL open
TEST eax, eax
JS   fail
MOV  esi, 0xC0099902                 ; cmd = read 9 bytes
MOV  edi, eax                        ; fd
MOV  rdx, state_buf
CALL ioctl
TEST eax, eax
JNE  skip
LEA  rdx, [cmd_byte]                 ; 1-byte input
MOV  esi, 0x40019907
MOV  edi, fd
CALL ioctl
```

The 9-byte state at offset 0: four bytes of per-component status, four bytes
reserved, one trailing byte. On the console under investigation while the DDD
was in `DELETE_INCOMPLETE`:

```
00 00 01 00 00 00 02 00 00
```

Byte `2 = 0x01` reads as "BD drive is physically attached". Byte `6 = 0x02`
appears consistently but its meaning was not identified. Byte `0 = 0x00` is
the BD-drive power indicator. Calling `Set(1)` did not flip it.

Directly writing byte values `0x00` through `0x08` via ioctl `0x40019907` was
tried: every write returned `0` with no change to the state read via
`0xC0099902`. The device accepts and discards.

## Why direct power-on does not work in this scenario

The ICC subsystem is a separate ARM microcontroller. Both the typed function
`sceKernelIccControlBDPowerState` and the raw ioctl `0x40019907` end up as
messages to that microcontroller. The microcontroller firmware is where the
decision to actually drive power to the BD module is made.

Observationally, when the DDD pairing state is `DELETE_INCOMPLETE` (`0x04`),
the ICC firmware acknowledges `Set(1)` but never routes power. The 1-byte
output buffer after `0x40019907` is overwritten with `0x04`, strongly
suggesting the ICC is reflecting back the DDD state as the reason for refusal.

Without the ability to patch the ICC firmware or convince it that the DDD state
is `PAIRED` (which requires persistent NVS changes with signature risk), there
is no user-space path to actually power the drive on.
