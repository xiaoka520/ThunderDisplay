#!/usr/bin/env python3
"""Prepare two isolated apps with our existing local signing identity.

Signing verification does not establish OS acceptance of a restricted entitlement.
Run preflight first, then launch the entitled GUI for a user-approved capture test.
"""
from pathlib import Path
import importlib.util
import os
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/tests/persistent-capture'
OUT.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
subprocess.run(['xcrun', 'swiftc', '-target', 'arm64-apple-macosx13.0',
    '-module-cache-path', str(OUT / 'module-cache'),
    str(ROOT / 'tests/PersistentCaptureProbe/main.swift'), '-framework', 'AppKit',
    '-framework', 'ScreenCaptureKit', '-framework', 'Security', '-o', str(OUT / 'PermissionProbe')], env=env, check=True)
spec = importlib.util.spec_from_file_location('sign_mac', ROOT / 'scripts/sign-mac.py')
signing = importlib.util.module_from_spec(spec); spec.loader.exec_module(signing)
identity, keychain = signing.local_identity()
try:
    for name, identifier, entitlement in [
        ('LocalCaptureBaseline', 'dev.thunderdisplay.capturebaseline', False),
        ('PersistentCaptureProbe', 'dev.thunderdisplay.captureprobe', True)]:
        app = OUT / (name + '.app'); binary = app / 'Contents/MacOS/PermissionProbe'
        binary.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(OUT / 'PermissionProbe', binary)
        (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': identifier, 'CFBundleName': name, 'CFBundleDisplayName': 'ThunderDisplay 本地权限测试',
            'CFBundleExecutable': 'PermissionProbe', 'CFBundlePackageType': 'APPL',
            'CFBundleVersion': '1', 'CFBundleShortVersionString': '1.0', 'LSMinimumSystemVersion': '13.0',
            'NSScreenCaptureUsageDescription': '本地权限测试：仅检查能否接收到一帧，不保存或传输桌面内容。',
        }))
        claims = OUT / (name + '.entitlements.plist')
        claims.write_bytes(plistlib.dumps({'com.apple.developer.persistent-content-capture': True} if entitlement else {}))
        signing.run(['/usr/bin/codesign', '--force', '--sign', identity, '--keychain', str(keychain),
            '--timestamp=none', '--identifier', identifier, '--entitlements', str(claims), str(app)])
        signing.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)])
        print(f'Prepared isolated local-signed test app: {app}')
finally:
    signing.run(['/usr/bin/security', 'lock-keychain', str(keychain)])
