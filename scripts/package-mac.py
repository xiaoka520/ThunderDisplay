#!/usr/bin/env python3
"""Validate signed apps and create the localized native macOS installer."""
from hashlib import sha256
from pathlib import Path
import plistlib
import re
import struct
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
app = ROOT / 'build/package/ThunderDisplayHost.app'
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
version = info['CFBundleShortVersionString']
client = re.search(r'#define TD_VERSION_TEXT "([0-9.]+)"', (ROOT / 'windows-client/version.h').read_text()).group(1)
assert version == client, 'Update both platforms together'
assert info['CFBundleIdentifier'] == 'dev.thunderdisplay.host'
assert info['CFBundleIconFile'] == 'ThunderDisplay', 'Missing application icon metadata'
assert (app / 'Contents/Resources/ThunderDisplay.icns').read_bytes() == (ROOT / 'mac-host/Resources/ThunderDisplay.icns').read_bytes(), 'App icon differs from the generated artwork'
assert (app / 'Contents/Resources/ThunderDisplayDark.icns').read_bytes() == (ROOT / 'mac-host/Resources/ThunderDisplayDark.icns').read_bytes(), 'Dark icon differs from the supplied artwork'
for suffix in ('', '@2x', '@3x'):
    name = f'ThunderDisplayStatus{suffix}.png'
    assert (app / 'Contents/Resources' / name).read_bytes() == (ROOT / 'mac-host/Resources' / name).read_bytes(), 'Status icon differs from Icon/Icon.png'
assert (app / 'Contents/_CodeSignature/CodeResources').stat().st_mode & 0o444 == 0o444, 'Public signature resource seal must be readable'
for name in ['ThunderDisplayHost', 'ThunderDisplayBoot']:
    path = app / 'Contents/MacOS' / name
    data = path.read_bytes()
    assert data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 4)[0] == 0x100000c, 'Expected arm64 Mach-O'
    assert path.stat().st_mode & 0o111, 'Missing executable permissions'
    if name == 'ThunderDisplayHost':
        # Inspect real load commands, not a marker string in a log message.
        count = struct.unpack_from('<I', data, 16)[0]
        offset = 32
        found = False
        for _ in range(count):
            command, size = struct.unpack_from('<II', data, offset)
            assert size >= 8 and offset + size <= len(data), 'Malformed Mach-O load command'
            if command == 0x19:  # LC_SEGMENT_64
                sections = struct.unpack_from('<I', data, offset + 64)[0]
                assert 72 + sections * 80 <= size, 'Malformed Mach-O section table'
                for index in range(sections):
                    section = offset + 72 + index * 80
                    if (data[section:section+16].rstrip(b'\0') == b'__cgpreloginapp' and
                        data[section+16:section+32].rstrip(b'\0') == b'__CGPreLoginApp'):
                        found = True
            offset += size
        assert found, 'Missing login-screen graphics marker in the host executable'
for name, folder in [('dev.thunderdisplay.host.agent', 'LaunchAgents')]:
    service = plistlib.loads((app / 'Contents/Library' / folder / (name+'.plist')).read_bytes())
    assert service['Label'] == name and service['RunAtLoad'] is True
assert not list((app / 'Contents/Library/LaunchDaemons').glob('*.plist')), 'Do not reintroduce a conflicting bundle daemon'
service = plistlib.loads((app / 'Contents/Resources/dev.thunderdisplay.boot.system.plist').read_bytes())
assert service['Label'] == 'dev.thunderdisplay.boot.system'
assert service['ProgramArguments'] == ['/Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system']
assert 'BundleProgram' not in service and service['UserName'] == 'nobody'
assert service['RunAtLoad'] is True and service['KeepAlive'] is True
assert (app / 'Contents/Resources/install-boot-service.sh').is_file()
loginwindow = plistlib.loads((app / 'Contents/Resources/dev.thunderdisplay.loginwindow.plist').read_bytes())
assert loginwindow['LimitLoadToSessionType'] == 'LoginWindow'
assert loginwindow['ProcessType'] == 'Interactive'
assert loginwindow['ProgramArguments'][0] == '/Library/Application Support/ThunderDisplay/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost'
desktop = plistlib.loads((app / 'Contents/Resources/dev.thunderdisplay.desktop.plist').read_bytes())
assert desktop['Label'] == 'dev.thunderdisplay.desktop' and desktop['RunAtLoad'] is True
assert desktop['LimitLoadToSessionType'] == 'Aqua' and 'UserName' not in desktop
assert desktop['ProcessType'] == 'Interactive', 'Desktop streaming must not use background resource limits'
assert desktop['ProgramArguments'] == ['/Library/Application Support/ThunderDisplay/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost', '--background']

