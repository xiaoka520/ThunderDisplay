#!/bin/bash
# Root-owned, fixed-path cleanup after the installed app leaves Applications.
# No GUI, network, user input, permission changes, or removal of the Trash copy.
set -euo pipefail
task_app=/Applications/ThunderDisplayHost.app
task_service_dir="/Library/Application Support/ThunderDisplay"
task_marker="$task_service_dir/installed-app.marker"
task_pending="$task_service_dir/trash-cleanup.pending"
task_label=dev.thunderdisplay.cleanup
task_desktop=/Library/LaunchAgents/dev.thunderdisplay.desktop.plist
task_login=/Library/LaunchAgents/dev.thunderdisplay.loginwindow.plist
if [ "$(/usr/bin/id -u)" != 0 ]; then exit 1; fi
if [ ! -d "$task_service_dir" ] || [ ! -f "$task_marker" ]; then exit 0; fi
for task_path in "$task_service_dir" "$task_marker" "$task_pending" "$task_desktop" "$task_login" \
    /Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist \
    /Library/LaunchDaemons/dev.thunderdisplay.boot.plist \
    "/Library/LaunchDaemons/$task_label.plist" \
    /Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system \
    /Library/PrivilegedHelperTools/dev.thunderdisplay.boot; do
    if [ -L "$task_path" ]; then exit 1; fi
done
for task_path in "$task_service_dir" "$task_marker"; do
    if [ "$(/usr/bin/stat -f %u "$task_path")" != 0 ]; then exit 1; fi
    task_mode="$(/usr/bin/stat -f %Lp "$task_path")"
    if (( (8#$task_mode & 0022) != 0 )); then exit 1; fi
done
if [ -e "$task_app" ] || [ -L "$task_app" ]; then
    /bin/rm -f "$task_pending"; exit 0
fi
task_now="$(/bin/date +%s)"
if [ ! -f "$task_pending" ]; then
    (umask 077; printf '%s\n' "$task_now" > "$task_pending"); exit 0
fi
if [ "$(/usr/bin/stat -f %u "$task_pending")" != 0 ]; then exit 1; fi
task_mode="$(/usr/bin/stat -f %Lp "$task_pending")"
if (( (8#$task_mode & 0022) != 0 )); then exit 1; fi
task_since="$(/bin/cat "$task_pending")"
if [[ ! "$task_since" =~ ^[0-9]{1,12}$ ]] || [ "$task_since" -gt "$task_now" ]; then
    (umask 077; printf '%s\n' "$task_now" > "$task_pending"); exit 0
fi
# A restore cancels cleanup. Native Installer unloads this job before replacing
# the bundle; the grace period also protects short manual replacements.
if (( task_now - task_since < 30 )); then exit 0; fi
if [ -e "$task_app" ] || [ -L "$task_app" ]; then exit 0; fi
task_uid="$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:3' "$task_desktop" 2>/dev/null || true)"
if [[ "$task_uid" =~ ^[0-9]{3,10}$ ]] && [ "$task_uid" -ge 500 ] && [ "$task_uid" -lt 4294967295 ]; then
    task_system_app="$task_service_dir/ThunderDisplayHost.app"
    task_host="$task_system_app/Contents/MacOS/ThunderDisplayHost"
    # Retire this user's native login registration while the immutable system
    # copy still exists. Never run the app as root or request new permissions.
    if [ -x "$task_host" ] && [ ! -L "$task_system_app" ] && [ ! -L "$task_host" ] && \
        [ "$(/usr/bin/stat -f %u "$task_host")" = 0 ] && \
        [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$task_system_app/Contents/Info.plist" 2>/dev/null || true)" = dev.thunderdisplay.host ] && \
        /bin/launchctl print "gui/$task_uid" >/dev/null 2>&1; then
        /bin/launchctl asuser "$task_uid" /usr/bin/sudo -H -u "#$task_uid" "$task_host" --unregister-login >/dev/null 2>&1 || true
    fi
    /bin/launchctl bootout "gui/$task_uid/dev.thunderdisplay.desktop" 2>/dev/null || true
fi
/bin/launchctl unload -S LoginWindow "$task_login" 2>/dev/null || true
for task_boot in dev.thunderdisplay.boot.system dev.thunderdisplay.boot; do
    /bin/launchctl disable "system/$task_boot" 2>/dev/null || true
    /bin/launchctl bootout "system/$task_boot" 2>/dev/null || true
    /bin/rm -f "/Library/LaunchDaemons/$task_boot.plist" "/Library/PrivilegedHelperTools/$task_boot"
done
/usr/bin/pkill -TERM -x ThunderDisplayHost 2>/dev/null || true
/bin/rm -f "$task_desktop" "$task_login" "/Library/LaunchDaemons/$task_label.plist"
/bin/rm -rf "$task_service_dir"
/usr/sbin/pkgutil --forget dev.thunderdisplay.installer >/dev/null 2>&1 || true
# The cleanup job is retired last. Its files and all startup paths are gone.
exec /bin/launchctl bootout "system/$task_label"
