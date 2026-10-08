#!/usr/bin/env python3
"""Validate the GUI and build a localized, installable NSIS distribution."""
from hashlib import sha256
from pathlib import Path
import re
import struct
import shutil
import subprocess
import os

ROOT = Path(__file__).resolve().parents[1]


def validate_gui(data, version):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Not a Windows executable")
    pe = struct.unpack_from("<I", data, 60)[0]
    if pe + 94 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header")
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
        raise ValueError("Expected a Windows x64 executable")
    if struct.unpack_from("<H", data, pe + 24 + 68)[0] != 2:
        raise ValueError("Refusing to package a console executable: Windows GUI is required")
    legacy = b"Supply the 32 character pairing code from the Mac menu: TD > Pairing information"
    if legacy in data:
        raise ValueError("Legacy mandatory-token entry point found")
    if b"Cursor: live macOS system cursor (video cursor hidden)" not in data:
        raise ValueError("Missing system cursor build marker")
    if b"Mouse: fullscreen crosses Mac displays; windowed returns to Windows" not in data:
        raise ValueError("Missing fullscreen multi-display pointer support")
    for marker in (b"Raw ten-bit plane texture", b"Uncompressed P010 / SDR 10-bit", b"Uncompressed packed10 / SDR 10-bit", b"Cursor: independent macOS cursor (multi-display control)"):
        if marker not in data:
            raise ValueError(f"Missing uncompressed / independent cursor support: {marker!r}")
    for marker in (b"receiver=independent", b"pending_replaced=", b"Raw video buffer pool exhausted"):
        if marker not in data:
            raise ValueError(f"Missing pipelined raw receiver: {marker!r}")
    for marker in (b"Uncompressed changed regions / SDR 10-bit", b"Invalid raw update baseline", b"exact-updates"):
        if marker not in data:
            raise ValueError(f"Missing exact changed-region transmission: {marker!r}")
    for marker in (b"upload=changed-planes", b"snapshot_copy_bytes_avg=", b"processed_P010_bytes_avg=", b"video.raw.gpu.check", b"MMCSS Playback/high"):
        if marker not in data:
            raise ValueError(f"Missing partial raw pixel processing: {marker!r}")
    for marker in (b"phase=before_connect", b"video.raw.startup", b"first_complete_frame_bytes=", b"raw receiver above-normal; nonblocking socket polling"):
        if marker not in data:
            raise ValueError(f"Missing first-frame connection recovery: {marker!r}")
    for marker in (b"video.performance", b"present_fps=", b"dxgi_display_fps=", b"Invalid diagnostic sharing acknowledgment"):
        if marker not in data:
            raise ValueError(f"Missing opt-in Windows-to-Mac diagnostics: {marker!r}")
    if b"Explicit sRGB shader conversion" not in data:
        raise ValueError("Missing desktop color conversion build marker")
    if b"ThunderDisplay handover" not in data or b"Invalid session transition notice" not in data:
        raise ValueError("Missing authenticated session handover / retained GPU frame support")
    if b"brief video interruption" not in data or b"display.frame.gap" not in data or b"display.resize" not in data:
        raise ValueError("Missing retained live-frame repaint / local display diagnostics")
    if b"video.latency" not in data or b"Initialize decoder apartment" not in data:
        raise ValueError("Missing isolated media worker / stage latency diagnostics")
    if b"Initialize presentation apartment" not in data or b"arrival_to_present_us_avg/max" not in data:
        raise ValueError("Missing latest-picture presenter / real frame-age measurements")
    if "正在进入桌面".encode("utf-16-le") in data:
        raise ValueError("Login handover must retain the frame without a text overlay")
    for preset in ("缩放与边缘", "1:1 原始像素显示（窗口较小时裁剪画面）", "60 Hz · 高画质", "120 Hz · 超清", "165 Hz · 超清", "240 Hz · 低延迟"):
        if preset.encode("utf-16-le") in data:
            raise ValueError("Fixed FPS presets must not appear in the GUI")
    for control in ("重连已停止，请手动重新连接。", "控制通道已建立，等待首帧…", "视频码率", "请输入有效码率", "请修正视频码率后再连接。", "网桥速率尚未检测到，码率上限暂用 20 Gbps。连接后按实际网卡重新核对。", "本地指针 · macOS 原生（同步系统形状与点击热点）"):
        if control.encode("utf-16-le") not in data:
            raise ValueError(f"Missing control: {control}")
    for text in (f"ThunderDisplay {version} · 连接 Mac", "配对码", "显示模式", "连接 Mac", "全屏快捷键", "串流色深", "清晰度优先：保留 Mac HiDPI 像素（自动模式）", "双向剪贴板：文字与图片（连接后复制）", "调试：将性能日志发送到 Mac（可在连接中切换）"):
        if text.encode("utf-16-le") not in data:
            raise ValueError(f"Missing GUI version/control: {text}")
    if "拖动调整日志高度".encode("utf-16-le") not in data or "LogPanelHeight".encode("utf-16-le") not in data:
        raise ValueError("Missing resizable diagnostics panel / saved height")