for path in app.rglob('*'):
    if path.is_file():
        assert not any(part in ['.local-signing', '.DS_Store'] for part in path.parts)
        assert path.suffix not in ['.p12', '.keychain', '.keychain-db', '.pem'], 'Do not package private signing material'
assert (app / 'Contents/Resources/uninstall-app.sh').is_file()

work = ROOT / 'build/package/mac-installer'
if work.exists(): shutil.rmtree(work)
payload = work / 'payload'
applications = payload / 'Applications'
applications.mkdir(parents=True)
shutil.copytree(app, applications / app.name)
uninstaller = applications / 'ThunderDisplay Uninstaller.app'
subprocess.run(['/usr/bin/osacompile', '-o', str(uninstaller),
                str(ROOT / 'installers/macos/Uninstaller.applescript')], check=True)
uninstall_info_path = uninstaller / 'Contents/Info.plist'
uninstall_info = plistlib.loads(uninstall_info_path.read_bytes())
uninstall_info.update(CFBundleIdentifier='dev.thunderdisplay.uninstaller', CFBundleName='ThunderDisplay Uninstaller',
                      CFBundleShortVersionString=version, CFBundleVersion=info['CFBundleVersion'],
                      LSMinimumSystemVersion='13.0', CFBundleIconFile='ThunderDisplay')
uninstall_info_path.write_bytes(plistlib.dumps(uninstall_info))
shutil.copyfile(app / 'Contents/Resources/ThunderDisplay.icns', uninstaller / 'Contents/Resources/ThunderDisplay.icns')
subprocess.run(['python3', str(ROOT / 'scripts/sign-mac.py'), str(uninstaller)], check=True)

scripts = work / 'scripts'
shutil.copytree(ROOT / 'installers/macos/scripts', scripts)
for script in scripts.iterdir(): script.chmod(0o755)
components = work / 'components.plist'
subprocess.run(['/usr/bin/pkgbuild', '--analyze', '--root', str(payload), str(components)], check=True)
entries = plistlib.loads(components.read_bytes())
for entry in entries:
    entry.update(BundleIsRelocatable=False, BundleHasStrictIdentifier=True, BundleIsVersionChecked=True,
                 BundleOverwriteAction='upgrade')
components.write_bytes(plistlib.dumps(entries))
component = work / 'ThunderDisplay.pkg'
subprocess.run(['/usr/bin/pkgbuild', '--root', str(payload), '--install-location', '/',
                '--component-plist', str(components), '--identifier', 'dev.thunderdisplay.installer',
                '--version', version, '--ownership', 'recommended', '--scripts', str(scripts), str(component)], check=True)

