# Runtime for the 3do-devkit's prebuilt Linux tools.
#
#   * Norcroft armcc/armcpp/armasm/armlink/armlib are 32-bit i386 ELF binaries
#   * 3dt/3it/3ct/modbin are 64-bit x86-64 static binaries
#
# So: an amd64 Debian with the i386 libc multiarch. On Apple Silicon / arm64
# Linux this runs under QEMU binfmt emulation (scripts/build-toolchain-image.sh
# makes sure that's registered). The devkit itself is bind-mounted, not baked
# in, so bumping the devkit pin doesn't need an image rebuild.
FROM debian:bookworm-slim

RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends make libc6:i386 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

ENV TDO_DEVKIT_PATH=/work/third_party/3do-devkit
ENV PATH=/work/third_party/3do-devkit/bin/compiler/linux:/work/third_party/3do-devkit/bin/tools/linux:$PATH
WORKDIR /work
