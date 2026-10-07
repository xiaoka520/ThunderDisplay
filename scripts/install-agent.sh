#!/bin/bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
task_label=dev.thunderdisplay.host
task_uid="$(id -u)"
task_plist="$HOME/Library/LaunchAgents/$task_label.plist"
if [ "${1:-}" = --uninstall ]; then
    launchctl bootout "gui/$task_uid/$task_label" 2>/dev/null || true
    rm -f "$task_plist"
    printf 'Removed login startup; the installed app and pairing code are preserved.\n'
    exit 0
fi
printf 'Use the native ThunderDisplay Mac Setup PKG to install the app and startup components.\n' >&2
exit 1