resources = work / 'Resources'
resources.mkdir()
shutil.copyfile(ROOT / 'LICENSE', resources / 'License.txt')
texts = {
    'zh_CN.lproj': (
        '安装 ThunderDisplay',
        '将 Mac 桌面通过本地雷雳网桥传输到 Windows。',
        '应用安装到“应用程序”，同时安装登录前、桌面和开机组件。升级会保留连接配置和权限身份。',
        '安装完成',
        'ThunderDisplay 已安装并启动，完成屏幕录制与辅助功能授权后即可连接。',
        '使用菜单栏 ThunderDisplay 图标打开设置；卸载请使用其菜单里的“卸载 ThunderDisplay…”，或打开“应用程序”中的 ThunderDisplay Uninstaller。'),
    'en.lproj': (
        'Install ThunderDisplay',
        'Stream your Mac desktop to Windows over the local Thunderbolt Bridge.',
        'Installs the apps in Applications, plus boot, login-screen and desktop startup components. Upgrades retain connection configuration and signing identity.',
        'Installation complete',
        'ThunderDisplay is installed and running. Grant screen recording and accessibility access to connect.',
        'Open settings from the ThunderDisplay menu bar icon. To uninstall, choose Uninstall ThunderDisplay from its menu or open ThunderDisplay Uninstaller in Applications.')
}
for language, text in texts.items():
    folder = resources / language
    folder.mkdir()
    for filename, offset in [('Welcome.html', 0), ('Conclusion.html', 3)]:
        title, first, second = text[offset:offset+3]
        (folder / filename).write_text(f'''<!doctype html><html><head><meta charset="utf-8"><style>
body {{ font: 15px -apple-system, sans-serif; margin: 28px; line-height: 1.65; }}
h1 {{ font-size: 25px; line-height: 1.25; }}
@media (prefers-color-scheme: dark) {{ body {{ color: #eee; }} }}
</style></head><body><h1>{title}</h1><p>{first}</p><p>{second}</p></body></html>''')
distribution = work / 'Distribution.xml'
distribution.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>ThunderDisplay {version}</title>
  <welcome file="Welcome.html" mime-type="text/html"/>
  <license file="License.txt" mime-type="text/plain"/>
  <conclusion file="Conclusion.html" mime-type="text/html"/>
  <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
  <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
  <allowed-os-versions><os-version min="13.0"/></allowed-os-versions>
  <choices-outline><line choice="main"/></choices-outline>
  <choice id="main" title="ThunderDisplay" visible="false"><pkg-ref id="dev.thunderdisplay.installer"/></choice>
  <pkg-ref id="dev.thunderdisplay.installer" version="{version}" onConclusion="none">ThunderDisplay.pkg</pkg-ref>
</installer-gui-script>
''')
ET.parse(distribution)
output = ROOT / 'dist' / f'ThunderDisplay-Mac-Setup-{version}-arm64.pkg'
output.parent.mkdir(exist_ok=True)
staged = work / output.name
subprocess.run(['/usr/bin/productbuild', '--distribution', str(distribution), '--resources', str(resources),
                '--package-path', str(work), str(staged)], check=True)

# Extract the actual PKG payload before publishing it. Verify installed bytes,
# executable permissions, upgrade placement, scripts and signature resources.
expanded = work / 'expanded'
subprocess.run(['/usr/sbin/pkgutil', '--expand-full', str(staged), str(expanded)], check=True)
installed_payload = expanded / 'ThunderDisplay.pkg/Payload/Applications'
for bundle in (applications / app.name, uninstaller):
    for source in bundle.rglob('*'):
        if source.is_file():
            packed = installed_payload / source.relative_to(applications)
            assert packed.read_bytes() == source.read_bytes(), f'Installer payload mismatch: {source.name}'
            if source.stat().st_mode & 0o111: assert packed.stat().st_mode & 0o111
for filename in ('preinstall', 'postinstall'):
    assert (expanded / 'ThunderDisplay.pkg/Scripts' / filename).read_bytes() == (scripts / filename).read_bytes()
package_info = ET.parse(expanded / 'ThunderDisplay.pkg/PackageInfo').getroot()
for bundle in package_info.findall('bundle'):
    assert bundle.get('path', '').removeprefix('./').startswith('Applications/')
assert all(len(node) == 0 for node in package_info.findall('relocate')), 'Installer must use fixed Applications paths'
shutil.copyfile(staged, output)
for old in output.parent.iterdir():
    obsolete = re.fullmatch(r'ThunderDisplay-Mac-(?:Setup-)?[0-9.]+-arm64\.(?:zip|pkg)', old.name)
    if old.is_file() and obsolete and old != output: old.unlink()
legacy_app = ROOT / 'dist/ThunderDisplayHost.app'
if legacy_app.exists(): shutil.rmtree(legacy_app)
print(f'Mac installer verified: {output}')
print(f'Host payload SHA256: {sha256((app / "Contents/MacOS/ThunderDisplayHost").read_bytes()).hexdigest()}')
