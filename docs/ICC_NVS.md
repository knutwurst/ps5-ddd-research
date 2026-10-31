# ICC NVS layout

The PS5 Slim exposes three ICC NVS partition device nodes that respond:

| Node             | Observed contents |
|------------------|-------------------|
| `/dev/iccnvs1`   | Opens, contents not inspected. |
| `/dev/iccnvs2`   | Console identity area. |
| `/dev/iccnvs4`   | DDD-related state. |

`/dev/iccnvs0`, `3`, and `5` return `ENOENT`.

All three accessible nodes open with any of `O_RDONLY`, `O_WRONLY`, `O_RDWR`,
`O_RDWR|O_SYNC`, `O_WRONLY|O_SYNC`, and the raw mode `0x00010002` that
mini-syscore uses for `/dev/icc_power`. No actual writes were performed during
this investigation.

## /dev/iccnvs2 header

Offset `0x00`, 64 bytes read via `pread`. Contains the console serial, model
code, and a region string. All sensitive bytes have been redacted here.

```
0x00: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX  (serial)
0x10: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX  (serial cont.)
0x20: XX FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
0x30: 43 46 49 2d XX XX XX XX 20 XX XX XX XX 00 00 00  (model code)
```

Byte `0x30`-`0x3C` holds the console's CFI model code. Bytes `0x00`-`0x20` are
the serial. Both uniquely identify the physical console.

Offset `0xc30` returns `pread` error `5 (EIO)` on this partition. The DDD area
is not here.

## /dev/iccnvs4 header

Offset `0x00`, 64 bytes:

```
0x00: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
0x10: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
0x20: 00 ff ff 01 ff ff ff ff ff ff ff ff ff ff ff ff
0x30: ff ff ff ff 03 ff ff ff ff ff ff ff ff ff ff ff
```

The header is mostly erased flash (`0xff`). A `0x01` at `0x23` and a `0x03` at
`0x34` suggest flags written during factory provisioning.

## /dev/iccnvs4 at offset 0xc30

SysCore reads 8 bytes from this offset in function at vaddr `0x0004a820`:

```
0x00: 01 00 00 00 05 00 60 11 ff ff ff ff cf d3 19 00
0x10: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
0x20: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
0x30: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
```

Interpretation hypothesis:

| Offset | Bytes            | Possibly |
|--------|------------------|----------|
| `0x00` | `01 00 00 00`    | Version / generation counter |
| `0x04` | `05 00`          | `FACTORY_PAIRED` state marker |
| `0x06` | `60 11`          | Unknown — maybe a counter |
| `0x08` | `ff ff ff ff`    | Erased |
| `0x0C` | `cf d3 19 00`    | A larger counter or hash |

## /dev/iccnvs4 at offset 0x1000 — DDD STATE AREA

```
0x00: 04 00 20 01 01 00 31 00 70 01 08 00 00 00 00 00
0x10: XX XX XX XX 01 00 00 00 00 00 00 00 00 00 00 00
0x20: 00 00 29 20 01 00 14 01 00 00 00 00 00 00 00 00
0x30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Byte `0x00` was `0x04` on the console under investigation, matching
`DELETE_INCOMPLETE`. This is almost certainly the persistent store the kernel
loads the DDD state from at boot.

Bytes `0x10`-`0x13` are redacted (`XX XX XX XX`) because they looked like
console-unique key material or a key hash. Their presence confirms the
pairing keys were not actually wiped by the incomplete delete.

The surrounding structure (byte `0x02 = 0x20`, byte `0x04-0x07 = 01 00 31 00`)
is likely a header; without more samples it is impossible to say whether the
state byte alone can be modified safely.

## Why not write here

Changing byte `0x1000` from `0x04` to `0x00` would make the kernel load state
`PAIRED` at next boot. The ICC would then be told pairing is valid and should
power the drive.

Risks that stopped this from being attempted:

1. The region plausibly carries a MAC/signature over its contents. The
   `XX XX XX XX` at `0x10` could be exactly that. Any modification would
   invalidate it, and the kernel's typical response is to reset the whole
   partition to factory defaults on next boot. That would effectively erase
   the stored keys and leave the drive permanently unpaired until an official
   factory-pair flow is run.
2. Flash writes are block-level operations. Even writing a single byte triggers
   a read-modify-write cycle. An interrupt or power loss mid-write can
   corrupt the entire block.
3. The device nodes open writable but the kernel driver may still reject the
   actual write, or may silently discard it. Testing this is not free of
   risk — any attempted write exercises the signature code path.

Because the console in question is in daily use and its owner cannot risk
making the DDD subsystem worse, no writes were performed.
