#!/usr/bin/env bash
# Check every piece of the environment and say how to fix what's broken.
source "$(dirname "$0")/lib.sh"
set +e
bad=0
pass() { ok "$1"; }
fail() { printf '%s  ✗ %s%s\n    fix: %s\n' "$_c_r" "$1" "$_c_0" "$2"; bad=$((bad+1)); }

step "3DO dev doctor ($(host_os)/$(host_arch))"

for t in git curl make cc; do
  have "$t" && pass "$t" || fail "$t missing" "install it (macOS: xcode-select --install)"
done

if [ "$(git -C "$DEVKIT_DIR" rev-parse HEAD 2>/dev/null)" = "$DEVKIT_COMMIT" ]; then
  pass "3do-devkit @ ${DEVKIT_COMMIT:0:10}"
else
  fail "3do-devkit missing or not at pinned commit" "./scripts/fetch-devkit.sh"
fi

if ! have docker; then
  fail "docker not installed" "install Docker Desktop / Rancher Desktop / OrbStack / colima"
elif ! docker info >/dev/null 2>&1; then
  fail "docker daemon not running" "start your Docker app / colima start"
elif ! docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1; then
  fail "toolchain image $TOOLCHAIN_IMAGE missing" "./scripts/build-toolchain-image.sh"
elif ! docker run --rm --platform linux/amd64 -v "$DEVKIT_DIR:/work/third_party/3do-devkit:ro" \
       "$TOOLCHAIN_IMAGE" sh -c 'armcc 2>&1 | grep -q Norcroft && 3dt --help >/dev/null 2>&1'; then
  fail "devkit compilers don't execute in Docker (x86 emulation lost after VM restart?)" "./scripts/build-toolchain-image.sh"
else
  pass "toolchain container runs armcc + 3dt"
fi

if [ -f "$CORE_PATH" ]; then
  if nm -g "$CORE_PATH" 2>/dev/null | grep -q tdo_debug_read; then
    pass "Opera core with tdo harness ($(basename "$CORE_PATH"))"
  else
    fail "Opera core lacks the tdo harness" "./scripts/build-emulator.sh"
  fi
else
  fail "Opera core not built" "./scripts/build-emulator.sh"
fi

if [ -f "$BIOS_DIR/panafz10.bin" ] && [ "$(md5_of "$BIOS_DIR/panafz10.bin")" = 51f2f43ae2f3508a14d9f56597e2d3ce ]; then
  pass "BIOS panafz10.bin"
else
  fail "BIOS panafz10.bin missing or wrong" "./scripts/fetch-bios.sh"
fi

if "$TDO_ROOT/.venv/bin/python" -c "import tdo.core, numpy, PIL, pygame, mcp" >/dev/null 2>&1; then
  pass "python env (.venv) with tdo"
  if "$TDO_ROOT/.venv/bin/tdo" info >/dev/null 2>&1; then
    pass "emulator loads (tdo info)"
  else
    fail "tdo info failed" ".venv/bin/tdo info  # to see the error"
  fi
else
  fail "python env missing/broken" "./scripts/setup-python.sh"
fi

if [ -f "$TDO_ROOT/.mcp.json" ]; then pass ".mcp.json registers the 3do MCP server"; fi

gdb_bin=""
for g in "${TDO_GDB:-}" arm-none-eabi-gdb gdb-multiarch gdb; do
  if [ -n "$g" ] && have "$g"; then gdb_bin="$g"; break; fi
done
if [ -n "$gdb_bin" ] && "$gdb_bin" -batch -ex "set architecture arm" >/dev/null 2>&1; then
  pass "gdb with ARM support ($gdb_bin) – optional, for ./3do gdb"
else
  warn "no ARM-capable gdb (optional): brew install gdb  /  apt install gdb-multiarch"
fi

echo
if [ "$bad" -eq 0 ]; then ok "all good"; else die "$bad problem(s)"; fi
