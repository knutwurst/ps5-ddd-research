PS5_PAYLOAD_SDK ?= $(shell echo $$PS5_PAYLOAD_SDK)
PS5_HOST        ?= 192.168.0.0
PS5_PORT        ?= 9021

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS := -Wall -Werror -g -O2
LDLIBS := -lkernel_sys

PAYLOADS := shellcore_patch.elf dipsw78_trampoline.elf icc_power_on.elf

all: $(PAYLOADS)

%.elf: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

deploy-%: %.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

clean:
	rm -f $(PAYLOADS)

.PHONY: all clean
