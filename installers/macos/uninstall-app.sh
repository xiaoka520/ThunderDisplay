#!/bin/bash
set -euo pipefail
if [ "$(/usr/bin/id -u)" != 0 ]; then printf 'Administrator authorization required.\n' >&2; exit 1; fi
task_app=/Applications/ThunderDisplayHost.app
task_uninstaller="/Applications/ThunderDisplay Uninstaller.app"
for task_path in "$task_app" "$task_uninstaller" "/Library/Application Support/ThunderDisplay"; do
    if [ -L "$task_path" ]; then printf 'Refusing a symlink in the uninstall destination.\n' >&2; exit 1; fi
done
if [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$task_app/Contents/Info.plist")" != dev.thunderdisplay.host ] ||
   [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$task_uninstaller/Contents/Info.plist")" != dev.thunderdisplay.uninstaller ]; then
    printf 'Installed application identity does not match.\n' >&2; exit 1
fi
task_uid="$(/usr/bin/stat -f %u /dev/console)"
if [ "$task_uid" -ge 500 ] && [ "$task_uid" -lt 4294967295 ]; then
    /bin/launchctl asuser "$task_uid" /usr/bin/sudo -H -u "#$task_uid" "$task_app/Contents/MacOS/ThunderDisplayHost" --unregister-login >/dev/null
fi
/bin/bash "$task_app/Contents/Resources/install-boot-service.sh" --uninstall
/usr/bin/pkill -TERM -x ThunderDisplayHost 2>/dev/null || true
/bin/rm -rf "$task_app" "$task_uninstaller"
/usr/sbin/pkgutil --forget dev.thunderdisplay.installer >/dev/null 2>&1 || true
# Keep connection preferences, pairing identity and user permission grants.
printf 'ThunderDisplay has been uninstalled.\n'
