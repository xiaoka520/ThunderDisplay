#!/bin/bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
task_installer="$(find "$task_root/dist" -maxdepth 1 -name 'ThunderDisplay-Mac-Setup-*-arm64.pkg' -print -quit)"
if [ -z "$task_installer" ]; then
    printf 'Build the native Mac installer first.\n' >&2; exit 1
fi
# The native upgrade validates and replaces the apps and supervised services.
# It preserves configuration and never deletes unrelated background items.
open "$task_installer"
