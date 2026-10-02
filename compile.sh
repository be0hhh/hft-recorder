#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
if [[ ! -f "$ROOT/tools/Build/CompileTargets.py" ]]; then
  printf '%s\n' 'Build requires the canonical CXET checkout.' >&2
  exit 2
fi
exec python3 "$ROOT/tools/Build/CompileTargets.py" --owner hft-recorder "$@"
