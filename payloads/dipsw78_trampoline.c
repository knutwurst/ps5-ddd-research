/*
 * ps5-ddd-research: dipsw 78 trampoline
 *
 * Overwrites the prologue of sceKernelCheckDipsw inside libSceDipsw.sprx with
 * a short trampoline that returns 1 for index 78 (SCE_KERNEL_DIPSW_ENABLE_DDD_EMU)
 * and falls through for every other index.
 *
 * Because libSceDipsw.sprx is mapped as shared memory, patching the function
 * in one process patches it for every other process that imports it.
 *
 * Trampoline:
 *   83 FF 4E        CMP edi, 78
 *   75 06           JNE +6
 *   B8 01 00 00 00  MOV eax, 1
 *   C3              RET
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <ps5/kernel.h>

/* sceKernelCheckDipsw */
static const char CHECK_DIPSW_NID[] = "JSNgID8Bupk";

static const uint8_t TRAMPOLINE[11] = {
    0x83, 0xFF, 0x4E,
    0x75, 0x06,
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xC3
};

static void escalate(void) {
    pid_t pid = getpid();
    kernel_set_ucred_authid(pid, 0x4800000000000010ULL);
    uint8_t caps[16];
    kernel_get_ucred_caps(pid, caps);
    caps[7] |= 0x40;
    kernel_set_ucred_caps(pid, caps);
    setuid(0);
}

static int patch_in_process(FILE *f, pid_t pid) {
    uint32_t handle = 0;
    const char *lib_names[] = {
        "libSceDipsw.sprx", "libSceDipsw.so", NULL
    };

    for (int i = 0; lib_names[i]; i++) {
        if (kernel_dynlib_handle(pid, lib_names[i], &handle) == 0) {
            break;
        }
    }

    if (handle == 0) {
        for (uint32_t h = 0; h < 256; h++) {
            intptr_t base = kernel_dynlib_mapbase_addr(pid, h);
            if (base == 0 || base == -1) continue;
            intptr_t addr = kernel_dynlib_resolve(pid, h, CHECK_DIPSW_NID);
            if (addr) {
                handle = h;
                break;
            }
        }
        if (handle == 0) return 0;
    }

    intptr_t addr = kernel_dynlib_resolve(pid, handle, CHECK_DIPSW_NID);
    if (!addr) {
        fprintf(f, "  pid %d: cannot resolve NID\n", pid);
        return 0;
    }

    uint8_t first;
    kernel_proc_copyout(pid, addr, &first, 1);
    if (first == TRAMPOLINE[0]) {
        fprintf(f, "  pid %d: already patched\n", pid);
        return 1;
    }

    int ret = kernel_proc_copyin(pid, TRAMPOLINE, addr, sizeof(TRAMPOLINE));
    if (ret != 0) {
        intptr_t page = addr & ~0xFFFL;
        kernel_mprotect(pid, page, 0x1000, 7);
        ret = kernel_proc_copyin(pid, TRAMPOLINE, addr, sizeof(TRAMPOLINE));
    }

    uint8_t verify[11];
    kernel_proc_copyout(pid, addr, verify, sizeof(verify));
    int ok = memcmp(verify, TRAMPOLINE, sizeof(TRAMPOLINE)) == 0;
    fprintf(f, "  pid %d addr=0x%lx: %s\n",
            pid, (unsigned long)addr, ok ? "ok" : "failed");
    return ok;
}

int main(void) {
    escalate();

    FILE *f = fopen("/data/dipsw78_trampoline.log", "w");
    if (!f) return 1;

    fprintf(f, "dipsw 78 trampoline\n\n");

    for (pid_t pid = 1; pid < 300; pid++) {
        intptr_t proc = kernel_get_proc(pid);
        if (!proc || proc == -1) continue;
        patch_in_process(f, pid);
    }

    fclose(f);
    return 0;
}
