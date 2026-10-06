#!/bin/bash
# ThunderDisplay 登录前运行诊断脚本

echo "========================================="
echo "ThunderDisplay 登录前组件诊断"
echo "========================================="
echo ""

# 1. 检查当前用户状态
echo "1. 当前会话状态："
if [ "$(whoami)" = "root" ]; then
    echo "   ✓ 以 root 运行"
else
    echo "   当前用户: $(whoami)"
fi

CONSOLE_USER=$(stat -f%Su /dev/console)
if [ "$CONSOLE_USER" = "root" ]; then
    echo "   ✓ 当前在登录界面（无用户登录）"
else
    echo "   当前已登录用户: $CONSOLE_USER"
fi
echo ""

# 2. 检查组件安装
echo "2. 组件安装状态："

if [ -f "/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist" ]; then
    echo "   ✓ 系统守护进程配置已安装"
else
    echo "   ✗ 系统守护进程配置缺失"
fi

if [ -f "/Library/LaunchAgents/dev.thunderdisplay.loginwindow.plist" ]; then
    echo "   ✓ LoginWindow agent 配置已安装"
else
    echo "   ✗ LoginWindow agent 配置缺失"
fi

if [ -f "/Library/LaunchAgents/dev.thunderdisplay.desktop.plist" ]; then
    echo "   ✓ Desktop agent 配置已安装"
else
    echo "   ✗ Desktop agent 配置缺失"
fi

if [ -f "/Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system" ]; then
    echo "   ✓ Boot 二进制已安装"
    BOOT_SIZE=$(ls -lh /Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system | awk '{print $5}')
    echo "     大小: $BOOT_SIZE"
else
    echo "   ✗ Boot 二进制缺失"
fi
echo ""

# 3. 检查雷电网桥
echo "3. 雷电网桥状态："
BRIDGE_IP=$(ifconfig | grep -A 1 "^bridge" | grep "inet " | head -1 | awk '{print $2}')
if [ -n "$BRIDGE_IP" ]; then
    echo "   ✓ 雷电网桥已连接"
    echo "     IP 地址: $BRIDGE_IP"
else
    echo "   ✗ 未找到雷电网桥 IPv4 地址"
    echo "     请检查雷电线连接和网络配置"
fi
echo ""

# 4. 运行 Boot 组件自检
echo "4. Boot 组件自检："
if [ -f "dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayBoot" ]; then
    ./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayBoot --check
else
    echo "   找不到 ThunderDisplayBoot，请先编译"
fi
echo ""

# 5. 检查权限（仅桌面会话）
if [ "$CONSOLE_USER" != "root" ]; then
    echo "5. 屏幕捕获权限检查（桌面会话）："
    
    # 尝试查询 TCC 数据库（需要特殊权限）
    TCC_DB="/Library/Application Support/com.apple.TCC/TCC.db"
    if [ -f "$TCC_DB" ]; then
        echo "   注意: 桌面会话的权限 ≠ 登录界面会话的权限"
        echo "   登录前权限需要在登录界面首次连接时授予"
    fi
    echo ""
fi

# 6. 检查进程状态
echo "6. 运行进程状态："
if pgrep -x ThunderDisplayHost > /dev/null; then
    PID=$(pgrep -x ThunderDisplayHost)
    echo "   ✓ ThunderDisplayHost 正在运行 (PID: $PID)"
else
    echo "   ThunderDisplayHost 未运行"
fi

if pgrep -x ThunderDisplayBoot > /dev/null; then
    PID=$(pgrep -x ThunderDisplayBoot)
    echo "   ✓ ThunderDisplayBoot 正在运行 (PID: $PID)"
else
    echo "   ThunderDisplayBoot 未运行"
fi
echo ""

# 7. 建议
echo "========================================="
echo "诊断建议："
echo "========================================="

if [ ! -f "/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist" ] || \
   [ ! -f "/Library/LaunchAgents/dev.thunderdisplay.loginwindow.plist" ]; then
    echo ""
    echo "⚠️  开机组件未安装"
    echo "   解决方法："
    echo "   1. 运行桌面主机: ./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost"
    echo "   2. 勾选'随系统启动'"
    echo "   3. 点击'更新开机组件与连接配置'"
    echo "   4. 输入管理员密码"
fi

if [ -z "$BRIDGE_IP" ]; then
    echo ""
    echo "⚠️  雷电网桥不可用"
    echo "   解决方法："
    echo "   1. 检查雷电线是否连接"
    echo "   2. 检查两端网络设置（应该有 169.254.x.x 地址）"
    echo "   3. 尝试拔插雷电线"
fi

if [ "$CONSOLE_USER" != "root" ]; then
    echo ""
    echo "ℹ️  当前已登录到桌面"
    echo "   要测试登录前功能，需要："
    echo "   1. 完整重启 Mac"
    echo "   2. 停在登录界面（不输入密码）"
    echo "   3. 从 Windows 尝试连接"
    echo "   4. 在 Mac 屏幕上授予权限（首次会弹窗）"
fi

echo ""
echo "========================================="
echo "详细日志查看命令："
echo "========================================="
echo ""
echo "# 查看最近 30 分钟的日志"
echo "log show --predicate 'subsystem == \"dev.thunderdisplay.host\"' --last 30m --style compact | tail -100"
echo ""
echo "# 查看登录前组件的错误"
echo "log show --predicate 'subsystem == \"dev.thunderdisplay.host\" AND category == \"loginwindow\"' --last 30m"
echo ""
echo "# 实时监控日志"
echo "log stream --predicate 'subsystem == \"dev.thunderdisplay.host\"'"
echo ""
echo "========================================="
