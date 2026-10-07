#!/usr/bin/env bash
#
# 3do-dev — one-shot installer.
#
# Clones (or updates) the repo, checks prerequisites, then runs ./setup.sh,
# which fetches the 3DO devkit, builds the toolchain container and the
# emulator, downloads the BIOS, creates the Python env and runs the tests.
# Re-running is safe: the checkout is updated and every step is idempotent.
#
#   curl -fsSL https://raw.githubusercontent.com/geekychris/3do-dev/main/scripts/install.sh | bash
#
# Works on macOS (Apple Silicon / Intel) and Linux (x86_64 / arm64).
# Windows: run it inside WSL2 (Ubuntu).
#
# Environment knobs:
#   TDO_DEV_SRC           install dir                   (default: $HOME/3do-dev)
#   TDO_DEV_REF           git branch/tag/commit         (default: main)
#   TDO_DEV_REPO          git remote                    (default: https://github.com/geekychris/3do-dev.git)
#   TDO_DEV_AUTO_INSTALL  1 = install missing prerequisites with brew/apt/dnf/pacman (asks for sudo)
#   TDO_DEV_QUICK         1 = skip the final build+test pass  (./setup.sh --quick)
#   TDO_DEV_NO_BIOS       1 = don't download BIOS ROMs (put your own in <src>/bios)
#   TDO_DEV_START         1 = launch the demo in a window when done (default: 1 if a display exists)

set -euo pipefail

SRC="${TDO_DEV_SRC:-$HOME/3do-dev}"
REF="${TDO_DEV_REF:-main}"
REPO="${TDO_DEV_REPO:-https://github.com/geekychris/3do-dev.git}"
AUTO_INSTALL="${TDO_DEV_AUTO_INSTALL:-0}"
QUICK="${TDO_DEV_QUICK:-0}"
NO_BIOS="${TDO_DEV_NO_BIOS:-0}"

c_reset=$'\033[0m'; c_bold=$'\033[1m'
c_green=$'\033[32m'; c_yellow=$'\033[33m'; c_red=$'\033[31m'; c_blue=$'\033[34m'
step() { printf "%s==>%s %s%s%s\n" "$c_blue" "$c_reset" "$c_bold" "$*" "$c_reset"; }
ok()   { printf "%s ✓%s %s\n" "$c_green" "$c_reset" "$*"; }
warn() { printf "%s ! %s%s\n" "$c_yellow" "$c_reset" "$*"; }
die()  { printf "%s ✗ %s%s\n" "$c_red" "$c_reset" "$*" >&2; exit 1; }

# curl | bash gets a minimal PATH; surface the usual extra bin dirs.
for _d in /opt/homebrew/bin /usr/local/bin "$HOME/.local/bin" "$HOME/.cargo/bin" /snap/bin; do
  if [ -d "$_d" ]; then
    case ":$PATH:" in *:"$_d":*) ;; *) PATH="$_d:$PATH" ;; esac
  fi
done
export PATH

# ---- platform -------------------------------------------------------------
case "$(uname -s)" in
  Darwin) OS=mac ;;
  Linux)
    OS=linux
    if [ -r /etc/os-release ]; then
      . /etc/os-release
      case "${ID:-}:${ID_LIKE:-}" in
        ubuntu*|debian*|linuxmint*|pop*|*:*debian*|*:*ubuntu*) OS=debian ;;
        fedora*|rhel*|centos*|rocky*|almalinux*|*:*fedora*|*:*rhel*) OS=fedora ;;
        arch*|manjaro*|*:*arch*) OS=arch ;;
      esac
    fi ;;
  *) die "unsupported OS $(uname -s) – on Windows use WSL2" ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }

