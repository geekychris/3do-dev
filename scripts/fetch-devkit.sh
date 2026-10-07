#!/usr/bin/env bash
# Fetch trapexit's 3do-devkit (Norcroft ARM C/C++ compilers, Portfolio 2.5
# headers+libs, 3dt/3it/3ct/modbin, base disc filesystem, docs, examples)
# at the pinned commit into third_party/3do-devkit.
source "$(dirname "$0")/lib.sh"

step "3do-devkit @ ${DEVKIT_COMMIT:0:10}"
have git || die "git is required"
git_checkout_pinned "$DEVKIT_REPO" "$DEVKIT_DIR" "$DEVKIT_COMMIT"
# The repo marks tools executable, but be robust to odd checkouts.
chmod +x "$DEVKIT_DIR"/bin/compiler/linux/* "$DEVKIT_DIR"/bin/tools/linux/* 2>/dev/null || true
ok "devkit at $DEVKIT_DIR"
