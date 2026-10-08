# 3DO ARCADE: one disc with a menu (this project) and every ported game.
#
# A project joins the arcade by having an arcade.txt:
#
#   title=Rock Blaster
#   origin=Amiga          (where the port came from; docs only)
#   genre=Shooter         (shown beside the title in the menu)
#   order=10
#   desc=Asteroids-style vector shooter: rotate, thrust and blast the rocks.
#
# Its executable goes on the disc as $boot/<name>/<name>; files under the
# project's takeme/<name>/ go next to it (games read them through
# amiga_set_progdir("<name>")).

NAME := arcade

ARCADE_TXT := $(sort $(wildcard ../*/arcade.txt))
GAMES      := $(foreach f,$(ARCADE_TXT),$(notdir $(patsubst %/,%,$(dir $(f)))))
GEN        := build/gen/games_gen.h

EXTRA_CFLAGS    += -Ibuild/gen
EXTRA_DISC_DEPS += build/disc/.games

include ../../sdk/amiga/amiga.mk

$(OBJDIR)/main.c.o: $(GEN)

# games_gen.h: one table entry per arcade.txt, sorted by order= then name
$(GEN): $(ARCADE_TXT) Makefile
	mkdir -p $(dir $@)
	{ echo "/* generated from each project's arcade.txt by projects/arcade/Makefile */"; \
	  echo "typedef struct { const char *name, *title, *genre, *desc; } ArcadeGame;"; \
	  echo "static const ArcadeGame GAMES[] = {"; \
	  for f in $(ARCADE_TXT); do \
	    g=$$(basename $$(dirname $$f)); \
	    awk -F= -v g=$$g ' \
	      /^title=/ {t=substr($$0,7)} /^genre=/ {o=substr($$0,7)} \
	      /^order=/ {n=substr($$0,7)} /^desc=/ {d=substr($$0,6)} \
	      END { gsub(/"/,"\\\"",t); gsub(/"/,"\\\"",d); if(n=="") n=500; if(o=="") o="GAME"; \
	            printf "%05d\t    { \"%s\", \"%s\", \"%s\", \"%s\" },\n", n, g, toupper(t), toupper(o), toupper(d) }' $$f; \
	  done | sort | cut -f2-; \
	  echo "    { 0, 0, 0, 0 }"; \
	  echo "};"; \
	  echo "#define NUM_GAMES ((int)(sizeof(GAMES) / sizeof(GAMES[0])) - 1)"; \
	} > $@

# Build every game (incrementally) and copy it onto this disc.
build/disc/.games: FORCE | build/disc/.base
	@set -e; for g in $(GAMES); do \
	  echo "arcade: $$g"; \
	  $(MAKE) --no-print-directory -C ../$$g > build/game-$$g.log 2>&1 || { tail -20 build/game-$$g.log; exit 1; }; \
	  mkdir -p build/disc/$$g; \
	  cp ../$$g/build/disc/LaunchMe build/disc/$$g/$$g; \
	  if [ -d ../$$g/takeme/$$g ]; then cp -R ../$$g/takeme/$$g/. build/disc/$$g/; fi; \
	done
	touch $@

.PHONY: FORCE
FORCE:
