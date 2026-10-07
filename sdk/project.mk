# Shared build rules for 3DO projects in this repo.
#
# A project Makefile is just:
#
#   NAME := mygame
#   include ../../sdk/project.mk
#
# Layout (all optional except src/):
#
#   src/*.c *.cpp *.s     -> compiled and linked into the disc's LaunchMe
#   takeme/...            -> copied verbatim onto the disc root (data files)
#   assets/*.png          -> converted to coded 16bpp cels at <disc>/cels/*.cel
#   banner.png            -> converted to the disc's BannerScreen (boot splash)
#
# sdk/src/*.c (tdo_log, on-target test helpers; see sdk/include/tdo.h) is
# compiled into every project automatically.
#
# Outputs:  build/<NAME>.iso   (signed; boots on the default retail BIOS)
#           build/<NAME>.elf   same code + DWARF debug info; addresses are
#                              offsets from the AIF load address
#                              (for gdb / symbolization; see ./3do gdb)
#           build/<NAME>.sym   armlink symbol table (name -> offset)
#
# Overridable: STACKSIZE, DEBUG=1, EXTRA_CFLAGS, EXTRA_LIBS, EXTRA_OBJS
#
# Run via bin/3do-make (or ./3do build), which supplies the toolchain.

ifndef NAME
$(error set NAME before including project.mk)
endif

SDK_DIR         := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
TDO_ROOT        ?= $(abspath $(SDK_DIR)/..)
TDO_DEVKIT_PATH ?= $(TDO_ROOT)/third_party/3do-devkit
DEVKIT          := $(TDO_DEVKIT_PATH)

SRC_DIR   ?= src
DATA_DIR  ?= takeme
ASSET_DIR ?= assets
BUILD     ?= build
STACKSIZE ?= 8192
BANNER    ?= $(wildcard banner.png)

DISC     := $(BUILD)/disc
OBJDIR   := $(BUILD)/obj
LAUNCHME := $(DISC)/LaunchMe
ISO      := $(BUILD)/$(NAME).iso
ELF      := $(BUILD)/$(NAME).elf
SYM      := $(BUILD)/$(NAME).sym

# ---- flags: identical to the 3do-devkit defaults (see its Makefile for notes)
ifeq ($(DEBUG),1)
OPT      = -O0
DEFFLAGS = -DDEBUG=1
else
OPT      = -O2 -zpno_check_stack
DEFFLAGS = -DNDEBUG=1
endif

INCFLAGS = -I$(SRC_DIR) -I$(SDK_DIR)/include -I$(DEVKIT)/include/3do -I$(DEVKIT)/include/community -I$(DEVKIT)/include/ttl
# -g only adds debug tables to the objects; the disc's AIF is linked -nodebug.
CFLAGS   = -g $(OPT) -bigend -za1 -zi4 -fa -fh -fx -fpu none -arch 3 -apcs "3/32/nofp/swst/wide/softfp" $(EXTRA_CFLAGS)
CXXFLAGS = $(CFLAGS)
ASFLAGS  = -bigend -fpu none -arch 3 -apcs "3/32/nofp/swst"
LDCOMMON = -match 0x1 -noscanlib -nozeropad -remove -dupok
LDFLAGS  = $(LDCOMMON) -ro-base 0 -nodebug -aif -reloc
# The AIF has a 0x80-byte header before the code, so linking the ELF at 0x80
# makes ELF addresses == offsets from the AIF load address in guest memory.
ELFFLAGS = $(LDCOMMON) -ro-base 0x80 -debug -elf
STARTUP  = $(DEVKIT)/lib/3do/cstartup.o

L := $(DEVKIT)/lib
LIBS = $(EXTRA_LIBS) \
  $(L)/3do/3dlib.lib $(L)/3do/audio.lib $(L)/3do/codec.lib $(L)/3do/compression.lib \
  $(L)/3do/cpluslib.lib $(L)/3do/DataAcq.lib $(L)/3do/DataAcqShuttle.lib $(L)/3do/DS.lib \
  $(L)/3do/DSShuttle.lib $(L)/3do/exampleslib.lib $(L)/3do/filesystem.lib \
  $(L)/3do/graphics.lib $(L)/3do/input.lib $(L)/3do/international.lib $(L)/3do/intmath.lib \
  $(L)/3do/lib3do.lib $(L)/3do/music.lib $(L)/3do/mvelib.lib $(L)/3do/operamath.lib \
  $(L)/3do/pgl.lib $(L)/3do/string.lib $(L)/3do/Subscriber.lib $(L)/3do/swi.lib \
  $(L)/community/cpplib.lib $(L)/community/svc_funcs.lib $(L)/community/libc.lib

