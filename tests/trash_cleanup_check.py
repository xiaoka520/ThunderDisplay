#!/usr/bin/env python3
"""Run production cleanup in temporary paths with all service/admin calls stubbed."""
from pathlib import Path
import os
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'installers/macos/trash-cleanup.sh').read_text()


def run_case(name, setup, verify, expected=0, owner='0'):
    with tempfile.TemporaryDirectory(prefix='td-trash-fixture-') as temporary:
        fixture = Path(temporary)
        service = fixture / 'Library/Application Support/ThunderDisplay'
        app = fixture / 'Applications/ThunderDisplayHost.app'
        service.mkdir(parents=True)
        app.parent.mkdir()
        marker = service / 'installed-app.marker'
        marker.touch(); marker.chmod(0o600)
        calls = fixture / 'calls.txt'
        mock = fixture / 'mock'
        mock.mkdir()
        scripts = {
            '/usr/bin/id': '#!/bin/bash\nprintf "0\\n"\n',
            '/usr/bin/stat': '#!/bin/bash\nif [ "$2" = %u ]; then printf "%s\\n" "$TD_FIXTURE_OWNER"; else exec /usr/bin/stat "$@"; fi\n',
            '/usr/libexec/PlistBuddy': '#!/bin/bash\nif [[ "$*" == *CFBundleIdentifier* ]]; then printf "dev.thunderdisplay.host\\n"; else printf "501\\n"; fi\n',
        }
        for command in ('/bin/launchctl', '/usr/bin/pkill', '/usr/sbin/pkgutil'):
            scripts[command] = '#!/bin/bash\nprintf "%s\\n" "$*" >> "$TD_FIXTURE_CALLS"\n'
        source = SOURCE.replace('/Applications/', str(fixture)+'/Applications/').replace('/Library/', str(fixture)+'/Library/')
        for command, script in scripts.items():
            file = mock / Path(command).name
            file.write_text(script); file.chmod(0o700)
            source = source.replace(command, str(file))
        # Guard against accidentally touching the real system in a fixture.
        assert '/bin/launchctl' not in source and '/usr/bin/pkill' not in source
        assert '=/Applications/' not in source and '="/Library/' not in source
        cleanup = fixture / 'check.sh'
        cleanup.write_text(source)
        setup(fixture, service, app)
        env = dict(os.environ, TD_FIXTURE_OWNER=owner, TD_FIXTURE_CALLS=str(calls))
        result = subprocess.run(['/bin/bash', str(cleanup)], env=env, capture_output=True, text=True)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        verify(fixture, service, app, calls.read_text() if calls.exists() else '')
        print(name+' passed')


def no_calls(fixture, service, app, calls):
    assert service.exists() and not calls


def pending(service, seconds=31):
    path = service / 'trash-cleanup.pending'
    path.write_text(str(int(time.time())-seconds)); path.chmod(0o600)


def restored(fixture, service, app):
    app.mkdir(); pending(service)


def restored_check(fixture, service, app, calls):
    no_calls(fixture, service, app, calls)
    assert app.exists() and not (service/'trash-cleanup.pending').exists()


def first_absent(fixture, service, app, calls):
    no_calls(fixture, service, app, calls)
    assert (service/'trash-cleanup.pending').exists()


def expired_setup(fixture, service, app):
    pending(service)
    host = service/'ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost'
    host.parent.mkdir(parents=True); host.touch(); host.chmod(0o755)
    for folder, files in (
        ('LaunchAgents', ['dev.thunderdisplay.desktop.plist', 'dev.thunderdisplay.loginwindow.plist']),
        ('LaunchDaemons', ['dev.thunderdisplay.boot.system.plist', 'dev.thunderdisplay.boot.plist', 'dev.thunderdisplay.cleanup.plist']),
        ('PrivilegedHelperTools', ['dev.thunderdisplay.boot.system', 'dev.thunderdisplay.boot']),
    ):
        directory = fixture/'Library'/folder; directory.mkdir()
        for file in files: (directory/file).touch()
        (directory/'unrelated.keep').touch()
    (fixture/'Trash/ThunderDisplayHost.app').mkdir(parents=True)


def expired_check(fixture, service, app, calls):
    assert not service.exists()
    assert (fixture/'Trash/ThunderDisplayHost.app').exists()
    assert 'bootout gui/501/dev.thunderdisplay.desktop' in calls
    assert 'asuser 501 /usr/bin/sudo -H -u #501' in calls and '--unregister-login' in calls
    assert 'unload -S LoginWindow' in calls and '-TERM -x ThunderDisplayHost' in calls
    assert calls.strip().endswith('bootout system/dev.thunderdisplay.cleanup')
    for directory in ('LaunchAgents', 'LaunchDaemons', 'PrivilegedHelperTools'):
        assert [p.name for p in (fixture/'Library'/directory).iterdir()] == ['unrelated.keep']


run_case('Installed app / restore cancels removal', restored, restored_check)
run_case('First absent observation waits', lambda *_: None, first_absent)
run_case('Grace interval preserves components', lambda f,s,a: pending(s,0), no_calls)
run_case('Expired removal stops only TD services and keeps Trash', expired_setup, expired_check)
run_case('Untrusted ownership refuses cleanup', lambda f,s,a: pending(s), no_calls, 1, '501')
run_case('Writable service directory refuses cleanup', lambda f,s,a: s.chmod(0o777), no_calls, 1)
run_case('Pending symlink refuses cleanup', lambda f,s,a: (s/'trash-cleanup.pending').symlink_to(f/'outside'), no_calls, 1)
run_case('Clock reversal restarts grace', lambda f,s,a: pending(s,-300), no_calls)
run_case('Replacement app protects components', lambda f,s,a: a.touch(), no_calls)
print('Trash cleanup fixtures passed; no production files, processes or services were touched')
