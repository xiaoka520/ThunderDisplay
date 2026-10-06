#!/bin/bash
# Explicit administrator installer for locally signed, unnotarized builds.
set -euo pipefail
task_label=dev.thunderdisplay.boot.system
task_legacy_label=dev.thunderdisplay.boot
task_plist="/Library/LaunchDaemons/$task_label.plist"
task_binary="/Library/PrivilegedHelperTools/$task_label"
task_agent_label=dev.thunderdisplay.loginwindow
task_agent_plist="/Library/LaunchAgents/$task_agent_label.plist"
task_desktop_label=dev.thunderdisplay.desktop
task_desktop_plist="/Library/LaunchAgents/$task_desktop_label.plist"
task_service_dir="/Library/Application Support/ThunderDisplay"
task_system_app="$task_service_dir/ThunderDisplayHost.app"
if [ "$(id -u)" != 0 ]; then
    printf 'Administrator installation required: sudo bash "%s" [path/to/ThunderDisplayHost.app]\n' "$0" >&2
    exit 1
fi
if [ "${1:-}" = --uninstall ]; then
    task_desktop_uid="$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:3' "$task_desktop_plist" 2>/dev/null || true)"
    if [[ "$task_desktop_uid" =~ ^[0-9]{3,10}$ ]]; then
        launchctl bootout "gui/$task_desktop_uid/$task_desktop_label" 2>/dev/null || true
    fi
    rm -f "$task_desktop_plist"
    launchctl disable "system/$task_label"
    launchctl bootout "system/$task_label" 2>/dev/null || true
    launchctl disable "system/$task_legacy_label"
    launchctl bootout "system/$task_legacy_label" 2>/dev/null || true
    rm -f "$task_plist" "$task_binary"
    rm -f "/Library/LaunchDaemons/$task_legacy_label.plist" "/Library/PrivilegedHelperTools/$task_legacy_label"
    launchctl unload -S LoginWindow "$task_agent_plist" 2>/dev/null || true
    rm -f "$task_agent_plist" "$task_service_dir/loginwindow-config.plist" "$task_service_dir/loginwindow-state.json"
    rm -rf "$task_system_app"
    printf 'Boot discovery service removed. Login startup and permissions are unchanged.\n'
    exit 0
fi
task_script_dir="$(cd "$(dirname "$0")" && pwd)"
if [[ "$task_script_dir" == *.app/Contents/Resources ]]; then
    task_app="${1:-$task_script_dir/../..}"
else
    task_app="${1:-$task_script_dir/../dist/ThunderDisplayHost.app}"
fi
codesign --verify --deep --strict "$task_app"
codesign --verify --strict "$task_app/Contents/MacOS/ThunderDisplayBoot"
plutil -lint "$task_app/Contents/Resources/$task_label.plist"
plutil -lint "$task_app/Contents/Resources/$task_agent_label.plist"
plutil -lint "$task_app/Contents/Resources/$task_desktop_label.plist"
task_config_source="${2:-}"
task_config_temporary=""
if [ -z "$task_config_source" ]; then
    task_config_temporary="$(mktemp)"
    task_config_source="$task_config_temporary"
    cat > "$task_config_source" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>port</key><integer>47990</integer><key>requirePairing</key><false/></dict></plist>
PLIST
fi
trap 'if [ -n "$task_config_temporary" ]; then rm -f "$task_config_temporary"; fi' EXIT
plutil -lint "$task_config_source"
task_port="$(/usr/libexec/PlistBuddy -c 'Print :port' "$task_config_source")"
if [[ ! "$task_port" =~ ^[0-9]{1,5}$ ]] || [ "$task_port" -lt 1 ] || [ "$task_port" -gt 65535 ]; then
    printf 'Invalid pre-login port.\n' >&2; exit 1
fi
task_desktop_uid="$(/usr/libexec/PlistBuddy -c 'Print :desktopUID' "$task_config_source" 2>/dev/null || /usr/bin/stat -f %u /dev/console)"
if [[ ! "$task_desktop_uid" =~ ^[0-9]{3,10}$ ]] || [ "$task_desktop_uid" -lt 500 ] || [ "$task_desktop_uid" -ge 4294967295 ]; then
    printf 'Invalid desktop startup user; install from the intended user desktop.\n' >&2; exit 1
fi
task_pairing="$(/usr/libexec/PlistBuddy -c 'Print :requirePairing' "$task_config_source")"
if [ "$task_pairing" = true ]; then
    task_token="$(/usr/libexec/PlistBuddy -c 'Print :token' "$task_config_source")"
    if [[ ! "$task_token" =~ ^[a-fA-F0-9]{32}$ ]]; then printf 'Invalid pre-login pairing configuration.\n' >&2; exit 1; fi
