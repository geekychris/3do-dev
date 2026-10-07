#!/usr/bin/env bash
# Download 3DO BIOS ROMs from 3dodev.com and verify their MD5s.
#
# The 3DO BIOS is copyrighted firmware. 3dodev.com hosts these images for the
# homebrew community; if you would rather dump your own console's ROM, put it
# in ./bios (or point TDO_BIOS_DIR at a directory) and this script will just
# verify it.
#
#   panafz10.bin        FZ-10 (U) – default. Retail ROM, runs signed discs
#                       (our build signs every ISO, so this "just works").
#   panafz1.bin         FZ-1 (U) – the original model, for compatibility checks.
#   panafz10-norsa.bin  FZ-10 with the RSA check disabled – runs unsigned discs.
source "$(dirname "$0")/lib.sh"

BASE_URL="https://3dodev.com/_media/roms"
ROMS=(
  "panafz10.bin       51f2f43ae2f3508a14d9f56597e2d3ce"
  "panafz1.bin        f47264dd47fe30f73ab3c010015c155b"
  "panafz10-norsa.bin 1477bda80dc33731a65468c1f5bcbee9"
)

step "BIOS ROMs -> $BIOS_DIR"
mkdir -p "$BIOS_DIR"
for entry in "${ROMS[@]}"; do
  read -r name md5 <<<"$entry"
  dest="$BIOS_DIR/$name"
  if [ -f "$dest" ] && [ "$(md5_of "$dest")" = "$md5" ]; then
    ok "$name (cached)"
    continue
  fi
  curl -fsSL --retry 3 -o "$dest.part" "$BASE_URL/$name" || die "download failed: $BASE_URL/$name"
  got="$(md5_of "$dest.part")"
  if [ "$got" != "$md5" ]; then
    rm -f "$dest.part"
    die "$name md5 mismatch (got $got, want $md5)"
  fi
  mv "$dest.part" "$dest"
  ok "$name"
done
