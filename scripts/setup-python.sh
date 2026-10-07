#!/usr/bin/env bash
# Create .venv with the tdo package (emulator host, CLI, MCP server).
source "$(dirname "$0")/lib.sh"

step "python environment (.venv)"
VENV="$TDO_ROOT/.venv"
if have uv; then
  [ -x "$VENV/bin/python" ] || uv venv -q --python ">=3.10" "$VENV"
  uv pip install -q --python "$VENV/bin/python" -e "$TDO_ROOT/tools/tdo"
else
  py=""
  for c in python3.13 python3.12 python3.11 python3.10 python3; do
    if have "$c" && "$c" -c 'import sys; sys.exit(sys.version_info < (3,10))' 2>/dev/null; then py="$c"; break; fi
  done
  [ -n "$py" ] || die "need Python >= 3.10 (or install uv: https://docs.astral.sh/uv/)"
  [ -x "$VENV/bin/python" ] || "$py" -m venv "$VENV"
  "$VENV/bin/python" -m pip install -q --upgrade pip
  "$VENV/bin/python" -m pip install -q -e "$TDO_ROOT/tools/tdo"
fi
"$VENV/bin/python" -c "import tdo.core, numpy, PIL, pygame, mcp" 2>/dev/null >/dev/null \
  || die "python deps failed to import"
ok "tdo CLI at .venv/bin/tdo, MCP server at .venv/bin/tdo-mcp"