elif [ "$task_pairing" != false ]; then
    printf 'Invalid pre-login pairing policy.\n' >&2; exit 1
fi
# The GUI agent is installed outside the user's writable checkout. It retains
# the app's signing identity; copying a lone executable would change its TCC
# identity / lose bundle resources. Only the LoginWindow session launches it.
if [ -L "$task_service_dir" ] || [ -L "$task_system_app" ]; then
    printf 'Refusing a symlink in the system application destination.\n' >&2; exit 1
fi
launchctl unload -S LoginWindow "$task_agent_plist" 2>/dev/null || true
task_old_desktop_uid="$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:3' "$task_desktop_plist" 2>/dev/null || true)"
if [[ "$task_old_desktop_uid" =~ ^[0-9]{3,10}$ ]]; then
    launchctl bootout "gui/$task_old_desktop_uid/$task_desktop_label" 2>/dev/null || true
fi
install -d -o root -g wheel -m 755 "$task_service_dir" /Library/LaunchAgents
rm -rf "$task_system_app.new"
ditto "$task_app" "$task_system_app.new"
chown -R root:wheel "$task_system_app.new"
chmod -R go-w "$task_system_app.new"
# Older local signing scripts created the public resource seal with mode 0600.
# Keep the root-owned bundle immutable while allowing normal signature checks.
chmod 644 "$task_system_app.new/Contents/_CodeSignature/CodeResources"
codesign --verify --deep --strict "$task_system_app.new"
rm -rf "$task_system_app"
mv "$task_system_app.new" "$task_system_app"
install -o root -g wheel -m 600 "$task_config_source" "$task_service_dir/loginwindow-config.plist"
install -o root -g wheel -m 644 "$task_app/Contents/Resources/$task_agent_label.plist" "$task_agent_plist"
install -o root -g wheel -m 644 "$task_app/Contents/Resources/$task_desktop_label.plist" "$task_desktop_plist"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments:2 string --startup-user' -c "Add :ProgramArguments:3 string $task_desktop_uid" "$task_desktop_plist"
rm -f "$task_service_dir/loginwindow-state.json"
# disable is persisted across boots. bootout alone only fixes the current boot;
# the retired SM registration previously displaced the absolute-path daemon.
launchctl disable "system/$task_legacy_label"
launchctl bootout "system/$task_legacy_label" 2>/dev/null || true
rm -f "/Library/LaunchDaemons/$task_legacy_label.plist" "/Library/PrivilegedHelperTools/$task_legacy_label"
launchctl bootout "system/$task_label" 2>/dev/null || true
install -d -o root -g wheel -m 755 /Library/PrivilegedHelperTools
install -o root -g wheel -m 755 "$task_app/Contents/MacOS/ThunderDisplayBoot" "$task_binary"
install -o root -g wheel -m 644 "$task_app/Contents/Resources/$task_label.plist" "$task_plist"
/usr/libexec/PlistBuddy -c 'Add :EnvironmentVariables dict' -c "Add :EnvironmentVariables:TD_BOOT_PORT string $task_port" "$task_plist"
launchctl enable "system/$task_label"
launchctl bootstrap system "$task_plist"
task_ready=false
for task_attempt in {1..30}; do
    if launchctl print "system/$task_label" | /usr/bin/awk '$1 == "state" && $2 == "=" && $3 == "running" { running=1 } $1 == "pid" && $2 == "=" && $3 > 0 { pid=1 } END { exit !(running && pid) }'; then
        task_ready=true
        break
    fi
    sleep 0.1
done
if [ "$task_ready" != true ]; then
    printf 'Boot helper was installed but did not start. Check its launchctl state and background item approval.\n' >&2
    exit 1
fi
# load -S is Apple's documented session-specific legacy operation. If the
# LoginWindow domain is absent while logged in, the installed LaunchAgent will
# be loaded when that graphical session is next created. Never load it into Aqua.
launchctl load -S LoginWindow "$task_agent_plist" 2>/dev/null || true
launchctl enable "gui/$task_desktop_uid/$task_desktop_label" 2>/dev/null || true
if launchctl print "gui/$task_desktop_uid" >/dev/null 2>&1; then
    launchctl bootstrap "gui/$task_desktop_uid" "$task_desktop_plist"
fi
printf 'Installed system discovery, LoginWindow host and Aqua startup for user %s on port %s.\n' "$task_desktop_uid" "$task_port"
printf 'Enable login startup in the Mac app so desktop capture starts after login.\n'
printf 'LoginWindow capture / input still require real pre-login permission and frame verification. FileVault preboot is unsupported.\n'