install_hint() {
  case "$OS:$1" in
    mac:git|mac:make|mac:cc) echo "xcode-select --install" ;;
    mac:curl)        echo "brew install curl" ;;
    mac:docker)      echo "install Docker Desktop, Rancher Desktop, OrbStack (brew install --cask orbstack) or colima (brew install colima docker && colima start)" ;;
    mac:python3)     echo "brew install python@3.12  (or: brew install uv)" ;;
    mac:gdb)         echo "brew install gdb" ;;
    debian:docker)   echo "sudo apt-get install -y docker.io && sudo usermod -aG docker \$USER  (then log out/in)" ;;
    debian:python3)  echo "sudo apt-get install -y python3 python3-venv python3-pip" ;;
    debian:gdb)      echo "sudo apt-get install -y gdb-multiarch" ;;
    debian:make|debian:cc) echo "sudo apt-get install -y build-essential" ;;
    debian:*)        echo "sudo apt-get install -y $1" ;;
    fedora:docker)   echo "sudo dnf install -y moby-engine && sudo systemctl enable --now docker && sudo usermod -aG docker \$USER" ;;
    fedora:make|fedora:cc) echo "sudo dnf install -y make gcc" ;;
    fedora:gdb)      echo "sudo dnf install -y gdb" ;;
    fedora:*)        echo "sudo dnf install -y $1" ;;
    arch:docker)     echo "sudo pacman -S --noconfirm docker && sudo systemctl enable --now docker && sudo usermod -aG docker \$USER" ;;
    arch:make|arch:cc) echo "sudo pacman -S --noconfirm base-devel" ;;
    arch:python3)    echo "sudo pacman -S --noconfirm python" ;;
    arch:*)          echo "sudo pacman -S --noconfirm $1" ;;
    *)               echo "install $1 with your package manager" ;;
  esac
}

# Opt-in only (TDO_DEV_AUTO_INSTALL=1). sudo reads from /dev/tty so the
# password prompt works under `curl | bash`.
auto_install() {
  local pkg="$1"
  step "installing $pkg (TDO_DEV_AUTO_INSTALL=1)"
  case "$OS:$pkg" in
    mac:git|mac:make|mac:cc) xcode-select --install || true
                             die "finish the Xcode command line tools install, then re-run" ;;
    mac:docker)  die "Docker on macOS needs an app: $(install_hint docker)" ;;
    mac:*)       have brew || die "Homebrew missing: https://brew.sh"
                 case "$pkg" in python3) brew install python@3.12 ;; *) brew install "$pkg" ;; esac ;;
    debian:docker)
      sudo apt-get update -qq </dev/tty
      sudo apt-get install -y docker.io </dev/tty
      sudo systemctl enable --now docker </dev/tty || true
      sudo usermod -aG docker "$USER" </dev/tty
      die "docker installed and $USER added to the docker group – log out and back in (or run 'newgrp docker'), then re-run this installer" ;;
    debian:make|debian:cc) sudo apt-get update -qq </dev/tty && sudo apt-get install -y build-essential </dev/tty ;;
    debian:python3) sudo apt-get update -qq </dev/tty && sudo apt-get install -y python3 python3-venv python3-pip </dev/tty ;;
    debian:gdb)  sudo apt-get install -y gdb-multiarch </dev/tty ;;
    debian:*)    sudo apt-get update -qq </dev/tty && sudo apt-get install -y "$pkg" </dev/tty ;;
    fedora:docker) sudo dnf install -y moby-engine </dev/tty && sudo systemctl enable --now docker </dev/tty && sudo usermod -aG docker "$USER" </dev/tty
                   die "docker installed – log out and back in, then re-run" ;;
    fedora:make|fedora:cc) sudo dnf install -y make gcc </dev/tty ;;
    fedora:*)    sudo dnf install -y "$pkg" </dev/tty ;;
    arch:docker) sudo pacman -S --noconfirm docker </dev/tty && sudo systemctl enable --now docker </dev/tty && sudo usermod -aG docker "$USER" </dev/tty
                 die "docker installed – log out and back in, then re-run" ;;
    arch:make|arch:cc) sudo pacman -S --noconfirm base-devel </dev/tty ;;
    arch:python3) sudo pacman -S --noconfirm python </dev/tty ;;
    arch:*)      sudo pacman -S --noconfirm "$pkg" </dev/tty ;;
    *)           die "don't know how to install $pkg on $OS: $(install_hint "$pkg")" ;;
  esac
}

