#!/bin/bash
set -e

echo "=========================================="
echo "ThunderDisplay 启动注册修复工具"
echo "=========================================="
echo ""

# 1. 停止旧版本
echo "1️⃣ 停止旧版本..."
pkill -f ThunderDisplayHost || echo "   没有运行中的进程"
sleep 1

# 2. 清理 BTM 系统中的旧注册（需要手动确认）
echo ""
echo "2️⃣ 检测到系统中的注册项："
sfltool dumpbtm 2>/dev/null | grep -B 2 -A 8 "ThunderDisplay" || echo "   无法查询 BTM"

echo ""
echo "⚠️  发现多个注册项冲突！"
echo "   需要清理系统后台任务管理器中的旧项目。"
echo ""
echo "📋 请手动执行以下步骤："
echo "   1. 打开：系统设置 → 通用 → 登录项与扩展"
echo "   2. 找到所有 ThunderDisplayHost 项目"
echo "   3. 将所有项目都关闭并删除"
echo "   4. 完成后回车继续..."
read -p ""

# 3. 移除 /Applications 中的旧版本
echo ""
echo "3️⃣ 移除 /Applications 中的旧版本..."
if [ -d "/Applications/ThunderDisplayHost.app" ]; then
    echo "   发现旧版本，准备移除..."
    rm -rf "/Applications/ThunderDisplayHost.app"
    echo "   ✅ 已删除"
else
    echo "   没有找到旧版本"
fi

# 4. 安装新版本到 /Applications
echo ""
echo "4️⃣ 安装新版本到 /Applications..."
cp -r "dist/ThunderDisplayHost.app" "/Applications/"
echo "   ✅ 已安装"

# 5. 验证新版本
echo ""
echo "5️⃣ 验证新版本..."
NEW_SIZE=$(/Applications/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost --version 2>&1 | wc -c || echo "0")
if [ "$NEW_SIZE" -gt "0" ]; then
    echo "   ✅ 新版本可执行"
else
    echo "   ⚠️ 新版本可能有问题"
fi

echo ""
echo "=========================================="
echo "✅ 清理完成！"
echo "=========================================="
echo ""
echo "📋 下一步："
echo "   1. 启动应用: open /Applications/ThunderDisplayHost.app"
echo "   2. 在应用界面中勾选'随系统启动'"
echo "   3. 这次应该可以成功注册了"
echo ""
