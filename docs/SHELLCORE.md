# SceShellCore DDD code map

All addresses are vaddrs in `SceShellCore.elf` on PS5 firmware 11.60.

## Relevant functions

| vaddr        | Function |
|--------------|----------|
| `0x0036b0b0` | `_AutoMounter::DiscHandler::getDddPairingStatus(uint32_t&)` |
| `0x0036b430` | `_AutoMounter::DiscHandler::turnOnBdDrive()` |
| `0x0036b920` | `_AutoMounter::DiscHandler::turnOffBdDrive()` |
| `0x0036bb00` | `_AutoMounter::DiscHandler::checkDddPairingStatus()` |
| `0x003737b0` | `_AutoMounter::powerOnBdDrive()` (no direct callers found) |

The `_AutoMounter::powerOnBdDrive()` at `0x003737b0` has zero direct call sites
in the binary. It is reached via vtable or callback dispatch that was not
located during this investigation.

## Global variables

| vaddr        | Name                       | Type   |
|--------------|----------------------------|--------|
| `0x01f35260` | `g_PreviousPairingStatus`  | uint32 |
| `0x01f6c478` | `g_bdDrivePowerOnTime`     | uint64 |

The log string
`[DDD] g_PreviousPairingStatus=%#x, g_bdDrivePowerOnTime=%lu`
appears in `getDddPairingStatus` at the point where both globals are read.

## checkDddPairingStatus flow (abridged)

```
0x0036bb00: entry
    ... early setup, locale/log function pointers ...
0x0036bd93: CALL sceKernelIccIsExtBDDriveAttached     (NID 1ns3nDImmno#Z#X)
    ...
0x0036bee5: CALL state getter (0x00601000 → stub → 0x006059a0)
    ...
0x0036bf40: CMP [rbp-0x1c0], 0                        ; network state check
0x0036bf59: CALL state getter (0x00601020)            ; eax = state code
0x0036bf5e: TEST eax, eax
0x0036bf60: JE  success_branch
    ...
0x0036bfc4: MOV eax, [rbp-0x1bc]                      ; load pairing state
0x0036bfca: CMP rax, 6
0x0036bfce: JA  err_handler
0x0036bfd4: LEA rcx, [0x01d8b5b8]                     ; jump table
0x0036bfdb: MOVSXD rax, [rcx+rax*4]
0x0036bfdf: ADD rax, rcx
0x0036bfe2: JMP rax                                   ; dispatch
```

## Patch 1 target bytes

Before:
```
0x0036bfc4: 8B 85 44 FE FF FF    MOV eax, [rbp-0x1bc]
```

After:
```
0x0036bfc4: 31 C0 90 90 90 90    XOR eax, eax ; NOP x4
```

The surrounding dispatch bytes stay intact. Because `rax = 0` the subsequent
`CMP rax, 6` is not above, `JA` is not taken, and `rax = 0` indexes the first
jump-table entry (`PAIRED`).

## Patch 2 target bytes

Before:
```
0x0036b373: 89 05 E7 9E BC 01    MOV [g_PreviousPairingStatus], eax
```

After:
```
0x0036b373: 90 90 90 90 90 90    NOP x6
```

## turnOnBdDrive sequence

```
0x0036b430: entry
0x0036b4e1: CALL sceKernelIccGetBDPowerState          (zLEuSU+hl-w#Z#X)
0x0036b4e6: TEST eax, eax
0x0036b4e8: JE   continue
    ...
0x0036b5ef: LEA  rdi, ["Skip turning on the BD drive because it is already"]
    ... (early exit if already on)
0x0036b628: CALL sceRtcGetCurrentTick                 (18B2NS1y9UU#Y#W)
0x0036b6c7: LEA  rdi, ["BD drive powered on. (start) startTime=..."]
0x0036b6d5: MOV  edi, 1                               ; arg0 = ON
0x0036b6da: CALL sceKernelIccControlBDPowerState      (1wxFFk3xIhA#Z#X)
0x0036b73a: CALL sceRtcGetCurrentTick                 ; end time
0x0036b7c6: LEA  rdi, ["BD drive powered on. (end) elapsetTime=..."]
0x0036b7dc: CALL __stack_chk_fail path                (Ou3iL1abvng#Z#X)
```

The `__stack_chk_fail` call is dead code in the normal path — it is only
reached if the stack canary is corrupted. `Ou3iL1abvng#Z#X` has 26,395 call
sites across ShellCore, matching the usual pattern for a canary-check helper.

## turnOffBdDrive relevant section

```
0x0036b920: entry
    ... check already-off ...
0x0036b9ef: CALL 0x0036bb00                           ; checkDddPairingStatus
0x0036b9f4: TEST eax, eax
0x0036b9f6: JE   0x0036ba23                           ; proceed to turn off
    ...
0x0036ba23: XOR  edi, edi                             ; arg0 = 0 (OFF)
0x0036ba25: CALL sceKernelIccControlBDPowerState
```

Patch 4 NOPs the `CALL` at `0x0036ba25` so even if the function is entered,
power is not switched off.

## State-name log strings (verified anchors)

| vaddr        | String |
|--------------|--------|
| `0x01c9d7a5` | `[DDD] sceSblDddGetPairingStatus() retuned %#x. pairingStatus = PAIRED, value = %i.` |
| `0x01d57732` | `... PAIRED_TO_ANOTHER ...` |
| `0x01c86497` | `... NOT_PAIRED ...` |
| `0x01d57791` | `... REGISTER_INCOMPLETE ...` |
| `0x01c4ae33` | `... DELETE_INCOMPLETE ...` |
| `0x01c32713` | `... SCE_SBL_DDD_STATUS_FACTORY_PAIRED ...` |
| `0x01c7aa54` | `... SCE_SBL_DDD_STATUS_FACTORY_PAIRED_TO_ANOTHER ...` |

Each one is referenced by exactly one `LEA rdi, [...]` instruction. Walking
back from the LEA finds the switch case the value belongs to.