# ---- sources
SRCS_C   := $(wildcard $(SRC_DIR)/*.c)
SRCS_CXX := $(wildcard $(SRC_DIR)/*.cpp)
SRCS_S   := $(wildcard $(SRC_DIR)/*.s)
SDK_SRCS := $(wildcard $(SDK_DIR)/src/*.c)
OBJS := $(SRCS_S:$(SRC_DIR)/%.s=$(OBJDIR)/%.s.o) \
        $(SRCS_C:$(SRC_DIR)/%.c=$(OBJDIR)/%.c.o) \
        $(SRCS_CXX:$(SRC_DIR)/%.cpp=$(OBJDIR)/%.cpp.o) \
        $(SDK_SRCS:$(SDK_DIR)/src/%.c=$(OBJDIR)/sdk/%.c.o) \
        $(EXTRA_OBJS)

rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
DATA_FILES  := $(call rwildcard,$(DATA_DIR)/,*)
ASSET_PNGS  := $(wildcard $(ASSET_DIR)/*.png)
ASSET_CELS  := $(ASSET_PNGS:$(ASSET_DIR)/%.png=$(DISC)/cels/%.cel)

# Base disc filesystem from the devkit (Portfolio 2.5 System/ + boot files).
BASE_FILES := AppStartup BannerScreen rom_tags signatures System

.DEFAULT_GOAL := all
all: $(ISO) $(ELF)
	@echo "built $(ISO)"

$(OBJDIR) $(OBJDIR)/sdk $(DISC) $(DISC)/cels:
	mkdir -p $@

$(DISC)/.base: | $(DISC)
	cd $(DEVKIT)/takeme && cp -R $(BASE_FILES) $(abspath $(DISC))/
	touch $@

$(DISC)/.data: $(DATA_FILES) $(DISC)/.base
	if [ -d "$(DATA_DIR)" ]; then cp -R $(DATA_DIR)/. $(DISC)/; fi
	touch $@

$(DISC)/cels/%.cel: $(ASSET_DIR)/%.png | $(DISC)/cels
	3it to-cel --bpp=16 --coded=false -o $@ $<

ifneq ($(BANNER),)
$(DISC)/.banner: $(BANNER) $(DISC)/.base
	3it to-banner -o $(DISC)/BannerScreen $(BANNER)
	touch $@
BANNER_STAMP := $(DISC)/.banner
endif

$(LAUNCHME): $(OBJS) | $(DISC)/.base
	armlink -o $@ $(LDFLAGS) -symbols $(BUILD)/aif.sym $(STARTUP) $(LIBS) $(OBJS)
	modbin --name="$(NAME)" --time --stack=$(STACKSIZE) $@ $@

# Same objects, same order, same base: symbol offsets match the AIF.
$(ELF): $(OBJS)
	armlink -o $@ $(ELFFLAGS) -symbols $(SYM) $(STARTUP) $(LIBS) $(OBJS)

$(ISO): $(LAUNCHME) $(DISC)/.data $(ASSET_CELS) $(BANNER_STAMP)
	3dt pack $(DISC) -o $@

$(OBJDIR)/%.c.o: $(SRC_DIR)/%.c | $(OBJDIR)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/sdk/%.c.o: $(SDK_DIR)/src/%.c | $(OBJDIR)/sdk
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcc $(INCFLAGS) $(DEFFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.cpp.o: $(SRC_DIR)/%.cpp | $(OBJDIR)
	armcpp $(INCFLAGS) $(DEFFLAGS) $(CXXFLAGS) -M $< -o $@ > $(@:.o=.d)
	armcpp $(INCFLAGS) $(DEFFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.s.o: $(SRC_DIR)/%.s | $(OBJDIR)
	armasm $(INCFLAGS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
-include $(wildcard $(OBJDIR)/*.d $(OBJDIR)/sdk/*.d)
