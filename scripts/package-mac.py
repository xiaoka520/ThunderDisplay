#!/usr/bin/env python3
"""Validate and package the current signed Mac app; retire older ZIPs afterward."""
from hashlib import sha256
from pathlib import Path
import plistlib
import re
import struct
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
app = ROOT / 'dist/ThunderDisplayHost.app'
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
version = info['CFBundleShortVersionString']
client = re.search(r'#define TD_VERSION_TEXT "([0-9.]+)"', (ROOT / 'windows-client/version.h').read_text()).group(1)
assert version == client, 'Update both platforms together'
assert info['CFBundleIdentifier'] == 'dev.thunderdisplay.host'
assert (app / 'Contents/_CodeSignature/CodeResources').stat().st_mode & 0o444 == 0o444, 'Public signature resource seal must be readable'
for name in ['ThunderDisplayHost', 'ThunderDisplayBoot']:
    path = app / 'Contents/MacOS' / name
    data = path.read_bytes()
    assert data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 4)[0] == 0x100000c, 'Expected arm64 Mach-O'
    assert path.stat().st_mode & 0o111, 'Missing executable permissions'
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
desktop = plistlib.loads((app / 'Contents/Resources/dev.thunderdisplay.desktop.plist').read_bytes())
assert desktop['Label'] == 'dev.thunderdisplay.desktop' and desktop['RunAtLoad'] is True
assert desktop['LimitLoadToSessionType'] == 'Aqua' and 'UserName' not in desktop
assert desktop['ProgramArguments'] == ['/Library/Application Support/ThunderDisplay/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost', '--background']
archive = ROOT / 'dist' / f'ThunderDisplay-Mac-{version}-arm64.zip'
files = [p for p in sorted(app.rglob('*')) if p.is_file()]
for path in files:
    assert not any(part in ['.local-signing', '.DS_Store'] for part in path.parts)
    assert path.suffix not in ['.p12', '.keychain', '.keychain-db', '.pem'], 'Private signing material must not be packaged'
with ZipFile(archive, 'w', ZIP_DEFLATED) as package:
    for path in files: package.write(path, path.relative_to(app.parent).as_posix())
    package.writestr('START-HERE-Mac.txt', f'''ThunderDisplay {version} · Mac 主机
放在固定位置后启动 .app，完成屏幕录制与键鼠权限。在设置中启用“登录时打开 ThunderDisplay”，系统登录项列表可见。
新版自动迁移旧登录服务。自启动配置与当前主机运行状态分开显示；待批准不算启用。
App 启动即自动运行主机，唤醒或显示器恢复后自动重建，不用手动点击启动。
默认开启“保持 Mac 可连接”，防止系统自动睡眠；屏幕可熄灭，开关可关闭。
雷雳不支持网络唤醒；手动让 Mac 睡眠后需先唤醒 Mac，随后主机自动恢复。
随系统启动需系统管理员确认，安装独立发现daemon与LoginWindow图形组件；检查有效帧和输入授权后接受连接，登录后桌面主机接管。
0.8.2 登录前键鼠改用系统HID输入；安装后点击“更新开机组件与连接配置”更新系统副本。仅替换桌面App不足以更新登录前程序。
0.8.3 移除登录前输入准备的全局状态查询，增加独立超时恢复与分阶段日志；需要更新开机组件。
0.8.6 将授权备用键盘定向投递给系统登录进程；鼠标保留原路径。Windows会话恢复时重新建立按键捕获。交接保留最后一帧、不加提示文字。需要更新开机组件，真实冷启动键盘仍需复测。
0.8.4 先释放交接端口再清理采集，增加限定用户的Aqua桌面启动任务；需更新开机组件。HID拒绝本地用户上下文时，仅在Quartz事件发送授权有效后尝试标准输入路径，冷启动控制仍需实测。
0.7.5 修复旧开机注册在重启时抢占任务的问题；已有配置需重新启用此项以迁移，不能只替换 .app。
FileVault 解锁不支持；macOS 登录界面捕获与控制仍待真实未登录会话验收，安装成功不等于已经验证可用。
两端更新 {version} 后可用动态原生指针、文字 / 图片剪贴板与 20 Gbps 请求上限。
0.7.6 动态指针优先使用 macOS 的高密度原图，逻辑大小与点击热点不放大；Windows 需同时更新。
0.7.7 自动选择匹配 Windows 显示缩放的原生指针倍率，存在对应倍率时直接显示原像素。
自动模式降低压缩强度，推荐160–1000 Mbps；高预算连接尝试限制帧QP以保留细节。
两端更新后自动采用sRGB桌面色彩链路，仍为SDR/4:2:0，尚未实现P3/HDR。
原生深浅色跟随系统。上限不是实测吞吐；硬件接受目标见 Windows 连接诊断。
Login startup and the boot discovery helper are configurable in the Mac app.
The boot helper does not capture loginwindow or FileVault preboot.
The separate LoginWindow graphics agent verifies actual frames and input authorization before serving; pre-login remote control still requires real-session validation.
Version 0.8.2 uses system HID input before login; update startup components in the Mac settings to update the system copy.
Update both platforms to {version}. Native Windows GPU/cursor/theme behavior requires the ROG.
''')
    package.writestr('SHA256SUMS.txt', ''.join(f'{sha256((app / "Contents/MacOS" / name).read_bytes()).hexdigest()}  ThunderDisplayHost.app/Contents/MacOS/{name}\n' for name in ['ThunderDisplayHost','ThunderDisplayBoot']))
with ZipFile(archive) as package:
    assert package.testzip() is None, 'ZIP integrity check failed'
    for path in files: assert package.read(path.relative_to(app.parent).as_posix()) == path.read_bytes()
    for name in ['ThunderDisplayHost','ThunderDisplayBoot']:
        assert package.getinfo(f'ThunderDisplayHost.app/Contents/MacOS/{name}').external_attr >> 16 & 0o111
for path in app.parent.iterdir():
    if path.is_file() and re.fullmatch(r'ThunderDisplay-Mac-[0-9.]+-arm64\.zip', path.name) and path != archive: path.unlink()
print(f'Mac package verified: {archive}')
print(f'Host SHA256: {sha256((app / "Contents/MacOS/ThunderDisplayHost").read_bytes()).hexdigest()}')
