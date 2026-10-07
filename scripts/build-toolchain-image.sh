#!/usr/bin/env bash
# Build the Docker image that runs the devkit's x86 Linux compilers/tools,
# registering QEMU binfmt handlers first if the Docker VM can't run x86.
source "$(dirname "$0")/lib.sh"

step "toolchain container ($TOOLCHAIN_IMAGE)"
have docker || die "docker is required (Docker Desktop, Rancher Desktop, OrbStack, colima, or docker-ce)"
docker info >/dev/null 2>&1 || die "docker daemon not reachable – start Docker Desktop / Rancher Desktop / colima first"

docker_arch="$(docker info --format '{{.Architecture}}' 2>/dev/null || echo unknown)"
case "$docker_arch" in
  x86_64|amd64) ;;  # i386 + amd64 run natively
  *)
    # arm64 VM: need qemu-i386 + qemu-x86_64 binfmt handlers.
    if ! docker run --rm --platform linux/386 debian:bookworm-slim true >/dev/null 2>&1; then
      warn "Docker VM ($docker_arch) cannot run x86 binaries; registering QEMU binfmt handlers"
      warn "(runs tonistiigi/binfmt --privileged; must be redone if the Docker VM is recreated)"
      docker run --privileged --rm tonistiigi/binfmt --install 386,amd64 >/dev/null \
        || die "binfmt registration failed; see https://github.com/tonistiigi/binfmt"
      docker run --rm --platform linux/386 debian:bookworm-slim true >/dev/null 2>&1 \
        || die "still cannot run i386 containers after binfmt install"
    fi
    ok "x86 emulation available in Docker VM ($docker_arch)"
    ;;
esac

docker build -q --platform linux/amd64 -t "$TOOLCHAIN_IMAGE" "$TDO_ROOT/docker" >/dev/null \
  || die "docker build failed"

# Verify both binary flavours actually execute.
docker run --rm --platform linux/amd64 -v "$DEVKIT_DIR:/work/third_party/3do-devkit:ro" \
  "$TOOLCHAIN_IMAGE" sh -c 'armcc 2>&1 | head -1 && 3dt --help >/dev/null 2>&1; echo "3dt rc=$?"' \
  | sed 's/^/    /'
ok "image $TOOLCHAIN_IMAGE ready"
