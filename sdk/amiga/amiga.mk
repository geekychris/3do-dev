# Build rules for Amiga games ported to the 3DO with the compatibility layer.
#
#   NAME := rock_blaster
#   include ../../sdk/amiga/amiga.mk
#
# Adds the Amiga shim headers to the include path and links the layer
# (sdk/amiga/src/*.c). Game-local headers in src/ win over the shims, so a
# game can override any shim header by providing its own copy.

AMIGA_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
STACKSIZE ?= 16384

AMIGA_SRCS := $(wildcard $(AMIGA_DIR)/src/*.c)
EXTRA_OBJS += $(AMIGA_SRCS:$(AMIGA_DIR)/src/%.c=build/obj/amiga/%.c.o)
# -J: <stdio.h> etc. must resolve to the 3DO SDK headers (matching its libc),
# not to Norcroft's built-in ANSI headers.
EXTRA_CFLAGS += -I$(AMIGA_DIR)/include -J$(DEVKIT)/include/3do -DAMIGA3DO=1

include $(AMIGA_DIR)/../project.mk

build/obj/amiga:
	mkdir -p $@

build/obj/amiga/%.c.o: $(AMIGA_DIR)/src/%.c | build/obj/amiga
	armcc $(INCFLAGS) -I$(AMIGA_DIR)/include -I$(AMIGA_DIR)/src $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) -I$(AMIGA_DIR)/include -I$(AMIGA_DIR)/src $(DEFFLAGS) $(CFLAGS) -c $< -o $@

-include $(wildcard build/obj/amiga/*.d)
