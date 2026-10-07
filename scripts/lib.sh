# Shared helpers for 3do_dev scripts. Source this; don't run it.
# shellcheck shell=bash

set -euo pipefail

TDO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TDO_ROOT

# ---- pinned upstream versions (bump deliberately, then re-run setup) ----
DEVKIT_REPO="https://github.com/trapexit/3do-devkit.git"
DEVKIT_COMMIT="4e370299bd273bb13089d41fa33c5e863dc6d405"

OPERA_REPO="https://github.com/libretro/opera-libretro.git"
OPERA_COMMIT="ea60917d7f8f79a19b46e1d8ecc6eca7be589b6a"

TOOLCHAIN_IMAGE="3do-dev-toolchain:1"

THIRD_PARTY="$TDO_ROOT/third_party"
DEVKIT_DIR="$THIRD_PARTY/3do-devkit"
OPERA_DIR="$THIRD_PARTY/opera-libretro"
BIOS_DIR="${TDO_BIOS_DIR:-$TDO_ROOT/bios}"

if [ -t 1 ]; then
  _c_b=$'\033[1m'; _c_g=$'\033[32m'; _c_y=$'\033[33m'; _c_r=$'\033[31m'; _c_0=$'\033[0m'
else
  _c_b=""; _c_g=""; _c_y=""; _c_r=""; _c_0=""
fi

step() { printf '%s==> %s%s\n' "$_c_b" "$*" "$_c_0"; }
ok()   { printf '%s  ✓ %s%s\n' "$_c_g" "$*" "$_c_0"; }
warn() { printf '%s  ! %s%s\n' "$_c_y" "$*" "$_c_0" >&2; }
die()  { printf '%s  ✗ %s%s\n' "$_c_r" "$*" "$_c_0" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

host_os()   { uname -s | tr '[:upper:]' '[:lower:]'; }   # darwin | linux
host_arch() { uname -m; }                                  # arm64 | x86_64 | aarch64

core_ext() { [ "$(host_os)" = darwin ] && echo dylib || echo so; }
CORE_PATH="$TDO_ROOT/emulator/build/opera_libretro.$(core_ext)"

md5_of() {
  if have md5sum; then md5sum "$1" | awk '{print $1}'
  else md5 -q "$1"; fi
}

ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

# Clone `repo` into `dir` and check out exactly `commit` (shallow).
git_checkout_pinned() {
  local repo="$1" dir="$2" commit="$3"
  if [ ! -d "$dir/.git" ]; then
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q -f FETCH_HEAD
  fi
}
