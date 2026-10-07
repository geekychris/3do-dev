#!/usr/bin/env bash
# One-shot setup for the 3DO dev environment. Safe to re-run.
#
#   ./setup.sh            everything, then build + test all projects
#   ./setup.sh --quick    skip the final build/test pass
#   ./setup.sh --no-bios  don't download BIOS ROMs (bring your own into ./bios)
#
# Requires: git, curl, make, a C compiler, Docker (any engine), Python >= 3.10
# (or uv). macOS (Apple Silicon / Intel) and Linux (x86_64 / arm64).
# Windows: use WSL2.
source "$(dirname "$0")/scripts/lib.sh"

QUICK=0; BIOS=1
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    --no-bios) BIOS=0 ;;
    -h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option $a" ;;
  esac
done

step "prerequisites ($(host_os)/$(host_arch))"
missing=()
for t in git curl make cc; do have "$t" || missing+=("$t"); done
if [ ${#missing[@]} -gt 0 ]; then
  if [ "$(host_os)" = darwin ]; then
    die "missing: ${missing[*]} – run: xcode-select --install"
  else
    die "missing: ${missing[*]} – e.g. sudo apt install git curl build-essential"
  fi
fi
have docker || die "Docker is required to run the devkit's x86 compilers. Install Docker Desktop, Rancher Desktop, OrbStack or colima (macOS) / docker-ce (Linux)."
ok "git, curl, make, cc, docker"

"$TDO_ROOT/scripts/fetch-devkit.sh"
"$TDO_ROOT/scripts/build-toolchain-image.sh"
"$TDO_ROOT/scripts/build-emulator.sh"
if [ "$BIOS" = 1 ]; then
  "$TDO_ROOT/scripts/fetch-bios.sh"
else
  [ -f "$BIOS_DIR/panafz10.bin" ] || warn "no $BIOS_DIR/panafz10.bin – put a 3DO BIOS there before running anything"
fi
"$TDO_ROOT/scripts/setup-python.sh"

if [ "$QUICK" = 0 ]; then
  step "build + test all projects"
  "$TDO_ROOT/3do" selftest
fi

cat <<EOF

${_c_g}3DO dev environment ready.${_c_0}

  ./3do run demo           play the demo (arrows, Z/X/C = A/B/C, Enter = P, Backspace = X)
  ./3do new mygame         start a project from the template
  ./3do build mygame       compile -> projects/mygame/build/mygame.iso
  ./3do test unittest      headless on-target tests
  ./3do doctor             diagnose problems

Claude Code: open this directory with \`claude\`; the "3do" MCP server in .mcp.json
gives Claude build/boot/screenshot/input/memory tools (see CLAUDE.md).
EOF
