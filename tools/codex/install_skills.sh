#!/usr/bin/env bash
set -euo pipefail

mode=install
if [[ "${1:-}" == "--check" ]]; then
  mode=check
elif [[ $# -ne 0 ]]; then
  echo "Usage: bash apps/hft-recorder/tools/codex/install_skills.sh [--check]" >&2
  exit 2
fi

recorder_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source_root="$recorder_root/tools/codex/skills"
target_root="${CODEX_HOME:-$HOME/.codex}/skills"
skills=(
  hft-recorder-corpus-pipeline
  hft-recorder-gui-development
  hft-recorder-compression-lab
)

failed=0
for skill in "${skills[@]}"; do
  source_path="$source_root/$skill"
  target_path="$target_root/$skill"

  if [[ ! -f "$source_path/SKILL.md" || ! -f "$source_path/agents/openai.yaml" ]]; then
    echo "missing skill source: $source_path" >&2
    failed=1
    continue
  fi

  if [[ "$mode" == "check" ]]; then
    if [[ ! -L "$target_path" ]]; then
      echo "not installed: $target_path" >&2
      failed=1
      continue
    fi
    if [[ "$(readlink -f "$target_path")" != "$(readlink -f "$source_path")" ]]; then
      echo "wrong symlink target: $target_path" >&2
      failed=1
      continue
    fi
    echo "ok: $skill"
    continue
  fi

  mkdir -p "$target_root"
  if [[ -L "$target_path" ]]; then
    if [[ "$(readlink -f "$target_path")" == "$(readlink -f "$source_path")" ]]; then
      echo "already installed: $skill"
      continue
    fi
    echo "refusing to replace foreign symlink: $target_path" >&2
    failed=1
    continue
  fi
  if [[ -e "$target_path" ]]; then
    echo "refusing to replace existing path: $target_path" >&2
    failed=1
    continue
  fi

  ln -s "$source_path" "$target_path"
  echo "installed: $skill"
done

exit "$failed"
