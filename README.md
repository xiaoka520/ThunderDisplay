# ThunderDisplay

为 **Mac mini → 雷雳网桥 → Windows ROG** 编写的本地远程显示器 MVP。

Mac 使用 Swift / ScreenCaptureKit / VideoToolbox；Windows 使用 C++17 / Media Foundation / Direct3D 11。
运行不依赖互联网、云账户、Moonlight、Sunshine 或第三方视频库。

## 当前实现

- Mac 全屏捕获；可选 Windows 本地显示 macOS 当前系统指针，动态同步形状、隐藏状态与原生点击热点，串流不重复绘制指针。
- HEVC Main / Main10 / H.264 High 硬件编码，8 / 10-bit 4:2:0，sRGB SDR（旧端 BT.709 兼容）；禁止 B 帧，实时编码，0.5 秒 GOP。
- 默认自动协商：检测 Mac 源渲染像素与 Windows 显示器原生尺寸 / 最大可用刷新率，保留源比例、避免放大，自动选择高码率。手动支持 1–240 FPS。
- TCP 配置、可选配对验证、心跳、键鼠；UDP Annex B 视频，单包最多 1200 字节。
- Windows D3D11 硬件解码和 GPU 色彩转换，保留宽高比，全屏、窗口缩放。
- 全屏捕获时 Win → Command、Alt → Option、Ctrl → Control；鼠标移动、三键、拖动、双向滚轮。
- 配对码验证默认关闭，可在 Mac 开启；关闭时仅接收选定接口同子网的连接。每连接随机视频会话、单客户端控制。
- 有界队列，过期 / 丢失帧后请求 IDR，跳过依赖缺失的 P 帧。
- HEVC 不可用时自动重连改用 H.264；断线自动重连；失焦 / 断线释放按键。
- Mac 自动选择已有 IPv4 的雷雳网桥；Windows 从本机 Ethernet 接口广播发现，也可显式指定 IP。
- Mac 圆角卡片界面，区分桌面逻辑尺寸、HiDPI 捕获源像素和最大可用模式，可选择捕获显示器；一次启动申请屏幕录制和辅助功能，自动检查授权，无权限 / 无网桥也能继续设置。
- Windows 圆角卡片和控件，随窗口尺寸 / DPI 调整，可选择目标屏幕并检测原生尺寸、最大可用模式、刷新率和 HDR 状态；中文 / 英文跟随系统，也支持手动切换。
- Windows 保存连接参数；配对码仅在勾选“记住”时使用当前 Windows 用户的 DPAPI 加密保存。
- 两端随系统切换深浅色；Mac 原生语义颜色，Windows 原生标题栏 / 控件主题与高对比度支持。
- Mac 原生登录项与独立系统启动组件：系统 daemon负责发现，LoginWindow agent负责登录界面捕获与输入检查，通过后接受串流，登录后桌面主机接管。登录前实机验收仍待完成；安装不代表已获得系统许可。
- 双向文字与图片剪贴板（UTF-8 / PNG / macOS TIFF / Windows DIB），不传文件。

GUI 显示模式仅“自动”和“自定义”，不再提供固定 FPS 档位或帧率输入；帧率取 Mac 当前刷新率与选定 Windows 显示模式检测值的较小值（程序最高 240）。这些帧率是 **请求上限**，不等于已经测得的实际输出。ScreenCaptureKit 的捕获能力、Mac 当前显示模式、编码吞吐量、Windows GPU 和面板刷新率都会影响结果。静止桌面不按目标 FPS 重复发送相同画面，但首次连接和丢包恢复可以从最新捕获表面生成 IDR。自动模式在连接时协商；运行中根据丢包 / 负载动态调整画质尚未实现，CLI 的旧模式别名仅保留兼容，GUI 不显示这些档位。

## 界面、色深与像素显示（0.7.4）

