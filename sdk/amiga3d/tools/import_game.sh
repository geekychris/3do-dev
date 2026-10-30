#!/usr/bin/env bash
# import_game.sh <game> - copy an amiga3d game from amiga_mcp/examples into
# projects/<game> (src/, src/tests/, Makefile) and run c89init.py over it.
# The amiga_mcp checkout is ../amiga_mcp next to this repo, or $AMIGA_MCP.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
g="$1"
src="${AMIGA_MCP:-$root/../amiga_mcp}/examples/$g"
dst="$root/projects/$g"
[ -d "$src" ] || { echo "no $src"; exit 1; }
mkdir -p "$dst/src/tests" "$dst/screenshots"
for f in "$src"/*.c "$src"/*.h; do cp "$f" "$dst/src/"; done
for f in "$src"/tests/*.c "$src"/tests/*.h; do [ -e "$f" ] && cp "$f" "$dst/src/tests/"; done
[ -f "$dst/Makefile" ] || printf 'NAME := %s\ninclude ../../sdk/amiga3d/a3d.mk\n' "$g" > "$dst/Makefile"
python3 "$here/c89init.py" "$dst"/src/*.c "$dst"/src/tests/*.c
echo "imported $g -> projects/$g (add src/pad_3do.c)"
