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
if [ ! -d "$task_root/dist/ThunderDisplayHost.app" ]; then
    printf 'Build the Mac app first: ./scripts/build-mac.sh\n' >&2
    exit 1
fi
task_installed="$HOME/Applications/ThunderDisplayHost.app"
mkdir -p "$HOME/Applications" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs/ThunderDisplay"
launchctl bootout "gui/$task_uid/$task_label" 2>/dev/null || true
ditto "$task_root/dist/ThunderDisplayHost.app" "$task_installed"
/usr/bin/python3 - "$task_plist" "$task_installed/Contents/MacOS/ThunderDisplayHost" "$HOME/Library/Logs/ThunderDisplay" "$@" <<'PY'
import plistlib, sys
from pathlib import Path
path, binary, logs, *args = sys.argv[1:]
config = {
    'Label': 'dev.thunderdisplay.host',
    'ProgramArguments': [binary, *args],
    'RunAtLoad': True,
    'KeepAlive': {'SuccessfulExit': False},
    'ThrottleInterval': 10,
    'LimitLoadToSessionType': 'Aqua',
    'StandardOutPath': str(Path(logs) / 'host.log'),
    'StandardErrorPath': str(Path(logs) / 'host-error.log'),
}
with open(path, 'wb') as output:
    plistlib.dump(config, output)
PY
chmod 600 "$task_plist"
launchctl bootstrap "gui/$task_uid" "$task_plist"
printf 'Installed %s\n' "$task_plist"