def main():
    header = (ROOT / "windows-client/version.h").read_text()
    version = re.search(r'#define TD_VERSION_TEXT "([0-9.]+)"', header).group(1)
    source = ROOT / "build/package/windows-x64"
    (source / "licenses").mkdir(parents=True, exist_ok=True)
    (source / "licenses/ThunderDisplay-AGPL-3.0.txt").write_bytes((ROOT / "LICENSE").read_bytes())
    data = (source / "ThunderDisplayClient.exe").read_bytes()
    validate_gui(data, version)
    for marker in (b"ThunderDisplay.ExitForInstaller.v1", b"window.return_to_setup", b"window.restore_remote"):
        encoded = marker.decode().encode("utf-16-le") if marker.startswith(b"ThunderDisplay.") else marker
        if encoded not in data:
            raise ValueError(f"Missing tray / recovery build marker: {marker!r}")
    compiler = shutil.which("makensis")
    if not compiler:
        for candidate in (Path("C:/Program Files (x86)/NSIS/makensis.exe"), Path("C:/Program Files/NSIS/makensis.exe")):
            if candidate.is_file():
                compiler = str(candidate)
                break
    if not compiler:
        raise RuntimeError("Install NSIS 3 and add makensis to PATH")
    output = ROOT / "dist" / f"ThunderDisplay-Windows-Setup-{version}-x64.exe"
    output.parent.mkdir(exist_ok=True)
    staged = source.parent / output.name
    installed_kb = (sum(path.stat().st_size for path in source.rglob('*') if path.is_file()) + 1023) // 1024
    prefix = "/" if os.name == "nt" else "-"
    subprocess.run([compiler, f"{prefix}V3", f"{prefix}DTD_VERSION={version}",
                    f"{prefix}DTD_INSTALLED_KB={installed_kb}",
                    f"{prefix}DTD_SOURCE={source}", f"{prefix}DTD_ROOT={ROOT}", f"{prefix}DTD_OUTPUT={staged}",
                    str(ROOT / "installers/windows/ThunderDisplay.nsi")], check=True)
    if staged.stat().st_size < 100000 or staged.read_bytes()[:2] != b"MZ":
        raise ValueError("Invalid installer output")
    extractor = shutil.which("7zz") or shutil.which("7z")
    if not extractor:
        raise RuntimeError("Install 7-Zip and add 7zz or 7z to PATH for installer payload verification")
    extracted = source.parent / "windows-installer-verify"
    if extracted.exists(): shutil.rmtree(extracted)
    subprocess.run([extractor, "x", "-y", f"-o{extracted}", str(staged)], check=True, stdout=subprocess.DEVNULL)
    clients = list(extracted.rglob("ThunderDisplayClient.exe"))
    if len(clients) != 1 or clients[0].read_bytes() != data:
        raise ValueError("Windows installer embeds the wrong client")
    if not list(extracted.rglob("*ninstall.exe")):
        raise ValueError("Windows installer is missing its uninstaller")
    digest = sha256(data).hexdigest()
    shutil.copyfile(staged, output)
    # Retire old versions only after the new package has passed verification.
    # Do not create an unversioned duplicate archive.
    for item in (ROOT / "dist").iterdir():
        known_build = re.fullmatch(r"ThunderDisplay-GUI-[0-9.]+-x64\.exe", item.name) or re.fullmatch(
            r"ThunderDisplay-Windows-GUI-[0-9.]+-x64\.zip", item.name) or re.fullmatch(
            r"ThunderDisplay-Windows-Setup-[0-9.]+-x64\.exe", item.name)
        legacy = item.name in ("ThunderDisplay-Windows-x64.zip", ".DS_Store")
        if item.is_file() and (known_build or legacy) and item != output:
            item.unlink()
    legacy_folder = ROOT / "dist/windows-x64"
    if legacy_folder.exists(): shutil.rmtree(legacy_folder)
    print(f"Windows installer built: {output}")
    print(f"Client payload SHA256: {digest}")


if __name__ == "__main__":
    main()
