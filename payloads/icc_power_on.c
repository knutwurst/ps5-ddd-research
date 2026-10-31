/*
 * ps5-ddd-research: direct ICC BD-drive power on
 *
 * Resolves sceKernelIccControlBDPowerState via its NID and calls it with 1.
 * Also reads back the pre- and post-state via sceKernelIccGetBDPowerState.
 *
 *   sceKernelIccGetBDPowerState     — NID zLEuSU+hl-w#Z#X
 *   sceKernelIccControlBDPowerState — NID 1wxFFk3xIhA#Z#X
 *
 * Note: on firmware where the DDD state is not PAIRED (0) or FACTORY_PAIRED (5),
 * the ICC subsystem accepts Set(1) and returns 0, but does not actually route
 * power to the drive.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <ps5/kernel.h>

static const char GET_NID[] = "zLEuSU+hl-w";
static const char SET_NID[] = "1wxFFk3xIhA";

typedef int (*icc_get_fn)(uint8_t *state_out);
typedef int (*icc_set_fn)(int state);

static void escalate(void) {
    pid_t pid = getpid();
    kernel_set_ucred_authid(pid, 0x4800000000000010ULL);
    uint8_t caps[16];
    kernel_get_ucred_caps(pid, caps);
    caps[7] |= 0x40;
    kernel_set_ucred_caps(pid, caps);
    setuid(0);
}

static int resolve_funcs(pid_t pid, icc_get_fn *get, icc_set_fn *set) {
    for (uint32_t h = 0; h < 256; h++) {
        intptr_t base = kernel_dynlib_mapbase_addr(pid, h);
        if (base == 0 || base == -1) continue;
        intptr_t s = kernel_dynlib_resolve(pid, h, SET_NID);
        if (s) {
            *set = (icc_set_fn)s;
            *get = (icc_get_fn)kernel_dynlib_resolve(pid, h, GET_NID);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    escalate();

    FILE *f = fopen("/data/icc_power_on.log", "w");
    if (!f) return 1;

    fprintf(f, "ICC BD-drive direct power on\n\n");

    icc_get_fn Get = NULL;
    icc_set_fn Set = NULL;

    if (!resolve_funcs(getpid(), &Get, &Set)) {
        fprintf(f, "cannot resolve ICC NIDs in own process\n");
        fclose(f);
        return 1;
    }

    fprintf(f, "Get=%p Set=%p\n", (void*)Get, (void*)Set);

    uint8_t before = 0xFF;
    int gret = Get(&before);
    fprintf(f, "Get before: ret=%d state=0x%02x\n", gret, before);

    int sret = Set(1);
    fprintf(f, "Set(1): ret=0x%x\n", sret);

    sleep(1);

    uint8_t after = 0xFF;
    gret = Get(&after);
    fprintf(f, "Get after:  ret=%d state=0x%02x\n", gret, after);

    if (after != before) {
        fprintf(f, "state changed\n");
    } else {
        fprintf(f, "state unchanged — ICC accepted and discarded\n");
    }

    fclose(f);
    return 0;
}
