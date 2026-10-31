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
| `0x40019907` | OUT       | 1    | Reads a 1-byte status. Returns `0`. Under FreeBSD ioctl encoding, `0x40000000` is `IOC_OUT` (kernel → user), so the kernel writes into the caller's buffer rather than reading from it. The observed output byte was `0x04` while DDD was in `DELETE_INCOMPLETE`. |

mini-syscore disassembly of the sequence at `vaddr 0x0046fc80`:

```
LEA  rdi, ["/dev/icc_power"]
MOV  esi, 0x00010002
CALL open
TEST eax, eax
JS   fail
MOV  esi, 0xC0099902                 ; cmd = read 9 bytes (INOUT)
MOV  edi, eax                        ; fd
MOV  rdx, state_buf
CALL ioctl
TEST eax, eax
JNE  skip
LEA  rdx, [out_byte]                 ; 1-byte output buffer
MOV  esi, 0x40019907                 ; IOC_OUT — reads 1 byte from driver
MOV  edi, fd
CALL ioctl
```

**Note on ioctl direction:** earlier versions of this document labelled
`0x40019907` as `IN` (user → kernel). Under FreeBSD's `ioccom.h` encoding
(`0x40000000 = IOC_OUT`, `0x80000000 = IOC_IN`), the direction is actually
`OUT` (kernel → user). The caller's buffer is an output destination, not an
input command. The values mini-syscore places in the buffer before the call
are overwritten by the driver and are not delivered as commands. Whether the
PS5's driver dispatch strictly follows the FreeBSD convention has not been
independently confirmed, but the standard encoding is the working assumption.

The 9-byte state at offset 0: four bytes of per-component status, four bytes
reserved, one trailing byte. On the console under investigation while the DDD
was in `DELETE_INCOMPLETE`:

```
00 00 01 00 00 00 02 00 00
```

Byte `2 = 0x01` reads as "BD drive is physically attached". Byte `6 = 0x02`
appears consistently but its meaning was not identified. Byte `0 = 0x00` is
the BD-drive power indicator. Calling `Set(1)` did not flip it.

Ioctl `0x40019907` was called with the output buffer pre-filled to each of
`0x00` through `0x08`: every call returned `0` and the output buffer was
overwritten with the same value. Given the `IOC_OUT` encoding, the pre-fill
values were likely never delivered to the driver. The call appears to be a
pure status read, not a command interface.

## Why direct power-on does not work in this scenario

Two observations:

1. The typed function `sceKernelIccControlBDPowerState(1)` returns `0`
   (SCE_OK) but a subsequent `Get` still shows the drive unpowered. The
   request is accepted at the API level but has no observable effect.
2. The raw ioctl `0x40019907` is `IOC_OUT` — a status read, not a command
   write (see the direction note above). The `0x04` byte it returns matches
   the DDD `DELETE_INCOMPLETE` state, but it is not evidence of an ICC
   rejection message — it is the ICC reporting the current DDD state.

**Hypothesis (not proven):** the ICC subsystem (believed to be a separate ARM
microcontroller, though the architecture has not been independently confirmed)
internally checks the DDD pairing state before routing power and silently
refuses when the state is not `PAIRED` or `FACTORY_PAIRED`. The typed setter
returning success while having no effect is consistent with this, but other
explanations are possible: a missing initialization sequence, a host-driver
precondition, an asynchronous handshake, or a different subsystem making the
decision.

Without a way to change the persistent DDD state (which requires ICC NVS
writes with signature/MAC risk) or to identify and address the actual
enforcement point, there is no known user-space path to power the drive on.
