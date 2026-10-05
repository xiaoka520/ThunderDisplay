#!/usr/bin/env python3
"""Package the GUI EXE under a versioned name and reject console/stale builds."""
from hashlib import sha256
from pathlib import Path
import re
import struct
from zipfile import ZipFile, ZIP_DEFLATED

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
    if b"Explicit sRGB shader conversion" not in data:
        raise ValueError("Missing desktop color conversion build marker")
    for preset in ("缩放与边缘", "1:1 原始像素显示（窗口较小时裁剪画面）", "60 Hz · 高画质", "120 Hz · 超清", "165 Hz · 超清", "240 Hz · 低延迟"):
        if preset.encode("utf-16-le") in data:
            raise ValueError("Fixed FPS presets must not appear in the GUI")
    for control in ("重连已停止，请手动重新连接。", "控制通道已建立，等待首帧…", "自定义码率", "本地指针 · macOS 原生（同步系统形状与点击热点）"):
        if control.encode("utf-16-le") not in data:
            raise ValueError(f"Missing control: {control}")
    for text in (f"ThunderDisplay {version} · 连接 Mac", "配对码", "显示模式", "连接 Mac", "全屏快捷键", "串流色深", "清晰度优先：保留 Mac HiDPI 像素（自动模式）", "双向剪贴板：文字与图片（连接后复制）"):
        if text.encode("utf-16-le") not in data:
            raise ValueError(f"Missing GUI version/control: {text}")


def main():
    header = (ROOT / "windows-client/version.h").read_text()
    version = re.search(r'#define TD_VERSION_TEXT "([0-9.]+)"', header).group(1)
    source = ROOT / "dist/windows-x64"
    data = (source / "ThunderDisplayClient.exe").read_bytes()
    validate_gui(data, version)
    folder = f"ThunderDisplay-GUI-{version}-x64"
    filename = f"ThunderDisplay-GUI-{version}-x64.exe"
    standalone = ROOT / "dist" / filename
    standalone.write_bytes(data)
    archive = ROOT / "dist" / f"ThunderDisplay-Windows-GUI-{version}-x64.zip"
    digest = sha256(data).hexdigest()
    with ZipFile(archive, "w", ZIP_DEFLATED) as package:
        package.writestr(f"{folder}/{filename}", data)
        package.writestr(f"{folder}/SHA256SUMS.txt", f"{digest}  {filename}\n")
        package.write(source / "START-HERE.txt", f"{folder}/START-HERE.txt")
        for item in sorted((source / "licenses").rglob("*")):
            if item.is_file():
                package.write(item, f"{folder}/{item.relative_to(source).as_posix()}")
    with ZipFile(archive) as package:
        if package.testzip() is not None:
            raise ValueError("Package integrity failed")
        packed = package.read(f"{folder}/{filename}")
        validate_gui(packed, version)
        if packed != data or sha256(standalone.read_bytes()).hexdigest() != digest:
            raise ValueError("Packaged EXE differs from the verified GUI build")
    # Retire old versions only after the new package has passed verification.
    # Do not create an unversioned duplicate archive.
    for item in (ROOT / "dist").iterdir():
        known_build = re.fullmatch(r"ThunderDisplay-GUI-[0-9.]+-x64\.exe", item.name) or re.fullmatch(
            r"ThunderDisplay-Windows-GUI-[0-9.]+-x64\.zip", item.name)
        legacy = item.name in ("ThunderDisplay-Windows-x64.zip", ".DS_Store")
        if item.is_file() and (known_build or legacy) and item not in (archive, standalone):
            item.unlink()
    print(f"GUI package verified: {archive}")
    print(f"Standalone EXE: {standalone}")
    print(f"EXE SHA256: {digest}")


if __name__ == "__main__":
    main()