require() {
  local bin="$1" pkg="${2:-$1}"
  have "$bin" && return 0
  if [ "$AUTO_INSTALL" = 1 ]; then
    auto_install "$pkg"
    have "$bin" && return 0
  fi
  die "missing $bin – $(install_hint "$pkg")
     (or re-run with TDO_DEV_AUTO_INSTALL=1 to install prerequisites automatically)"
}

# ---- prerequisites ---------------------------------------------------------
step "prerequisites ($OS, $(uname -m))"
require git
require curl
require make
require cc
require docker
docker info >/dev/null 2>&1 || die "docker is installed but the daemon isn't reachable.
     macOS: start Docker Desktop / Rancher Desktop / OrbStack / 'colima start'.
     Linux: sudo systemctl start docker, and make sure \$USER is in the docker group."
if ! have uv; then
  require python3
  python3 -c 'import sys; sys.exit(sys.version_info < (3,10))' \
    || die "Python >= 3.10 required (found $(python3 --version)); $(install_hint python3) – or install uv: https://docs.astral.sh/uv/"
  if [ "$OS" = debian ] && ! python3 -c 'import ensurepip' >/dev/null 2>&1; then
    require __none__ python3-venv
  fi
fi
ok "git, curl, make, cc, docker, python"
if ! have gdb && ! have gdb-multiarch && ! have arm-none-eabi-gdb; then
  if [ "$AUTO_INSTALL" = 1 ]; then auto_install gdb || true
  else warn "gdb not found (optional, for source-level debugging): $(install_hint gdb)"; fi
fi

# ---- source ----------------------------------------------------------------
step "source -> $SRC ($REF)"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" fetch -q origin "$REF"
  if [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]; then
    warn "local changes in $SRC – not updating the checkout (commit/stash them to pull $REF)"
  else
    git -C "$SRC" checkout -q "$REF" 2>/dev/null || git -C "$SRC" checkout -q -B "$REF" FETCH_HEAD
    git -C "$SRC" merge -q --ff-only FETCH_HEAD 2>/dev/null || git -C "$SRC" reset -q --hard FETCH_HEAD
  fi
else
  [ ! -e "$SRC" ] || die "$SRC exists and isn't a git checkout – set TDO_DEV_SRC to another path"
  git clone -q "$REPO" "$SRC"
  git -C "$SRC" checkout -q "$REF"
fi
ok "$(git -C "$SRC" log -1 --format='%h %s')"

# ---- setup -----------------------------------------------------------------
args=()
[ "$QUICK" = 1 ] && args+=(--quick)
[ "$NO_BIOS" = 1 ] && args+=(--no-bios)
cd "$SRC"
./setup.sh "${args[@]+"${args[@]}"}"

# ---- optional: show the demo -------------------------------------------------
has_display=0
if [ "$OS" = mac ] || [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then has_display=1; fi
START="${TDO_DEV_START:-$has_display}"
if [ "$START" = 1 ] && [ "$NO_BIOS" != 1 ]; then
  step "launching the demo window"
  mkdir -p "$SRC/build"
  nohup ./3do run demo >"$SRC/build/demo-window.log" 2>&1 &
  echo $! >"$SRC/build/demo-window.pid"
  ok "demo running (pid $(cat "$SRC/build/demo-window.pid"), log build/demo-window.log)"
fi

cat <<EOF

${c_green}${c_bold}3do-dev is installed in $SRC${c_reset}

  cd $SRC
  ./3do run demo            play the demo (control port 7330, gdb port 2330)
  ./3do new mygame          start your own program
  ./3do build mygame && ./3do run mygame
  ./3do ctl help            remote-control commands
  ./3do gdb demo            source-level debugging
  claude                    open with Claude Code (MCP server "3do" is preconfigured)

Docs: README.md, docs/getting-started.md, docs/how-it-works.md
Re-run this installer any time to update.
EOF
