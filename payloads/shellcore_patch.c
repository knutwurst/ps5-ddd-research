/*
 * ps5-ddd-research: ShellCore state-cache patcher
 *
 * Applies four patches to the running SceShellCore process so that every
 * pairing-state query returns PAIRED (0) regardless of the real kernel state.
 *
 *   1. checkDddPairingStatus switch dispatch @ vaddr 0x0036bfc4
 *        MOV eax, [rbp-0x1bc]   ->   XOR eax, eax; NOP x4
 *   2. g_PreviousPairingStatus cache-write @ vaddr 0x0036b373
 *        MOV [g_PreviousPairingStatus], eax   ->   NOP x6
 *   3. g_PreviousPairingStatus initial value @ vaddr 0x01f35260
 *        write uint32 0
 *   4. turnOffBdDrive CALL @ vaddr 0x0036ba25
 *        CALL sceKernelIccControlBDPowerState   ->   NOP x5
 *
 * Build against ps5-payload-sdk; link with -lkernel_sys.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define PATCH_CHECK_VADDR    0x0036bfc4
#define PATCH_CACHE_VADDR    0x0036b373
#define PATCH_TURNOFF_VADDR  0x0036ba25
#define G_PREV_PAIR_VADDR    0x01f35260

static const uint8_t CHECK_SIG[6]   = { 0x8B, 0x85, 0x44, 0xFE, 0xFF, 0xFF };
static const uint8_t CHECK_PATCH[6] = { 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90 };

static const uint8_t CACHE_SIG[6]   = { 0x89, 0x05, 0xE7, 0x9E, 0xBC, 0x01 };
static const uint8_t CACHE_PATCH[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

static const uint8_t TURNOFF_SIG_BYTE  = 0xE8;
static const uint8_t TURNOFF_PATCH[5]  = { 0x90, 0x90, 0x90, 0x90, 0x90 };

static void escalate(void) {
    pid_t pid = getpid();
    kernel_set_ucred_authid(pid, 0x4800000000000010ULL);
    uint8_t caps[16];
    kernel_get_ucred_caps(pid, caps);
    caps[7] |= 0x40;
    kernel_set_ucred_caps(pid, caps);
    setuid(0);
}

static int find_shellcore(FILE *f, pid_t *out_pid, intptr_t *out_base) {
    uint8_t probe[6];
    for (pid_t pid = 1; pid < 300; pid++) {
        intptr_t proc = kernel_get_proc(pid);
        if (!proc || proc == -1) continue;
        for (uint32_t h = 0; h < 256; h++) {
            intptr_t base = kernel_dynlib_mapbase_addr(pid, h);
            if (base == 0 || base == -1) continue;
            if (kernel_proc_copyout(pid, base + PATCH_CHECK_VADDR, probe, 6) != 0)
                continue;
            if (memcmp(probe, CHECK_SIG, 6) == 0 ||
                memcmp(probe, CHECK_PATCH, 6) == 0) {
                fprintf(f, "ShellCore pid=%d base=0x%lx\n",
                        pid, (unsigned long)base);
                *out_pid = pid;
                *out_base = base;
                return 1;
            }
        }
    }
    return 0;
}

static int patch(FILE *f, pid_t pid, intptr_t addr, const char *label,
                 const uint8_t *bytes, size_t len) {
    uint8_t before[16], after[16];
    kernel_proc_copyout(pid, addr, before, len);
    if (memcmp(before, bytes, len) == 0) {
        fprintf(f, "  %s @ 0x%lx: already patched\n",
                label, (unsigned long)addr);
        return 1;
    }
    kernel_proc_copyin(pid, bytes, addr, len);
    kernel_proc_copyout(pid, addr, after, len);
    if (memcmp(after, bytes, len) != 0) {
        intptr_t page = addr & ~0xFFFL;
        kernel_mprotect(pid, page, 0x1000, 7);
        kernel_proc_copyin(pid, bytes, addr, len);
        kernel_proc_copyout(pid, addr, after, len);
    }
    int ok = memcmp(after, bytes, len) == 0;
    fprintf(f, "  %s @ 0x%lx: %s\n", label, (unsigned long)addr,
            ok ? "ok" : "failed");
    return ok;
}

int main(void) {
    escalate();

    FILE *f = fopen("/data/shellcore_patch.log", "w");
    if (!f) return 1;

    fprintf(f, "ShellCore DDD state-cache patcher\n\n");

    pid_t pid = -1;
    intptr_t base = 0;
    if (!find_shellcore(f, &pid, &base)) {
        fprintf(f, "ShellCore not found\n");
        fclose(f);
        return 1;
    }

    patch(f, pid, base + PATCH_CHECK_VADDR,
          "checkDddPairingStatus switch", CHECK_PATCH, 6);
    patch(f, pid, base + PATCH_CACHE_VADDR,
          "cache-write NOP", CACHE_PATCH, 6);

    uint32_t zero = 0;
    kernel_proc_copyin(pid, &zero, base + G_PREV_PAIR_VADDR, 4);
    fprintf(f, "  g_PreviousPairingStatus @ 0x%lx: set to 0\n",
            (unsigned long)(base + G_PREV_PAIR_VADDR));

    uint8_t probe[5];
    kernel_proc_copyout(pid, base + PATCH_TURNOFF_VADDR, probe, 5);
    if (probe[0] == TURNOFF_SIG_BYTE || probe[0] == 0x90) {
        patch(f, pid, base + PATCH_TURNOFF_VADDR,
              "turnOffBdDrive CALL NOP", TURNOFF_PATCH, 5);
    } else {
        fprintf(f, "  turnOffBdDrive CALL: unexpected first byte 0x%02x\n",
                probe[0]);
    }

    fclose(f);
    return 0;
}