Windows 设置窗口改为独立文档子窗口：滚动只移动整张页面，控件在页面内的位置不变，再统一合成重绘。Windows 0.6.1 修复焦点保留在下方字段时，向上滚动又被拉回的问题；仅在焦点切换时定位，关闭的下拉框不吞滚轮或改值，诊断文本到边缘后继续滚动整页，并累积高精度滚轮的小增量。Mac 授权状态改为 34 px 高的圆角徽章。Windows 原生滚动效果仍需 ROG 实机验收；最新两端均为 0.8.1。

“串流色深”提供自动 / 8-bit SDR / 10-bit SDR。自动模式在 Mac Main10 硬件编码、Windows 当前 10-bit 输出及 HEVC 条件满足时尝试 10-bit；捕获、GPU 解码或 GPU 转换不支持时重连回退到 8-bit。强制 10-bit 会明确报错。Mac 捕获 RGB10，以 VideoToolbox 转为 P010 并用 HEVC Main10 编码；Windows 请求 P010 解码并输出 R10G10B10A2，禁止在 10-bit 路径中悄悄输出 NV12 / BGRA8。参见 [Microsoft HEVC 解码器及 Main10 / P010 要求](https://learn.microsoft.com/en-us/windows/win32/medfound/h-265---hevc-video-decoder)。

新版两端串流为 sRGB SDR、BT.709 YCbCr 矩阵、4:2:0；旧端保持 BT.709 兼容，尚不支持 P3、HDR 和 4:4:4。10-bit 改善渐变精度，不增加分辨率或恢复采样丢失的彩色文字细节。连接诊断显示实际色深、色彩转换、串流像素、显示像素和缩放比例；不是按面板标称能力显示串流支持。

0.7.4 统一桌面 sRGB：Mac 捕获 sRGB BGRA8 / RGB10，经 VideoToolbox 保留 sRGB 传递函数转换为 NV12 / P010；Windows 使用显式 GPU 矩阵和范围转换，输出 full-range sRGB，减少驱动解释视频传递曲线的差异。颜色和灰阶不额外加饱和度、对比度或 Gamma。只有 query6 协商成功才启用，旧 query1–5 保持原有 BT.709 路径；新路径不支持时明确提示并以 query5 重连。

自动模式默认勾选 **清晰度优先：保留 Mac HiDPI 像素**。例如逻辑 2048×1280、渲染 4096×2560 的 Mac，以 4096×2560 串流到 2560×1600 Windows 屏幕：Mac 不提前适配 Windows 面板，客户端先按源尺寸转换为 RGB，再做两遍按缩放比例计算的 Lanczos-2 重采样。对这组 60 Hz 参数自动码率为 570 Mbps；限制最大 4096×4096；自动推荐 160–1000 Mbps，自定义上限默认 20000 Mbps（20 Gbps），不修改 Mac 显示模式。大于 2304 行时禁用曾导致连续丢帧的低延迟率控扩展，仍使用硬件、禁止 B 帧和单帧编码准入。

0.7.4 自动模式的每像素每帧预算提高到 HEVC 0.90 / H.264 1.44 bit，约为此前两倍；保留用户明确设置的自定义码率。高码率且每帧预算充足时，Mac 尝试将最大帧 QP 限制为 22，并读取编码器属性验证是否接受。属性不支持时记录原因；低预算自定义连接不强制此限制，避免为保画质持续丢帧。平均码率是目标，不是固定流量或无损承诺；仍为 4:2:0，也不改变桌面逻辑尺寸、缩放或色深。旧 Mac 未声明大帧支持时，自动预算保持其 300 Mbps 兼容上限。

关闭“保留 Mac HiDPI 像素”后沿用适配 Windows 面板的兼容策略。此开关仅作用于自动模式；需要保留 HiDPI 源像素时选择“自动”。Windows 0.6.1 移除锐化，默认使用不锐化的 Lanczos，原来保存的锐化选项也迁移到此模式；GPU 不支持时自动回退兼容缩放。GPU 着色器不可用时回退兼容显示，诊断标明原因。极小窗口需要超过 4:1 缩小时使用兼容缩放。高分辨率编码或解码失败会按 Windows 分辨率重连，之后继续执行现有色深 / 编码回退。

Windows 设置移除“缩放与边缘”和“1:1 原始像素显示”。默认保留完整桌面，维持比例，使用既有不锐化的高质量 GPU 缩放；删除控件不改变编码画质，保留 HiDPI 源像素选项。完整显示 4096×2560 到 2560×1600 仍有必要的降采样，4:2:0 仍限制彩色细字清晰度。

**双向文字与图片剪贴板** 支持中文、Emoji、换行及 PNG 图片，文字最多 64 KiB UTF-8，PNG 最多 32 MiB / 1600 万像素 / 单边 8192。Mac 将原生 TIFF 转 PNG，Windows 支持 PNG、DIBV5 / DIB / Bitmap；接收后写入系统可用的原生图片格式。两端均开启后只发送连接后新复制的内容，不导出已有内容；按变化序号和会话抑制回传。仅文本能力的旧端保持文本同步，图片需两端 0.7.0。不同格式 / 文件复制不等于图片剪贴板。

全屏默认 Ctrl+Alt+Enter，在设置中直接按组合键更改并随连接参数保存。需含 Ctrl 或 Alt，不允许 F11 或保留系统 / 释放输入组合键。远程画面全屏、获得焦点且捕获输入时，Win / Alt+Tab / Alt+F4 等 Windows 快捷键交给 Mac；Ctrl+Shift+Esc 在 UI 线程调用系统 Taskmgr.exe 唤起任务管理器（Ctrl / Shift 仍拦截，避免本机语言切换和粘滞键快捷键泄漏），Ctrl+Alt+Del 由系统处理。失焦、断线或释放输入后停止拦截，已被捕获的按键会继续吞掉对应释放事件，防止 Win key-up 弹出开始菜单。

若黑色也发灰、整个画面对比度下降，还需区分电平范围、Windows HDR 的 SDR 亮度和色彩管理问题。参见 [Microsoft Advanced Color 说明](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)。实际 ROG 色彩仍需对照验收。


自定义码率的默认最大值为 **20000 Mbps（20 Gbps）**，使用 64-bit 协商以避免溢出；自动推荐为 160–1000 Mbps。它是请求目标的上限，不是测得的网桥吞吐，也不要求静止桌面占满链路。VideoToolbox 拒绝超高目标时检测可接受范围，在 Welcome 和 Windows 诊断明确显示实际接受的目标。当前 Mac 合成检查接受 10737 Mbps，HEVC / H.264 / Main10 各三帧通过；这不是持续吞吐或20 Gbps 输出证明。新端支持最高 64 MiB 单帧，旧 query4 仍16 MiB，其他旧端4 MiB。

“本地指针 · macOS 原生”默认关闭。开启后，Mac 读取 WindowServer 当前指针的系统原始像素和热点，包括文本、调整大小、手形、截屏及应用自定义形状；不根据按键猜状态，不手绘。Windows 按本机 DPI 同步缩放形状及点击热点，不再次乘 Mac HiDPI 比例。Mac 物理屏幕指针保留，SCK 视频从第一帧起隐藏指针。读取使用运行时可选的系统私有 getter，不支持时保留视频指针；改变开关需重连。这减少移动指针等待，不改变输入网络往返。当前 Mac 只读实测可取箭头 / 文本光标，系统截屏状态及 ROG 实际点击仍需两机验收。

## 1. 配置直连网络

连接支持雷雳网络的线和两端接口，在系统里确认出现网络设备。可以在独立网段配置静态 IPv4，例如：

| 设备 | IPv4 | 子网掩码 | 网关 / DNS |
| --- | --- | --- | --- |
| Mac 雷雳网桥 | 192.168.77.1 | 255.255.255.0 | 留空 |
| Windows 雷雳 / Ethernet 网络接口 | 192.168.77.2 | 255.255.255.0 | 留空 |

这是示例网段，请避开已有网络。先在 Windows 测试 `ping 192.168.77.1`。本项目不修改系统网络配置，也不依赖自动发现替你创建网桥。自动发现失败时用 `--host` / `--bind`。

放行 Mac TCP 47990、UDP 47990（发现）及来自配对设备的视频 UDP。Windows 客户端在 TCP 路由选中的本地地址上绑定一个临时 UDP 视频端口；Mac 视频源端口也是临时端口。Windows 首次出现防火墙提示时允许该本地网络通信。

## 2. 构建和启动 Mac Host

需要 macOS 13+、Xcode / Swift 5.9+；推荐 Apple Silicon Mac。

```bash
cd /Users/caoenming/Developer/ThunderDisplay
./scripts/build-mac.sh
open dist/ThunderDisplayHost.app
```

最新 Mac 构建为 `dist/ThunderDisplay-Mac-0.8.1-arm64.zip`。首次启动会显示 **权限与连接设置窗口**，应用同时保留菜单栏 **TD** 入口。

1. 点击 **一键申请权限**，自动打开 **拖拽授权助手**，并在同一次运行中登记 **辅助功能** 与 **屏幕录制** 请求。也可以分别点击每一项的 **打开设置**，自动显示助手。助手显示当前 `.app` 的图标和完整路径；将图标拖到系统设置权限列表，打开对应开关。用助手中的两个按钮切换屏幕录制和辅助功能页面；切到系统设置后助手仍保持显示，两项授权完成后自动关闭；授权已完成但当前进程未生效时，主窗口保留重启提示。
2. 在系统设置开启两项权限，回到此窗口。应用每 1.5 秒读取当前权限，缺权限时每 5 秒静默启动新进程复查；后台复查保留原状态和提示，只有结果变化才更新界面，也能点击 **检查授权**；拒绝授权或尚未授权不会让应用退出。
3. 若 macOS 提示屏幕录制需要重启，先完成两项权限，再点击 **重启应用**。系统要求的重启不能绕过，但无需为每个权限分别打开应用。
4. 地址留空自动检测雷雳网桥，或填写本机网桥 IPv4 / 端口。权限与地址就绪后自动启动；也能手动停止 / 启动。无网桥时窗口保持打开。
5. 配对码默认关闭，Windows 只需填写网桥地址。需要验证时在 Mac 勾选 **启用配对码验证**，复制配对码，再在 Windows 勾选 **使用配对码** 并粘贴。关闭设置窗口后，用 **TD → 权限与连接设置…** 再次打开。

在设置的“自启动与无人值守”中启用登录自动启动；开机服务还会启用登录启动，本地签名构建自动弹出系统管理员确认安装。应用应放在固定位置，移动后重新启用自启动。0.8.0 同时安装独立 LoginWindow 图形组件；先验证真实画面与输入授权，通过后开放连接，失败由 Windows 显示原因。FileVault 开机解锁不支持；登录前画面及输入仍需真实未登录会话验收。

Mac 界面跟随系统深浅色与首选语言，中文系统显示中文，其他语言显示英文。IPv4 和端口设置会保留；运行服务时先停止服务再修改。

权限状态区分 **可用 / 已授权，需重启 / 当前未生效 / 检查失败**。实际检查使用 ScreenCaptureKit 启动并停止一次 64×64 捕获，不保存或传输画面。窗口显示当前应用路径，可在 Finder 定位。从 0.3.2 起，构建默认复用 `.local-signing/` 中的项目本地开发签名，避免以前 ad-hoc 重建改变身份的问题。首次从旧签名迁移，需在权限列表选中旧 ThunderDisplayHost 条目、点“−”移除，再从助手拖入当前应用并开启开关；完成两项后重启一次。`.local-signing/` 保存私有钥匙串和配置，目录 / 私密文件仅当前用户可读，不进入 Git 或发布包；签名后锁定钥匙串。请保留该目录以继续使用同一身份。该本地证书不是 Apple 分发证书 / 公证；已有正式证书时，可用 `THUNDERDISPLAY_SIGN_IDENTITY` 覆盖。

键鼠权限的申请、监测、新进程复查及转发入口统一使用 CoreGraphics 的事件发送权限 API。AX 客户端信任值仅供诊断，不再作为 CGEvent 键鼠转发的门槛。

显式指定网桥 IP：

```bash
./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost --bind 192.168.77.1
```

只读诊断，不请求屏幕或输入权限，也不开启监听：

```bash
./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost --diagnose
```

用合成画面测试实际 VideoToolbox 硬件编码器和 Annex B 输出，不捕获屏幕：

```bash
./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost --encoder-check
```

诊断采用 640×360 / 60 FPS 参数、每种编码器三个合成帧，不能作为 2560×1600 / 240 FPS 性能测试。Host 每五秒记录活跃捕获阶段的实际编码帧率和丢帧计数。

已有屏幕录制授权后，可进行一次真实桌面 → HEVC 编码检查；只记录元数据，不保存图片、不传输视频、不注入输入：

```bash
./dist/ThunderDisplayHost.app/Contents/MacOS/ThunderDisplayHost --capture-check
```

通过 `--diagnose` 查看 display ID 后，可用 `--display ID` 选择单个现有显示器。
第一版捕获已有显示器，并保留比例缩放到请求尺寸；比例不同时使用黑边，鼠标映射会扣除黑边。它不创建 2560×1600 虚拟显示器，也不修改 Mac 显示模式。

## 3. 构建和启动 Windows Client

Windows 10 1703+ / Windows 11 x64，支持 D3D11 视频解码的 GPU；安装带 **使用 C++ 的桌面开发** 和 Windows SDK 的 Visual Studio 2022+，以及 CMake 3.21+。
在项目根目录使用 PowerShell：

```powershell
cmake -S . -B build/windows -A x64
cmake --build build/windows --config Release
ctest --test-dir build/windows -C Release --output-on-failure

.\build\windows\windows-client\Release\ThunderDisplayClient.exe `
  --host 192.168.77.1 --connect `
  --mode auto --fullscreen
```

**日常使用不需要命令行。** 双击 `ThunderDisplayClient.exe` 即显示连接窗口：

最新 GUI 下载包为 `dist/ThunderDisplay-Windows-GUI-0.8.1-x64.zip`，解压后双击其中的 `ThunderDisplay-GUI-0.8.1-x64.exe`。窗口标题应显示 **ThunderDisplay 0.8.1 · 连接 Mac**。动态原生指针、图片剪贴板与 20 Gbps 协商需同时更新两端。0.8.1 修复 Windows Caps Lock 状态和键盘指示灯不变化的问题，切回远程窗口或恢复输入时同步本地状态；这项修复兼容 Mac 0.8.0，只需更新 Windows。

1. 默认无需配对码。仅在 Mac 开启验证时，勾选 **使用配对码** 并粘贴 Mac 配对码。
2. 填写 Mac 网桥 IPv4，或留空自动发现；端口须与 Mac 一致。
3. 选择目标 Windows 显示屏，默认使用 **自动**，也可选择 **自定义** 调整分辨率和码率；帧率由两端显示器参数协商。原始像素和文本剪贴板需两端均更新至 0.6.0；旧版能力查询会逐级回退。
4. 点击 **连接 Mac**。设置窗口显示连接 / 自动重试状态和错误原因；收到首帧后显示远程桌面。
5. 可用 **断开连接** 修改参数，或 **打开远程画面** 返回串流。关闭远程画面会断开并返回设置；关闭设置窗口退出应用。

默认根据 Windows 显示语言选择中文或英文，右上角可切换 **跟随系统 / 中文 / English**；也支持 `--lang auto|zh|en`。原生控件支持 Tab / Enter 和显示缩放，输入框和按钮随窗口宽度调整；窗口变高时扩展诊断区，窗口较小时可以滚动。键鼠仅在远程画面窗口处于前台、已连接并呈现首帧时转发；在连接表单里输入不会发给 Mac。

连接参数存储在当前用户的 `HKCU\Software\ThunderDisplay\Client`。配对码默认不保存，勾选 **在此电脑记住配对码（加密）** 后才使用当前用户 DPAPI 保存；取消勾选立即移除保存的配对码。记住配对码不会让双击启动自动连接。显式传入命令行 `--connect`、`--token` 或 `--token-file` 才会自动连接。

高级用法：省略 `--host` 则尝试本地广播发现。`--token-file pairing.token` 可以从本地文件读取配对码；文件内容仅放配对码，并自行限制文件访问权限。

本次也提供了 `dist/windows-x64/ThunderDisplayClient.exe` 交叉编译产物，可以连同 `licenses` 目录拷到 ROG 上先做联调。
如需在 Mac 上重建，使用官方 [LLVM MinGW](https://github.com/mstorsjo/llvm-mingw) macOS 工具包解压目录：

```bash
./scripts/build-windows-cross.sh /absolute/path/to/llvm-mingw
python3 scripts/package-windows.py
```

交叉编译不等于 Windows 实机测试。客户端只导入 Windows 系统 DLL，C++ 运行库已静态链接；Windows N 版本还需具备 Media Foundation 系统组件。

| 模式 | 分辨率 | 帧率 | 码率 |
| --- | --- | --- | --- |
| 自动 | 默认保留 Mac HiDPI 源像素，最大 4096×4096 | 检测两端显示参数后协商 | 推荐 160–1000 Mbps，也可独立自定义至 20000 Mbps |
| 自定义 | 用户指定偶数分辨率 | 同样按两端显示参数协商 | 用户指定 10–20000 Mbps |

Mac 的“桌面逻辑尺寸”对应缩放后的桌面布局，例如 2048×1280；2× HiDPI 渲染源则为 4096×2560。自动模式以渲染源为准保留文字细节，保留原始像素时最大 4096×4096；兼容模式按目标屏幕缩放并限制 4096×2304。不会为了“最大模式”修改系统分辨率 / 刷新率。最大模式来自系统列出的可用模式，不保证代表面板物理极限；色深为当前桌面 / 输出通道值，HDR 能力检测不代表已支持 HDR 串流。

参数按命令行顺序应用，覆盖参数放在 `--mode` 后。例如：

```powershell
.\ThunderDisplayClient.exe --host 192.168.77.1 --connect `
  --mode auto --bitrate 1000 --codec hevc
```

`--display-pixels` 关闭原始 HiDPI 像素；`--no-clipboard` 关闭文本剪贴板。
`--depth 0|8|10` 选择自动、8-bit SDR 或强制 10-bit SDR；默认自动。
`--codec auto` 为默认值，支持 `hevc` 和 `h264`。Windows 上 HEVC Media Foundation 解码器未安装 / 不可用时，自动模式改用 H.264。程序要求 GPU 解码输出，不静默采用 CPU 解码。
`--width` / `--height` 支持偶数尺寸，范围 320×240 至 4096×4096（超过 2304 行需两端 0.6.0）；`--bitrate` 单位 Mbps，范围 10–20000（超过 1000 需两端 0.7.0）。

- **Ctrl + Alt + Enter**：切换无边框全屏，可在设置中自定义；不使用 F11。
- **Ctrl + Shift + Esc**：全屏时仍可唤起 Windows 任务管理器。
- **Ctrl + Alt + Shift + Esc**：释放 / 恢复远程键鼠控制。
- 切换到其他 Windows 应用自动释放远程按键；关闭远程画面窗口断开连接并返回设置。
- 默认立即 `Present(0)`，支持时允许撕裂；加 `--vsync` 开启垂直同步。
- 键盘为 Windows VK → Mac ANSI 物理键位映射，字符受 Mac 输入法 / 键盘布局影响。第一版不做 IME 文本同步，触控板通过 Windows 鼠标 / 滚轮事件转发。

## 4. 自启动与无人值守

当前本地签名版本请求普通屏幕录制和键鼠控制权限。截图中的“远程桌面”持久捕获权限尚未接入；Apple 的 `com.apple.developer.persistent-content-capture` 是需要开发者申请、获批后通过授权签名配置启用的受限资格，不能通过增加一个设置按钮或自行签入此键获得。[Apple Persistent Content Capture](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.persistent-content-capture)。持久捕获资格与登录前图形会话组件是不同环节：0.8.0已实现后者的捕获、输入入口及交接，但仍需验证当前系统实际允许登录前捕获和控制，获批资格也不等于完成验收。

先把应用放到固定位置，完成权限授权，然后启用“登录时打开 ThunderDisplay”。0.7.4 使用 `SMAppService.mainApp` 注册主 App，系统“登录时打开”可见。首次打开新版时，自动迁移旧 ServiceManagement LaunchAgent 登录配置并取消旧注册。主 App 登录项不承诺崩溃后由 launchd 自动拉起。状态区分已注册、待系统批准和当前桌面主机是否运行，不以配置存在证明启动成功。

“随系统启动”包含两个组件：system daemon以nobody运行，提供发现与失败回退；全局LaunchAgent只在LoginWindow图形会话运行，验证有效帧和事件发送授权后接受连接。主机只绑定雷雳网桥并限制同子网；登录前不提供剪贴板或文件操作，登录后立即停止并由桌面主机接管。默认47990端口，安装时复制当前端口及可选配对配置（root专属600配置文件）。0.7.5 改用独立标识 `dev.thunderdisplay.boot.system`，由管理员安装到 `/Library/LaunchDaemons` 和 `/Library/PrivilegedHelperTools`。安装时持久禁用旧 `dev.thunderdisplay.boot` 注册并移除旧系统文件，应用包也不再携带旧 LaunchDaemon，避免重启时旧 ServiceManagement 注册抢占同名任务。不修改用户目录权限。启用时同时注册主 App 登录项。UI 核对新服务的实际运行状态及加载路径，旧配置显示迁移提示。0.8.0还安装 `/Library/LaunchAgents/dev.thunderdisplay.loginwindow.plist`（LimitLoadToSessionType=LoginWindow）及root所有、其他账户不可写的 `/Library/Application Support/ThunderDisplay/ThunderDisplayHost.app`。使用完整应用包保留签名身份与资源；不把用户可写的工作目录程序作为root执行目标。组件间仅共享root所有的只读状态心跳，无任意命令通道；12秒过期恢复发现回退。新增“更新开机组件与连接配置”用于更新程序或端口/配对策略。系统日志 `subsystem == "dev.thunderdisplay.host" AND category == "boot"` 记录进程启动及是否已登录，用于冷启动验收；未经过真实重启不能称为完成验收。

Windows 自动连接与断线恢复每轮最多10分钟、150次尝试，每次尝试起点至少间隔4秒；发现、连接、握手与等待均计入时间预算。UI 显示 `number/150`，时间或次数上限先到即停止，允许点击连接手动开始新一轮。实际显示新会话首帧后才确认画面已恢复；画面3秒无更新使其失效并清空，持续12秒无解码输出重建连接，保持控制心跳不再掩盖视频中断。静止桌面通过现有关键帧请求刷新。

旧版本 `scripts/install-agent.sh` 仅用于兼容，勿与新原生登录项同时启用。若曾用旧脚本安装，先以该脚本的 `--uninstall` 移除旧启动项，再在设置中启用新登录项。

0.7.4 修复睡眠/显示器变化关闭自动启动意图的问题。App 启动即自动运行主机；系统睡眠时清理旧捕获、输入与网络，唤醒后重新检测显示器和网桥并自动重建。网桥或捕获暂不可用时继续重试，只有用户主动停止服务才暂停。显示器短暂消失不丢弃首选显示器，恢复后重新选择。

“保持 Mac 可连接”默认开启，仅在此 App 中持有 PreventUserIdleSystemSleep 电源断言，不改系统设置；屏幕仍可正常熄灭。退出或主动停止主机时释放，用户可关闭开关。Thunderbolt 不支持 Wake-on-LAN，所以防止系统自动睡眠用于维持无人值守连接；手动让系统睡眠后仍需先在 Mac 端唤醒，再由 App 自动恢复主机。[Apple TN3205](https://developer.apple.com/documentation/technotes/tn3205-low-latency-communication-with-rdma-over-thunderbolt)

LoginWindow主机代码及安装流程已接入，真实未登录会话中的画面、输入、登录交接仍待双机验收；FileVault开机解锁不支持。真实硬件睡眠与锁屏捕获也仍需验收。睡眠/唤醒通知已用真实主机、编码与回环首帧验证，但不等于物理睡眠测试。开机后台进程存在或安装成功不代表可在登录前看到或控制桌面；Windows会区分组件未就绪、正在验证与捕获/输入不可用，保留系统错误。

## 验证

```bash
./scripts/test.sh
```

Swift 和 C++ 使用同一组固定字节向量检查协议兼容；测试涵盖 TCP 拆包 / 粘包、签名整数、长度校验、UDP 分片、乱序、重复、参考帧丢失、内存上限和序号回绕。`cmake` / `ctest` 也能在 Mac 上单独运行可移植 C++ 协议测试。

本机验证记录见 [docs/validation.md](docs/validation.md)。Windows 原生 GPU、真实雷雳传输、高刷吞吐和端到端延迟需要在 Mac + ROG 上验收，不能从编译和协议测试推断。

## 当前边界

- 暂不实现音频、文件剪贴板、文件拖放、虚拟屏幕、多显示器同时传输、P3 / HDR、4:4:4 或 AV1。现有显示器 HiDPI 捕获与双向文字与图片剪贴板已实现。
- 启用验证时，配对码存储在 `~/Library/Application Support/ThunderDisplay/pairing-code`，新文件权限为 0600。TCP / UDP 第一版没有加密，仅用于你控制的直连网络；不要映射到公网或用于不可信局域网。轮换配对码可停止 Host 后删除该文件再启动。
- 雷雳链路中断后 Windows 自动重连；Mac 网桥 IP 改变时在设置窗口停止服务、修改 / 重新检测地址再启动。无可用网桥时 Host 保持设置窗口打开。
- 屏幕录制、辅助功能权限必须由本机用户完成；配对验证为可选。远程注入不保证对系统安全输入界面有效。

协议和线程设计见 [docs/protocol.md](docs/protocol.md)。API 依据：[Apple 捕获帧间隔](https://developer.apple.com/documentation/screencapturekit/scstreamconfiguration/minimumframeinterval)、[Microsoft D3D11 解码](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation)、[Microsoft HEVC Annex B / NV12](https://learn.microsoft.com/en-us/windows/win32/medfound/h-265---hevc-video-decoder)、[Win32 DPI](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows)、[DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)。

Windows 等待首帧时，详细信息每秒显示有效视频包、完整帧、提交解码和解码输出计数，以及当前所处阶段。0.3.3 的 Windows 会从接收视频的 UDP socket 主动探测 Mac 视频端口；Mac 回复端口并在收到视频探测后重发关键帧。当前两端均更新至 0.4.0，可使用此探测与自动画质协商。首帧 12 秒无解码输出会报告具体阶段并重连；自动编码模式收到完整 HEVC 帧仍无解码输出时会改试 H.264。最小化远程画面窗口时提示恢复窗口，不因此更换编码。

0.8.0 登录前诊断：主 App 的 `--login-window-check` 只读检查当前运行上下文；实际登录前结果查看系统日志 `subsystem == "dev.thunderdisplay.host" AND category == "loginwindow"`。agent不记录输入、密码或图像。root身份不绕过TCC，不修改系统安全策略。
