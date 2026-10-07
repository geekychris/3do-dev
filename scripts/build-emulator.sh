#!/usr/bin/env bash
# Build the Opera libretro core (3DO emulator) natively, with the tdo
# harness extension applied (debug console capture, SWI trace, tracepoints,
# memory/register access). Output: emulator/build/opera_libretro.{dylib,so}
source "$(dirname "$0")/lib.sh"

step "opera-libretro @ ${OPERA_COMMIT:0:10} + tdo harness"
have git  || die "git is required"
have make || die "make is required (macOS: xcode-select --install; Linux: build-essential)"
have cc   || die "a C compiler is required (macOS: xcode-select --install; Linux: build-essential)"

git_checkout_pinned "$OPERA_REPO" "$OPERA_DIR" "$OPERA_COMMIT"

# Always start from a pristine tree so patches apply cleanly and repeatably.
git -C "$OPERA_DIR" reset -q --hard
git -C "$OPERA_DIR" clean -q -fdx
cp "$TDO_ROOT"/emulator/ext/opera_harness.[ch] "$OPERA_DIR/libopera/"
for p in "$TDO_ROOT"/emulator/patches/*.patch; do
  git -C "$OPERA_DIR" apply --whitespace=nowarn "$p" || die "patch failed: $(basename "$p")"
  ok "applied $(basename "$p")"
done

make -C "$OPERA_DIR" -j"$(ncpu)" >"$OPERA_DIR/build.log" 2>&1 \
  || { tail -30 "$OPERA_DIR/build.log"; die "core build failed (full log: $OPERA_DIR/build.log)"; }

mkdir -p "$(dirname "$CORE_PATH")"
# Install via rename, never overwrite in place: on macOS rewriting a dylib
# that a running emulator has mapped invalidates its code signature and new
# processes loading it get killed.
cp "$OPERA_DIR/opera_libretro.$(core_ext)" "$CORE_PATH.tmp"
mv -f "$CORE_PATH.tmp" "$CORE_PATH"
ok "built $CORE_PATH"
