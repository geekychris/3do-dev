# Build rules for games on the amiga3d engine (sdk/amiga3d), ported from
# amiga_mcp/examples/<game>. A game's Makefile is:
#
#   NAME := block_breaker_3d
#   include ../../sdk/amiga3d/a3d.mk
#
# src/*.c        the game's sources (core, shell, rendering) + src/pad_3do.c
# src/tests/*.c  its test cases (run_tests), kept beside tests/vectors.h
#
# On top of sdk/amiga (the frame, pad, sound, files), this adds the engine
# (sdk/amiga3d/src, its headers in sdk/amiga3d/include) and main_3do.c.
#
#   make                         build/<NAME>.iso: the game
#   make A3D_TEST=1 BUILD=build/selftest
#                                build/selftest/<NAME>.iso: run_tests on the 3DO
#                                (TDO:PASS / TDO:FAIL lines; test.py does this)

A3D_DIR   := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
STACKSIZE ?= 32768
BUILD     ?= build

# C99 in the games: "static inline" helpers in headers
EXTRA_CFLAGS += -I$(A3D_DIR)/include -I$(A3D_DIR)/../amiga/src -Dinline=__inline
ifeq ($(A3D_TEST),1)
EXTRA_CFLAGS += -DA3D_TEST=1
endif

A3D_SRCS   := $(wildcard $(A3D_DIR)/src/*.c)
A3D_TSRCS  := $(wildcard src/tests/*.c)
A3D_OBJDIR := $(BUILD)/obj/a3d
EXTRA_OBJS += $(A3D_SRCS:$(A3D_DIR)/src/%.c=$(A3D_OBJDIR)/%.c.o) \
              $(A3D_TSRCS:src/tests/%.c=$(A3D_OBJDIR)/tests/%.c.o)

include $(A3D_DIR)/../amiga/amiga.mk

$(A3D_OBJDIR) $(A3D_OBJDIR)/tests:
	mkdir -p $@

$(A3D_OBJDIR)/%.c.o: $(A3D_DIR)/src/%.c | $(A3D_OBJDIR)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

$(A3D_OBJDIR)/tests/%.c.o: src/tests/%.c | $(A3D_OBJDIR)/tests
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

-include $(wildcard $(A3D_OBJDIR)/*.d $(A3D_OBJDIR)/tests/*.d)
