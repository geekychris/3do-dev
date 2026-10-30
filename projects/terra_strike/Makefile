# Terra Strike on the 3DO: amiga_mcp/examples/terra_strike (its own engine)
# on sdk/amiga, with sdk/amiga3d's headers (stdint.h, the bridge API) and
# 32-bit arithmetic (a3d_i64.c).
#   make                                   build/terra_strike.iso
#   make TS_TEST=1 BUILD=build/selftest    the unit tests + the scripted flight on the 3DO
NAME := terra_strike
BUILD ?= build
A3D := ../../sdk/amiga3d
STACKSIZE := 32768

EXTRA_CFLAGS += -I$(A3D)/include -I../../sdk/amiga/src -Dinline=__inline
ifeq ($(TS_TEST),1)
EXTRA_CFLAGS += -DTS_TEST=1
endif
EXTRA_OBJS += $(BUILD)/obj/a3d/a3d_i64.c.o $(BUILD)/obj/a3d/a3d_libc.c.o \
              $(BUILD)/obj/tests/test_cases.c.o $(BUILD)/obj/tests/trace.c.o

include ../../sdk/amiga/amiga.mk

$(BUILD)/obj/a3d $(BUILD)/obj/tests:
	mkdir -p $@

$(BUILD)/obj/a3d/%.c.o: $(A3D)/src/%.c | $(BUILD)/obj/a3d
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/tests/%.c.o: src/tests/%.c | $(BUILD)/obj/tests
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

-include $(wildcard $(BUILD)/obj/tests/*.d)
