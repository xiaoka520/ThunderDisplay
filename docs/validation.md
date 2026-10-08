# 验证记录

2026-10-03 至 2026-10-04，Asia/Shanghai。本记录区分构建 / 合成测试与真实两台机器验收。

## 已验证

| 项目 | 结果 | 验证范围 |
| --- | --- | --- |
| Mac Host 0.2.0 | Swift Release 构建通过 | 本机 Apple Swift 6.4，部署目标 macOS 13 |
| Mac .app | ad-hoc 签名校验通过 | `codesign --verify --deep --strict`；不含 Apple 分发签名 / 公证 |
| HEVC 硬件编码 | 通过 | 640×360、60 FPS 参数，3 个合成帧，IDR → P → 主动 IDR |
| H.264 硬件编码 | 通过 | 同上，要求硬件编码，输出 Annex B |
| 真实桌面捕获 → 编码 | 通过 | ScreenCaptureKit → 2560×1600 HEVC 首帧，关键帧 30410 bytes；只检查元数据，不保存 / 发送画面 |
| Swift 协议 | 6 个 XCTest 通过 | 鉴权、固定字节向量、TCP 各拆分点、非法长度、签名整数、视频尾片 |
| C++ 协议 | 全部通过 | 固定字节向量、分片重组、乱序、重复、丢帧恢复、内存上限、序号回绕 |
| C++ 内存检查 | 通过 | AddressSanitizer / UndefinedBehaviorSanitizer 下运行协议测试 |
| Windows 客户端 0.2.0 | x64 GUI 交叉编译和链接通过 | LLVM MinGW 20260908 / Clang 23.1.1，`-Wall -Wextra -Werror` |
| CMake Windows 构建 | 0.2.0 GUI / 资源交叉构建通过 | 主程序和 Windows 协议测试可执行文件均完成；没有在 Mac 执行 Windows 二进制 |
| Windows 导入依赖 | 已检查 | Windows 系统 DLL；C++ 运行库静态链接，无工具链运行时 DLL 依赖 |
| 构建 / 启动脚本 | shell 语法和 plist 校验通过 | 没有在本机安装 LaunchAgent 或改动网络配置 |

官方 LLVM MinGW macOS 包 SHA256：`d1dc5d1ecf3a3ced5ed5544c72f1acd0c8e84eb3024d520ecc6b143eec62a149`，下载后与官方 release metadata 核对。
交叉工具链仅位于忽略的 `build/toolchains/` 中；应用运行不需要该目录。

VideoToolbox 未接受 `MaxFrameDelayCount=1`（-12900），程序已记录并保留应用侧只允许一个在途编码帧的限制。RealTime、无 B 帧、目标码率 / FPS 和 GOP 属性均成功设置。

## 本次 UI 与权限流程改进（2026-10-04）

- Mac 实际启动打包应用的 `--preview-ui` 模式，通过原生窗口截图 / Accessibility tree 检查中文标题、两项权限行、网桥地址、复制配对码、启动 / 停止 / 重启按钮；最终 620×640 窗口文字和按钮完整可见。
- 在预览中编辑 IPv4 / 端口并点击“重新检查”，窗口保持打开，字段值保留。关闭窗口后进程仍保留；检查结束已退出预览。
- 预览模式只显示占位配对码，禁用权限申请、服务启动及重启。未重置、撤销或自动授予用户的 TCC 权限，因此 **没有声称实测了首次授权弹窗与授权后重启流程**。
- 最终正常启动正式 .app（非预览）：两项权限均显示待授权，申请与重启按钮可用、服务启动禁用；点击“重新检查”后窗口仍保持运行。没有申请或更改系统权限，正式设置窗口保留给用户操作。
- 权限实现检查：同一进程先请求辅助功能再请求屏幕录制；拒绝 / 缺权限不退出；定时与激活时检查；需要 OS 重启时可从窗口重启一次并保留编辑后的 IP / 端口。
- Windows 发布文件 PE Subsystem 已检查为 **Windows GUI**，包含 DPI / Common Controls v6 manifest 与 0.2.0 版本资源；双击无须必填命令行 token。
- Windows 原生表单支持语言切换、模式 / 参数、状态诊断、显式连接 / 断开。键盘钩子入口限定远程画面前台；串流输入还要求已连接并实际呈现首帧。
- 当前 Mac 环境不能运行 Windows EXE。Windows 控件布局、语言切换、DPAPI 记住配对码、Tab / Enter、DPI 多显示器与实际连接操作 **仅已交叉编译 / 代码检查，尚未原生运行验证**。
- C++ 协议测试与 6 个 Swift XCTest 在改动后再次通过；本次没有修改视频编解码协议。

## 0.3.0 配对、权限状态与自适应布局（2026-10-04）

- Mac / Windows 默认关闭配对验证；Mac 开启验证后，空码和错误码被拒绝，并明确提示 Windows 勾选填写。关闭验证时 Host 仅接收选定本机接口同子网 TCP 客户端。协议保留 48-byte Hello，用 32 零字节表示不提供配对码。
- Mac 不将界面偏好当作授权状态：当前进程检查、同 bundle 新进程只读复查、手动实际 ScreenCaptureKit 检查区分当前可用、已授权需重启、当前未生效和检查失败。授权发生变化时清除旧检查结果，避免过去一次失败覆盖新的权限。
- 11 个 Swift XCTest（7 协议 + 4 权限状态）通过；C++ 协议与独立布局测试通过。布局测试覆盖逻辑宽度 628 / 686 / 1200 / 1800，高度 350 / 802 / 1600，检查输入框随宽度扩展、各列无重叠、按钮边界、诊断区和滚动内容高度。
- Windows 0.3.0 在 `-Wall -Wextra -Werror` 下完成 x64 GUI 交叉编译。重新打包校验 GUI PE 子系统、当前中文版本标题、表单标签、ZIP 和独立 EXE 字节 / SHA256 一致。实际 ROG 窗口缩放与 DPI 切换尚未运行验证。
- Mac 0.3.0 Release 构建与严格签名校验通过。真实正常启动窗口显示默认关闭配对码、当前应用绝对路径及权限状态；点击“检查实际权限”后屏幕权限变为“检查失败”，显示 ScreenCaptureKit 返回的 TCC 拒绝原因，窗口保持打开且服务不启动。截图检查文字、按钮完整可见。没有授予或重置系统权限，也没有在缺授权时伪造可用状态。
- 本机没有有效代码签名证书。默认 ad-hoc 构建的签名身份随可执行文件变化，旧副本的系统开关不代表新副本获准访问；UI 给出定位当前副本与重新添加的指引。构建脚本支持 `THUNDERDISPLAY_SIGN_IDENTITY` 使用已有持续签名身份，并用临时可执行文件 + rename 替换，避免原地覆盖正在运行的二进制。

## Mac 0.3.1 权限防闪烁与拖拽助手（2026-10-04）

- 修复后台每五秒复查时的可见 busy 状态和错误清空：内部 probing 与手动 checking 分开，定时复查保留上次提示，不使授权按钮周期性禁用。设置窗口只在完整展示状态变化时刷新，文档尺寸未变化时不重新设置滚动内容尺寸。
- 申请权限 / 打开权限设置会显示浮动 NSPanel；也可单独打开助手。助手显示当前 Bundle 的图标及完整路径，拖拽源使用 NSURL 的文件 URL pasteboard writer 与 copy 操作。提供屏幕录制、辅助功能设置、实际检查和统一重启按钮。切到别的应用后不隐藏助手。
- Mac 0.3.1 Release 构建通过；11 个 Swift XCTest、C++ 协议与布局测试通过。
- 实际启动正式应用，打开助手，通过原生 AX 和截图检查图标、路径、两项设置入口、权限状态、检查和重启按钮，文字完整可见。辅助功能入口打开了当前系统的“设备控制和数据访问”页面。
- 跨多个后台轮询周期的界面读取保留原提示，未出现周期性“检查中”和提示清空；期间屏幕权限状态变化后，两窗口更新为“可用”。没有由代理操作系统授权开关。
- 尝试在助手自身范围内拖拽并取消，自动化工具返回窗口状态变化 / windowNotFoundAtPosition，不能据此确认运行时文件投放成功。因此实际拖入系统权限列表并授权仍由用户完成；未宣称已代用户完成此步骤。
- 更新 Mac 0.3.1 ZIP 并校验版本、归档完整性和内含可执行文件与当前 .app 一致。Windows 未改动，最新版本仍为 0.3.0；dist 仅保留各平台最新构建。

## Mac 0.3.2 助手生命周期与辅助功能检测（2026-10-04）

- 删除主窗口独立“打开拖拽授权助手”按钮；仅申请权限或进入尚未完成的权限设置时自动显示助手。两项状态均为可用 / 已授权需重启时关闭助手；重启操作也关闭助手。未完成或检查失败不会提前关闭，主窗口继续提示需要重启。
- 原先键鼠代码用 CGEvent，但申请 / 检查 / 转发门槛全部依赖 AXIsProcessTrusted。现在统一使用 CGRequestPostEventAccess / CGPreflightPostEventAccess；AX 值保留为只读诊断，未用“任一值为真”假定实际输入可用。
- 修复子进程复查失败时清空上次成功结果的问题，超时保留既有状态；不会把失败复查解释为新授权。
- 12 个 Swift XCTest（7 协议 + 5 状态）通过，新增助手关闭策略覆盖两项完成、等待重启、缺少一项及检查失败；C++ 协议与布局测试通过。Mac Release 构建通过。
- 默认构建生成并复用项目 `.local-signing/` 内的私有钥匙串 / 自签名开发证书，不导入用户登录或系统钥匙串、不添加系统信任。配置与钥匙串权限仅当前用户可读；签名后锁定钥匙串，私钥不可导出，源码忽略该目录。已有签名身份仍可通过 THUNDERDISPLAY_SIGN_IDENTITY 使用。
- 更改源码重新构建后，DR 保持同一 bundle identifier + 证书 leaf fingerprint，已无旧 ad-hoc cdhash 约束。完整系统上下文下严格验签和当前证书匹配通过，错误证书约束被拒绝。沙盒内自签名链验证报 CSSMERR_TP_NOT_TRUSTED，沙盒外只读复验通过，未为此添加系统信任。
- 实际正常启动 0.3.2，主窗口没有独立助手按钮。点击“申请所需权限”后助手自动出现，截图 / AX 核对当前应用、移除旧授权再添加的指引、两项权限按钮及重启按钮完整显示。
- 本次没有由代理删除系统权限条目、授予权限或注入键鼠。切换到固定身份后当前应用仍待重新授权，故不宣称已实测新的完整授权及助手自动关闭流程；关闭策略通过状态测试及实现检查。真正键鼠转发仍需 Mac + Windows 联调。
- 当前 Mac ZIP 0.3.2；Windows 最新仍为 0.3.0。只打包 .app，签名私钥、钥匙串和配置不进入发布包。

## 0.3.3 首帧与 UDP 通道修复（2026-10-04）

- 原协议只通过 TCP 声明 UDP 接收端口；客户端未从该 socket 发出任何 UDP，连接成功不能证明视频返回路径可用。增加可选 session 探测：先向发现端口查询实际视频端口，再从同一视频 socket 向该端口发包；Host 校验当前 ready 会话、对端 IP 与 Hello 声明端口，随后重发 IDR。v1 控制 / 视频包格式不变；两端均更新才能使用探测，显式 UDP 阻断仍会报告零视频包。
- 新建解码器收到初始 IDR 不再 flush；异步队列先处理 NeedInput / HaveOutput 事件再判满；输入首次与 flush 后标记 Discontinuity。候选解码器包含输出类型和启动阶段的失败会继续尝试下一候选。自动 HEVC 模式的运行时解码错误或首帧无输出会改试 H.264。
- 每秒显示视频包 / 完整帧 / 提交解码 / 解码输出计数和中文或英文阶段。首帧 12 秒无输出报告具体阶段并重连；已解码但最小化 / 遮挡未呈现时提示恢复窗口。首帧超时提示至少保留 3 秒，避免立即被重连状态覆盖。
- C++ 协议新增端口 / session / 非法探测响应和各首帧阶段测试，通过 ASan / UBSan；布局测试和 12 个 Swift XCTest 通过。Windows x64 GUI 0.3.3 在 -Wall -Wextra -Werror 下交叉编译通过。ZIP / 独立 EXE / 原始 EXE 字节与 SHA256 相同；新发布包验证后删除旧版本。
- Mac 0.3.3 Release 构建、固定开发身份签名校验通过。HEVC 和 H.264 各 3 个合成帧通过（首 IDR、P 帧、主动 IDR）。已有屏幕权限下真实 ScreenCaptureKit → 2560×1600 HEVC 首帧通过，102061 bytes，未保存或发送桌面画面。
- `python3 tests/host_udp_probe_check.py` 编译真实 HostServer 与只提供合成帧的捕获替身，通过回环 socket 校验 Ready 前拒绝、错误 session / 额外字段 / 非声明源端口拒绝、发现 socket 同端口回复、视频 socket 收到探测后重发 IDR，以及 180000 bytes / 156 个分片完整一致。测试不启动 ScreenCaptureKit、不注入输入；沙盒阻止回环 bind 时需要在正常本地环境运行。
- 仍未在 ROG 执行 Windows MFT / D3D11 或复现用户的实际“等待首帧”。这些修复处理已发现的通道与初始化缺口，并提供剩余故障所处阶段；不能把本地编码 / 回环测试视为两机端到端通过。

## 0.4.0 两端 UI、显示器检测与自动画质（2026-10-04）

- Mac 使用浅色背景、圆角卡片、SF Symbols 和状态标签；设置窗口可缩放 / 滚动。Windows 使用双缓冲卡片绘制、圆角按钮 / 输入框和 Win11 窗口圆角；按 DPI 与宽高重新布局，自动 / 手动、配对区域按需展开，中文 / 英文可切换。
- Mac 检测选中屏幕的逻辑尺寸、当前渲染像素、刷新率、系统最大可用模式、桌面通道色深及 HDR/EDR / P3 能力；Windows 检测当前 / 首选尺寸、原生尺寸下最高可用刷新率、最大面积模式、输出通道色深和明确 HDR 能力。未能读取明确 HDR 状态时显示未知，不将高级颜色能力直接当作 HDR。
- 使用当前真实 Mac 显示器只读核对：Display 13 逻辑桌面 2048×1280，HiDPI 渲染源 4096×2560，当前 60 Hz；可用最大模式 7680×4800 / 240 Hz 来自虚拟显示器模式列表，不代表当前桌面或真实面板极限。正常窗口截图 / AX 已核对两种尺寸分别显示。窗口启动时发现的约束初始化顺序错误已修复，重启后无该异常。
- 自动模式增加能力查询，再以源渲染像素和选中的 Windows 原生模式协商比例、FPS 与高码率；不放大源图像，编码尺寸上限 4096×2304，FPS 上限 240，码率 80–300 Mbps。显示器变化更新下一次协商快照；源显示器断开则停止服务。当前串流仍为 SDR 8-bit 4:2:0，不宣称 HDR / 10-bit 已实现。
- C++ 协议、自动画质 / 能力边界和响应式布局测试通过；布局测试覆盖窄 / 宽窗口、可选配对及手动画质卡片。13 个 Swift XCTest（8 协议、5 权限状态）通过，包含 144 Hz Hello、能力消息黄金字节及中文名称截断保持 UTF-8 完整性。
- 真实 HostServer + 合成捕获替身的回环测试通过能力查询 / 144 Hz Hello、session / UDP 端口验证、IDR 重发和 156 个分片 / 180000 bytes 一致性。不保存桌面画面、不注入输入。
- 正常 Mac Host 监听 10.0.0.2:47990 时，仅发送能力查询，实际返回虚拟 16:10 / 4096×2560 / 60 Hz / SDR 8-bit 元数据；未发送 Hello / Ready，不启动桌面捕获。最终应用已重新启动并显示服务就绪。
- Windows x64 GUI 0.4.0 以 -Wall -Wextra -Werror 交叉编译通过；Mac Release 构建与持续项目签名校验通过。Mac ZIP 完整性、版本、可执行文件字节一致且不含签名钥匙串；Windows ZIP / 独立 EXE / 原始 EXE 均一致且 GUI PE 子系统正确。校验后删除 dist 的所有旧版本构建。
- Mac 正常运行界面已实际检查。Windows 仍未在 ROG 原生运行，无法把交叉编译 / 布局测试视为原生控件、DPI 或 MFT / GPU 实测通过；自动协商的目标值也不代表已达到的实际帧率。两机联调与高帧率性能待以下实机验收。

## 0.4.1 Windows 滚动重绘与 SDR 色彩标记（2026-10-04）

- 用户确认撕裂发生在 Windows 客户端设置窗口。原实现每次滚动逐个 MoveWindow(TRUE)、WM_SETFONT(TRUE) 和 SetWindowRgn(TRUE)，导致分阶段绘制。现在使用 DeferWindowPos / SWP_NOREDRAW 批量定位（失败时完整定位回退）、WS_EX_COMPOSITED 合成子控件，最后统一重绘。字体仅在需要变化时应用，圆角仅在实际尺寸 / DPI 改变时更新；避免布局重入。远程画面 VSync / Present 策略未变。
- 原 Windows 代码已将 YUV 输入设为 BT.709 / 16–235，并将 RGB_Range 设为全范围，不能据此声称已发现电平写反。0.4.1 补齐旧 API RGB 输出 Nominal_Range，支持新 API 时明确使用 YCBCR_STUDIO_G22_LEFT_P709 → RGB_FULL_G22_NONE_P709，并先查询视频处理器转换支持；不支持时保留兼容路径。Swap chain 明确标记 SDR 全范围 RGB；诊断显示当前转换路径。
- Mac VideoToolbox 显式设置 BT.709 色彩基准、传递函数及 YCbCr 矩阵，Windows Media Foundation 输入补齐 BT.709 传递函数。没有将 P3 像素仅改标签来假称宽色域，也没有实现 P3 / HDR / 10-bit 串流。
- Windows x64 GUI 0.4.1 在 -Wall -Wextra -Werror 下交叉编译通过；Mac Release 和持续签名校验通过。C++ 协议、自动画质、响应式布局以及 13 个 Swift XCTest 均通过。
- Mac 合成 HEVC / H.264 各 3 帧验证初始 IDR、P 帧、请求 IDR和输出格式 BT.709 色彩标记通过；有限范围输出没有被标记为 FullRangeVideo。真实 ScreenCaptureKit → 2560×1600 HEVC 初始关键帧 133218 bytes，捕获与 BT.709 输出标记检查通过。仅验证元数据，不保存或发送桌面画面，不注入输入。这不等于验证了 ROG GPU 的 RGB 数值或最终观感。
- Mac 已正常重新启动到 0.4.1，窗口显示服务就绪。Windows 设置窗口滚动和实际褪色原因仍需 ROG 验收：黑白 / 灰阶也异常时检查范围与 HDR / 色彩管理；仅宽色域饱和色变淡时检查 P3 → BT.709 的色域损失。


## 0.5.0 滚动文档、Main10、全屏快捷键与像素显示（2026-10-04）

- 用户在 0.4.1 的 ROG 设置窗口仍看到滚动残影。0.5.0 将原先随滚动移动的几十个子控件改为固定在独立文档子窗口内，只改变整页位置；父窗口只绘背景，文档在本地坐标绘卡片，重绘所有子控件，不拷贝旧滚动像素。未取得 Windows 原生环境，不能宣称已实机复现并确认消失。
- 全屏默认 Ctrl+Alt+Enter，可直接在录入框按组合键并立即存储到当前用户偏好。禁止 F11 / 系统安全 / 释放输入 / 任务管理器组合，支持扩展键。热键匹配排除额外 Win / Shift 修饰键，重复按下只切换一次。普通 Hotkey control 不接受 Enter 等特殊按键，录入子类显式处理，Tab / Shift+Tab 保留焦点导航。
- 全屏远程画面有焦点且捕获输入时拦截键盘。Ctrl/Shift 也拦截，避免 Ctrl+Shift 输入语言切换和粘滞键泄漏；Ctrl+Shift+Esc 在 UI 线程调用系统路径 Taskmgr.exe，不在低级钩子里执行耗时操作。Ctrl+Alt+Del 保留系统安全处理。窗口模式允许本机 Win / Alt+Tab / Alt+F4；失焦释放远程按键，已拦截按下对应的释放继续吞掉，防止开始菜单意外弹出。ROG 全屏 / 重复按键 / 任务管理器 / 失焦回归仍待实测。
- Main10 能力通过实际要求硬件的 VideoToolbox 会话准备探测。旧 `[9]` 查询保持 mask=3 / bits=8；新 `[9,2]` 可返回 7 / 10。Hello / Welcome / UDP 使用 codec=4 表示 Main10，保持消息长度。自动回退按 Main10 → 8-bit HEVC → H.264；强制 10-bit 不静默降级。
- 本机硬件诊断通过 HEVC / H.264 / Main10 各 3 帧，包含 RGB10 → P010 转换。真实 ScreenCaptureKit 2560×1600 桌面捕获通过 RGB10 输入、P010 转换、HEVC Main10 编码、BT.709 有限范围标记，并验证 hvcC profile_idc=2 与亮度 / 色度各 10-bit；首帧 145608 bytes。只记录元数据，不保存 / 发送真实屏幕。未验证 ROG 的 MF P010 解码和 RGB10 呈现。
- Windows 10-bit 路径要求 MF Main10 profile / P010 输出、D3D11 P010 输入、R10G10B10A2 swap chain 与显式 BT.709 SDR 转换支持；硬件不支持时自动模式重连降级。仍不支持 HDR / P3 / 4:4:4。
- HiDPI 渲染源保持检测为 4096×2560、逻辑桌面 2048×1280。自动适配目标 2560×1600 会降采样；新增 1:1 串流像素显示，较小窗口居中裁剪并按裁剪偏移映射鼠标。诊断显示串流 / 显示像素和缩放比例；不会误称取消源端降采样。
- C++ 协议、能力与回退、响应式布局、快捷键规则、1:1 / 适配 / 黑边 / 裁剪鼠标映射通过；Swift 9 个 Wire + 5 个权限 XCTest 通过；真实 HostServer 测试通过旧 / 新能力查询和 Main10 codec=4 UDP 探测、156 分片 / 180000 合成字节恢复。
- Mac 0.5.0 Release 持续签名通过。原生 UI 观察确认两项权限保持已授权、徽章为 148×34 圆角，重启时清理了重复启动的实例，保留的单实例已与旧 Windows 客户端自动恢复 HEVC / 8-bit 2560×1600 @120 请求连接。Windows x64 GUI 在 -Wall -Wextra -Werror 下交叉编译通过；ROG 原生 UI / 按键 / GPU 验收待完成。

- 最终 dist 只保留 0.5.0 两端压缩包、同版本独立 Windows GUI exe 和两端当前构建目录。包完整性 / exe 一致性 / GUI 标签与版本检查通过；Mac 最终签名在可读取钥匙串信任的上下文中校验通过。Windows exe SHA256: `6ef93035525080431295fff1ecbab7cd2a7b8a6fc22b65e9535d304d5d930f28`。

## 0.6.0 原始 HiDPI、GPU 缩放与文本剪贴板（2026-10-04）

- 自动模式默认保留 Mac 当前渲染源像素，最高 4096×4096；当前 2048×1280 逻辑桌面 / 4096×2560 HiDPI 源对 ROG 2560×1600 @240 参数计算为 4096×2560 / 60 Hz / 290 Mbps。不会修改系统显示模式。兼容模式仍按面板适配 / 4096×2304；新协议能力查询 3→2→1 向旧 Host 回退。
- 捕获测试发现低延迟率控扩展能建立 4096×2560 会话，但每帧回调均为 frameDropped。超过 2304 行改用正常 VideoToolbox 硬件路径，维持实时、无 B 帧、单帧准入；连续编码丢帧 / 回调错误会明确失败并使客户端回退，不再静默无限等待首帧。
- 原始像素真实 Main10 首帧 248849 bytes：RGB10 → P010 → HEVC Main10，验证实际 4096×2560、hvcC Main10 / 亮度与色度 10-bit，以及 BT.709 有限范围元数据。最终构建 8-bit 原始像素首帧 267935 bytes，实际尺寸与 BT.709 元数据通过；仅打印诊断，没有保存或传输屏幕。合成 HEVC / H.264 / Main10 各 3 帧全部通过。这不是持续高帧率 / GPU / 端到端画质测试。
- Windows 先将 NV12 / P010 按源尺寸转换为 RGB8 / RGB10，再做水平 / 垂直 Lanczos-2 缩放，支持兼容 / 高质量 / 高质量加轻微锐化。1:1 模式跳过缩放与锐化；超过 4:1 缩小或着色器不可用时兼容呈现。高分辨率失败时自动模式按面板尺寸重连，诊断保留本次回退原因；强制 10-bit 仍不偷偷降级。高质量路径在本机只做 HLSL / SPIR-V 编译验证，未做 Windows D3DCompile / GPU 呈现验收。
- 双向文本剪贴板最多 64 KiB UTF-8，每片 3072 bytes；支持中文、Emoji、换行与空文本，拒绝越界、乱序、非法 UTF-8 / NUL。两端开关，Mac 可连接中更改；双方确认实际允许后才同步。只发送连接后新复制文本，不导出已有内容；按会话隔离，防止重复回传。剪贴板队列与键鼠 / 心跳分开，传片有界；Windows 剪贴板忙时短暂重试。
- 6 个 C++ 测试程序 / 17 个 Swift XCTest 通过。真实 HostServer + 合成捕获的回环检查通过三档能力查询、原始像素 Hello / Welcome、剪贴板偏好拒绝和动态允许确认、Unicode 分块双向一致性、启停与交错 Ping，以及 UDP session / 端口限制与 156 分片 / 180000 bytes 恢复。原生 AppKit 以独立命名测试粘贴板验证 Unicode、连接不发送旧内容、抑制回传、禁用、旧会话和停止；未读取或修改用户系统剪贴板。
- GUI 验证发现旧重启方式 / 同时观察可能启动两个实例，造成端口冲突。新增单实例文件锁、等待旧进程退出的重启助手，并提供标准退出 / 编辑菜单。正常重启后实测只有一个主机进程监听 10.0.0.2:47990。0.6.0 原生界面两项权限保持已授权，原始像素和剪贴板卡片显示无重叠；持续签名校验通过。
- Windows x64 GUI 0.6.0 在 -Wall -Wextra -Werror 下交叉编译通过；Mac Release 构建 / 签名通过。ZIP 完整性、版本、包内 / 独立 / 原始可执行文件一致性通过，包中没有本地签名私钥；dist 仅剩最新 0.6.0 构建。
- Windows EXE SHA256: `636924e1a0e50e6717973a799b4077fc69a67df2cd4091181e0dfbc848f7742b`。Mac EXE SHA256: `5cd59c08e5b961fbb712599dbcf81ccf65ec54338fea85891dabced689a265f5`。
- 待 ROG 原生验证：新控件 / 滚动、MF 高分辨率 Main10、D3DCompile 与 Lanczos 图像 / GPU 负载、两机剪贴板、实际文字边缘 / 灰阶 / 彩色细字、吞吐与延迟。串流仍为 BT.709 SDR 4:2:0；音频、P3 / HDR、4:4:4、文件 / 图片剪贴板和虚拟屏幕未实现。

## Windows 0.6.1 向上滚动与移除锐化（2026-10-04）

- 原消息循环在 `IsDialogMessageW` 处理任何消息之后都定位当前焦点。输入框仍保持焦点时，滚轮 / 滚动条移动后会被拉回字段所在位置。现在仅在实际焦点切换时定位，保留 Tab / Shift+Tab 的可见性；滚动允许焦点字段离开视口。
- 在对话框分发之前处理设置窗口滚轮，关闭的下拉框不吞掉滚轮或因滚动改变值。展开的下拉框保留原生滚动；诊断文本仅在指针位于文本框、且当前方向还有内容时独立滚动，到边缘后滚动整页。补齐滚动条到顶部 / 底部和最终拖动位置，保留高精度滚轮的分数像素，缩放 / 窗口尺寸变化后按文档边界限制位置。
- 实际接入 UI 的便携滚动状态回归测试通过：下方输入框持续有焦点时 25 次向上滚轮到顶而不反弹；Tab / Shift+Tab 定位、新窗口边界、高精度正反滚轮在 100 / 125 / 150 / 200% 步长下累积、方向反转、下拉框和嵌套诊断滚动边界。6 组 C++ 测试和 17 个 Swift XCTest 全部通过。
- 用户补充自动画质下已经清晰。代码确认保留 Mac HiDPI 源像素只作用于自动模式，手动预设使用指定分辨率；自动模式还调整码率。这个对比支持源分辨率 / 缩放是主要排查方向，不能据此单独归因于 4:2:0 或 HiDPI，也未取得用户实时连接诊断参数。此次不增加先前考虑的 RGB 无损补帧。
- 移除锐化算法和选项，默认使用不锐化的 Lanczos；原来的锐化偏好迁移到此模式，保留兼容缩放。HLSL 顶点 / 像素着色器通过 glslang 语法和 SPIR-V 编译；Windows D3DCompile 与 GPU 显示未实机验证。
- Mac 仍为最新 0.6.0，与 Windows 0.6.1 的协议兼容；此修复没有重建或重启正在运行的 Mac Host。Windows 在 `-Wall -Wextra -Werror` 下交叉编译，随后校验 GUI PE 子系统、版本控件及发布包字节一致后清理旧 Windows 发布文件。
- Windows 0.6.1 EXE SHA256: `bb5935ff3a2149cdde0cf4e0952e003ca8577fecd3314dfd4f4ff8cd7610e37c`。dist 保留 Windows 0.6.1 EXE / ZIP / 运行目录以及 Mac 0.6.0 .app / ZIP。
- Windows 原生控件滚动、DPI、实际 HiDPI / 缩放比例和最终清晰度仍需 ROG 验收，交叉编译和便携测试不能代替原生运行。

## 0.6.2 两档模式、1 Gbps 与本地指针（2026-10-04）

- GUI 显示模式只保留“自动”和“自定义”，删除固定 FPS 档位与 FPS 输入框。自动 / 自定义都按 Mac 当前刷新率与选定 Windows 显示器检测参数取较小值，程序最高 240；覆盖非预设 75 / 90 Hz 的协商测试通过。CLI 显式 --fps / 历史模式别名只为兼容保留，GUI 不展示它们。
- 码率输入 / 偏好 / CLI / Hello / Welcome 校验支持 10–1000 Mbps。自动模式新增独立自定义码率开关，1 Gbps 覆盖值不会覆盖 HiDPI 源像素或 FPS 协商。未覆盖时仍推荐 80–300 Mbps，保留现有默认策略。1000 Mbps 是编码器平均目标值，不是恒定输出或实测链路吞吐。
- query4 新增高码率 / 16 MiB 帧和本地指针能力；旧查询屏蔽新位且拒绝超过 300 Mbps / 4 MiB。新端对旧 Host 不静默降低指定码率。重组器在分配前检查协商大小，最多三帧；6 MiB IDR 跨过旧 25 ms 期限完整重组测试通过，超时 / 恢复保持有界，P 帧仍按 25 ms 失效。
- Windows “本地指针 · Mac 风格”默认关闭，连接前配置。开启后在查询中传给 Mac，真实 CaptureEngine 创建 SCStream 时即设置 showsCursor=false；Windows 只在能力位确认后显示按 DPI 生成的黑色箭头 / 白色描边，恢复 / 失焦 / 断线沿用既有本机输入释放规则。图像源栅格预览已查看，100 / 125 / 150 / 200 / 300% DPI 的热点、透明度、预乘像素和黑白边界测试通过。当前只显示本地箭头，不同步远端文本 / resize 等动态形状；更改开关需重新连接。
- 本地 HostServer + 合成捕获替身验证 query1 / 2 / 3 的原形状、query4 新能力、旧 query3 拒绝 1 Gbps、新查询保留 4096×2560 / 1000 Mbps，并核对 CaptureEngine 接收到 cursorVisible=false；继续覆盖剪贴板开关 / Unicode 回传 / 心跳、UDP session / 端口验证与 156 个分片的 IDR 重发。测试不注入输入、不捕获桌面。
- 6 组 C++ 测试和 18 个 Swift XCTest 通过。Windows x64 GUI 在 -Wall -Wextra -Werror 下交叉编译通过；发布校验拒绝旧 FPS GUI 文案，要求自定义码率 / 本地指针控件，ZIP / 独立 EXE / 原始 EXE 与版本指令一致。
- Mac 硬件合成编码的 1000 Mbps 检查最初发现 H.264 低延迟率控连续丢帧；高码率切换普通硬件率控后，HEVC / H.264 / Main10 各三帧（IDR / P / 请求 IDR）均通过，保持硬件、无 B 帧和单帧准入。这只证明接受配置与产出合成帧，不是 1 Gbps 吞吐 / 端到端性能验收。
- 真实 4096×2560 捕获检查在当前环境受阻：开 / 关视频指针均返回 Selected display is unavailable，CUA 确认 Mac 已锁定。已请求用户手动解锁；在解锁前不把真实桌面捕获 / 指针隐藏记为通过。改善 HostError 的 localizedDescription，使异步捕获错误保留具体原因。
- Mac 0.6.2 Release 构建与固定项目签名严格校验通过；Mac ZIP 的 Info.plist / 可执行文件和原 .app 一致且无签名私钥。验证后 dist 仅保留两端 0.6.2 最新构建。Windows EXE SHA256: `db6ca4513dda1a67152a65941db1261ce7e9c58082b786575c8cf688dbf9f4f1`；Mac binary SHA256: `5bcda306c18920a742740604c897135b464ba0c775c00755eb21bfb2586a6f02`。
- 待 Windows / ROG 实机验收：新两档 UI / 开关 / 原生 HCURSOR、DPI / HDR 桌面下指针观感、MF / GPU 高码率解码、大帧 UDP 丢包与持续带宽、解锁后的真实捕获与双机效果。没有测试或宣称音频 / P3 / HDR / 4:4:4 已支持。

## 0.6.3 Windows 本地箭头尺寸（2026-10-04）

- 将本地 Mac 风格箭头的几何尺寸缩小 40%，约 18 逻辑像素高，热点同步缩小，仍只按当前窗口所在显示器 DPI 生成一次栅格。200% 缩放的可见区域为 24×36 物理像素，透明画布为 64×64；检查的是可见箭头而非透明画布大小。栅格预览已检查。
- 在 100 / 125 / 150 / 200 / 300 / 400 / 800% DPI 下验证可见边界、箭头尺寸上限、热点对齐、透明边缘、黑白描边与预乘 BGRA；同时运行既有布局和滚动回归测试，全部通过。Windows 在 -Wall -Wextra -Werror 下交叉编译成功。
- GUI PE 子系统、版本文本、ZIP 完整性、独立 / 原始 / ZIP 内 EXE 字节一致均通过。Windows EXE SHA256: `3bbe60b6ea6b6398a72da2b03568bbfd4d2e40ba223185bf4e9307337284f8b0`。通过后删除旧 Windows 0.6.2 EXE / ZIP，dist 仅保留 Windows 0.6.3 EXE / ZIP / 运行目录与兼容的 Mac 0.6.2 .app / ZIP。本次没有重建或重启 Mac。
- 无 Windows 原生运行环境，ROG 上的系统指针呈现及最终视觉大小仍由实机验收；便携栅格测试与交叉编译不代表该项已通过。

## 0.6.4 Windows 使用 macOS 系统原版箭头（2026-10-04）

- 用户指出先前手画的箭头轮廓像 Windows 黑色指针。只读查找本机系统资源：HIServices Resources/cursors 包含多个 PDF 指针，SkyLight Resources 包含 CursorAsset；默认箭头通过公开 AppKit `NSCursor.arrow.image` / `hotSpot` 读取，初始化 NSApplication 后成功获得系统图像，未更改系统指针或设置。
- 来源为 macOS 27.0.1（26A434），系统图像逻辑画布 28×40、热点 5×5，保留原版轮廓、圆角白边、黑色填充和阴影。以 4× 栅格导出 112×160；PNG SHA256: `439d03923c56214bfb662007a5e8d0f4cbadec2d7590cd66f2758f70802296a5`。只裁透明边距，得到 61×88 BGRA / 热点 14×12，通过 RLE 无损嵌入 Windows；删除原手画多边形算法。来源图像、元数据与可重复导出 / 嵌入脚本保存在项目中。
- Windows 主体尺寸在 100% 下为 11×18、200% 下为 21×35 物理像素；阴影透明区域较大。100 / 125 / 150 / 200 / 300 / 400 / 800% DPI 的尺寸、热点、黑白边缘、原版阴影与预乘 BGRA 检查通过；4× 下与导出原像素 / 热点逐项一致，既有布局 / 滚动回归也通过。实际 Windows 栅格预览已查看。
- Windows x64 GUI -Wall -Wextra -Werror 交叉编译通过；发布校验新增系统指针诊断标记，GUI 版本、ZIP 完整性和三个 EXE 字节一致。EXE SHA256: `4ff360376e4b8733af862e3381226c8f4a848a5fbba314622228e0919d275f0d`。验证后清除旧 Windows EXE / ZIP；dist 仅留 Windows 0.6.4 EXE / ZIP / 运行目录与 Mac 0.6.2 .app / ZIP。Mac 无需重建或重启。
- 本次图像在构建时导出，不同步远端动态形状、指针主题或自定义颜色。Windows 原生 HCURSOR 和最终 ROG 观感仍需实机验收，便携测试 / 构建不等于原生运行。

## 尚需实机验收

2026-10-04 Windows GUI 0.2.1 交付核对：用户截图显示旧版本的完整必填配对码报错和控制台。检查此前 0.2.0 发布 ZIP，其 EXE 与本地 GUI EXE 字节一致，PE 子系统为 GUI，且没有截图中的旧报错字符串。因此截图对应的程序不是该 GUI 构建；没有直接访问用户 Windows 下载目录来核对文件哈希。本次增加窗口 / PE 资源版本号、独立 GUI ZIP / EXE 文件名与打包校验，拒绝控制台 EXE、旧入口字符串、缺少当前 GUI 标题或表单的文件。0.2.1 完成重新编译，打包后再次检查字节与哈希一致；Windows 原生运行验证仍待实机完成。

此前编码验证时，沙盒外只读诊断显示屏幕录制可用、辅助功能未授权，且当时未检测到网桥 IPv4。真实捕获检查在既有屏幕授权下完成，没有输入注入。本次 UI 预览读到网桥地址，但这不代表已完成 ROG 直连验收；以设置窗口显示的当前权限 / 网络状态为准。

- Mac 完整退出旧版本后打开新 .app：两项权限未授权时设置窗口仍保持打开；一次申请两项权限，返回后自动检查，必要时只在两项完成后重启一次。确认拒绝授权 / 无网桥不退出。
- Windows 双击 EXE 查看连接窗口，分别测试中文 / 英文 / 跟随系统、默认自动协商、各模式切换、自定义参数、无效 IP / 配对码提示、Tab / Enter、125% / 150% / 200% DPI 和滚动。
- 检查关闭远程画面返回设置、设置窗口文字输入不发送到 Mac，默认关闭配对码时直接连接；Mac 开启后 Windows 必须勾选并正确填写。勾选记住后下次预填但不自动连接，取消记住后删除已存配对码。
- 两端配置独立子网、确认 ping 和防火墙放行；分别测试手动 IP 与广播发现。
- 在 ROG 运行客户端，确认实际 HEVC / H.264 解码器返回 D3D11 NV12 纹理，图像和色彩正确。
- 先以 60 FPS 连接，检查左右上下角点击、不同宽高比黑边、拖动、双击、滚轮、左右修饰键、Command 快捷键和小键盘 Enter。
- 确认默认 Ctrl+Alt+Enter / 自定义全屏组合键、Ctrl+Shift+Esc 任务管理器、释放输入快捷键、Alt 切换应用后释放、拔线后的重连和无卡键。
- 测量 120 / 165 / 240 FPS 档位的实际捕获 / 解码 / 呈现吞吐，分别检查滚网页、拖窗口和文字。
- 用高速相机或时钟同步后的测量程序测试端到端延迟；Host 与 Client 的未同步 PTS 不能直接相减。
- 正常连接验证后，再安装 LaunchAgent 检查登录后启动和崩溃恢复。

未验证的能力包括 Windows 驱动兼容性、真实雷雳吞吐、显示链路的端到端延迟、睡眠 / 唤醒、锁屏和无物理显示器的捕获。
第一版不宣称已达到 2560×1600 / 240 FPS，也不支持 FileVault Preboot / loginwindow。

## 0.7.0 原生指针、系统深浅色、启动组件、图片与20Gbps上限（2026-10-04）

- 两端均0.7.0。Windows删除“缩放与边缘”和“1:1原始像素显示”控件、压缩卡片布局，保留HiDPI捕获选项及不锐化的默认GPU缩放；GUI包检查拒绝包含已删除控件的构建。控件删除不改变编码格式，仍BT.709 SDR / 4:2:0。
- 动态指针通过Mac运行时可选WindowServer getter读取系统本来的BGRA像素、尺寸、热点与像素变化；不自行绘制，不根据快捷键猜状态。先检查能力，系统不提供getter时保留SCK视频指针。Mac common run loop 60Hz轮询，复用有界像素缓冲，只编码并发送变化，不依赖形状seed而遗漏光标动画；Windows WIC解码、按自身DPI缩放热点一次，透明隐藏图及旧会话隔离已实现。实机只读读取了28×40箭头/热点5,5以及23×22文本光标/热点12,11。C++100/125/150/200/300%DPI测试覆盖居中热点与原生箭头热点；截图工具UI控制受限，截屏十字/拖动/应用自定义状态与Windows实际点击还须两机验收，不能把当前实现等同全部状态已测。
- Mac取消强制aqua，卡片/徽章随effectiveAppearance解析系统语义颜色；CUA确认系统Dark状态下原生窗口的文字、控件、卡片、剪贴板及启动卡片完整可见。Windows读取系统应用颜色及高对比度，DWM标题栏、原生控件主题与可选版本限定的UxTheme导出跟随系统变化；设置变化会刷新现有控件，防止WM_THEMECHANGED递归。Windows原生主题尚无ROG验证。
- 二进制CursorImage/ClipboardImage以3072-byte分块传输，分别512KiB/32MiB；文字仍64KiB。Mac原生独立命名测试粘贴板验证PNG/TIFF导出、PNG接收并提供TIFF、Unicode、连接不发送旧内容、回传抑制、禁用及旧会话。没有读取/改写用户剪贴板。Windows实现WIC PNG与PNG/DIBV5/DIB/Bitmap原生剪贴板转换；输入图片先检查8192单边/16M像素，32-bit才读取DIB alpha。ROG真实图片粘贴尚需验收。
- 64-bit HelloWide/WelcomeWide独立消息15/16、版本2，query5/bit64确认后允许10–20000Mbps和64MiB单帧；旧协议保持原结构及边界。7个C++程序与20个Swift XCTest通过。真实HostServer套接字+合成捕获回环通过20Gbps精确值、Main10/4096×2560、动态指针分块、204800-byte图片回传、文字/启停/交错Ping、UDP会话/端口校验及156分片180000-byte IDR恢复，不注入CGEvent，不捕获桌面。
- 当前VideoToolbox拒绝20000Mbps属性；实现硬件范围检测、恢复实际接受目标，在Welcome和Windows诊断明确报告降低结果，避免溢出/静默降低/连不上。当前Mac实测HEVC、H.264、Main10均接受10737Mbps目标并输出IDR/P/主动IDR各三帧，BT.709/Main10元数据通过。这是合成配置与输出检查，不是10.737Gbps持续输出或20Gbps网桥吞吐证明。自动推荐仍80–300Mbps。
- 最终真实HiDPI Main10捕获检查通过：4096×2560 RGB10→P010→HEVC Main10，首帧454386bytes，BT.709色彩及10-bit元数据通过；未保存或传输屏幕，没有注入输入。以前锁屏导致Selected display unavailable的问题不能从此次未锁定测试推断已支持锁屏。
- Mac原生SMAppService登录项在当前机器已启用，菜单栏Host正常监听10.0.0.2:47990。首次手动进程启用登录项时，新agent因单实例保护退出0；下次登录由launchd管理崩溃恢复，这次没有退出登录/重启机器验证。
- 开机组件源码已实现：不含AppKit/capture/CGEvent，普通nobody账户，未登录时仅在bridge同子网提供发现与HostWaitingForLogin，登录后关闭socket由桌面Host接管。修正了最初错误的_nobody账户、补齐ProgramArguments，并检测私有home目录无法被nobody遍历时改用系统helper目录安装，未放宽用户目录权限。系统管理界面注册不等于真实运行；中途的EX_CONFIG未当作成功。当地安装脚本经管理员确认可安装/卸载，原生SM与脚本资源及签名检查通过；最终重新启用被自动批准审核拒绝，要求操作时用户明确确认，当前开机服务保持关闭。未验证真实冷启动/注销接管。FileVault preboot和loginwindow远控仍未实现。
- Windows -Wall/-Wextra/-Werror GUI x64交叉编译通过。两端ZIP完整性、版本、文件字节、Mac可执行权限与持续签名严格检查；私有签名资料不打包。清理后dist仅保留两端0.7.0的app/运行目录与版本化EXE/ZIP。Windows native GPU/深浅色/动态指针/双向图片/真实雷雳吞吐需ROG实测。

- 最终 Windows EXE SHA256: `69ca7cc15e5244617a64cdab53845b1e5070f662e9c4f2674221bc51ac79c6d8`；Mac Host SHA256: `1dfd13a793127591624c965176a0b502fb249f0d3108229a0afbbda1dc5fe1fe`。
# 0.7.1 · 降低压缩强度（2026-10-04）

- 自动预算由 HEVC 0.45 / H.264 0.72 提高至 0.90 / 1.44 bit/pixel/frame，自动范围从 80–300 提高至 160–1000 Mbps。4096×2560@60 HEVC/Main10 从 290 提高至 570 Mbps；2560×1600@120 HEVC 从 230 提高至 450 Mbps。像素几何和桌面逻辑模式保持原有行为。明确自定义的码率保留；旧主机未声明大帧能力时，自动上限 300 Mbps。
- Mac 每帧预算充分时请求最大帧 QP 22，读取属性验证；低预算或属性不支持时不强制此约束。QP 为帧级参数，不代表各块无损。当前 Mac 的 HEVC、H.264 和 HEVC Main10 合成检查均接受并读回 22，1 Gbps 目标下各三帧输出、Annex B、强制 IDR 及 BT.709/Main10 元数据检查通过。10 Mbps 兼容检查跳过 QP 上限，三种编码各三帧通过。合成画面为 640×360，不是桌面清晰度对比或持续吞吐测试。
- 7 组 C++ 回归及 20 个 Swift XCTest 通过；Mac release 编译并签名，Windows x64 GUI 交叉构建通过，双端 0.7.1 包完整性验证通过，dist 仅保留最新版本。
- 色彩检查：Mac 捕获/编码请求 BT.709 SDR；Windows 解码标记 709 和 limited 范围，D3D 视频处理 limited YUV 到 full RGB，交换链为 G22/P709 SDR。没有新增色彩修复：P3/HDR 串流、端到端灰阶/色卡及 ROG 原生显示验收未完成。固定色彩元数据正确不等于两台面板显示已校准一致。
- 当前仍 4:2:0，减少压缩不能恢复已丢失的色度分辨率，也不能消除两端尺寸不同的降采样。Windows 最终观感待实机对比。


## 0.7.2 · 桌面 sRGB 色彩一致性

- 问题检查：旧链路请求 BT.709 捕获/视频传递曲线，输出为 DXGI RGB_FULL_G22_NONE_P709（sRGB 呈现定义）。没有端到端色卡验证，因此不能仅凭设置名保证灰阶/颜色在所有驱动上一致。原生合成灰阶探针实测 sRGB 128 经 BT.709 像素转换后，仅反解矩阵/范围得到约116；保留sRGB曲线后得到128。这是可能产生颜色/灰阶偏差的链路证据，不是 ROG 褪色原因已经确诊。
- 新 query6 专用桌面路径：Mac 请求 sRGB BGRA8/RGB10 捕获，VideoToolbox 保留 sRGB 传递函数转换为 NV12/P010，并编码为709 primaries、sRGB transfer、709 matrix、limited range。Windows 使用显式 GPU shader 矩阵/范围转换成全范围 sRGB RGB8/RGB10，绕过视频处理器的色彩转换和传递曲线解释。保持现有缩放及不锐化策略，不增加饱和度、对比度或Gamma补偿。兼容缩放路径也保持同一 sRGB 转换结果。
- 新标记 bit7/128 仅在query6回复中出现，必须包含bit6/64。query1–5仍保持原有capabilities及BT.709捕获。客户端6->5->4->3->2->1兼容旧主机；新GPU色彩资源不支持时以明确诊断和query5回退，避免反复等待首帧。
- 原生合成色块：scripts/check-desktop-color.swift 使用16组灰阶及彩色色块，调用真实VideoToolbox RGB->YUV转换，再用Windows shader相同矩阵/范围反解。8-bit最大通道误差1.280/255，10-bit最大误差2.222/1023；黑白在一个YUV码值误差内。此检查不读取屏幕、剪贴板或网络，不包含编解码损失及Windows GPU实机执行。
- HEVC、H.264及HEVC Main10合成硬件编码检查均接受sRGB元数据，Annex B与IDR检查通过。真实SCK 4096×2560 capture分别返回BGRA8/RGB10，经NV12/P010编码首帧452665/423541字节，sRGB/709 matrix元数据验证通过；未保存或发送桌面画面。
- 新HLSL的vertex、YUV conversion、bilinear blit入口通过glslang HLSL校验；Windows x64 GUI交叉构建通过。7组C++回归及20个Swift XCTest通过。生产HostServer回环测试验证query1–6、Welcome、Ready及UDP中选定的色彩分支，后续query5不继承query6色彩；捕获与输入用桩隔离，未访问用户桌面。
- 双端0.7.2签名/GUI和压缩包验证完成，dist只保留最新构建。ROG原生D3D11执行、HDR开启下的最终观感和真实两机色卡对比仍待实机验收；P3/HDR/4:4:4未在本次实现。

## 0.7.3 · 自启动状态与断线恢复

- 现场检查旧登录服务182次尝试后为spawn failed / exit78，开机服务196次尝试同样失败。日志确认nobody无法访问私有用户目录内helper；登录服务无法执行bundle-relative Program。注册状态不等于进程运行。
- 改用SMAppService.mainApp，取消旧登录服务；系统设置实查“登录时打开”已显示ThunderDisplayHost，旧登录服务已卸载。新版经LaunchServices原生启动后，生产主机实际监听10.0.0.2:47990。实际注销、重启验收未执行。
- 主App登录项不提供KeepAlive，本次移除“崩溃后恢复”的UI承诺并记录后续TODO。待批准和实际服务状态分开显示。
- 开机服务统一通过管理员安装组件放到绝对系统路径；不放宽用户目录权限。状态检查实际launchctl进程和退出码。管理员安装认证仍待用户完成，不能宣称开机服务已恢复。
- Windows每轮恢复最多10分钟、150次，尝试起点至少4秒间隔，发现/连接/握手等待统一计时；实际呈现首帧才结束本轮。显示number/150，超限停止并启用手动连接。
- 原Renderer.resetFrame仅清标记，现清空实际交换链并呈现黑色。实际呈现时间决定画面有效性；3秒无解码输出清除旧画面，12秒无解码输出重连。控制握手、等待登录、等待首帧与实时画面分别显示。
- 8组C++回归和22个Swift XCTest通过，包含150次/10分钟边界、成功后新一轮和画面过期。双端构建及Mac签名、GUI版本、压缩包完整性验证通过，dist只保留0.7.3。ROG原生GPU清屏、双机重启/断线及睡眠唤醒仍需实机验收。

## 0.7.4 · 睡眠唤醒后主机自动恢复

- 根因：旧onDisplay在显示器临时消失时将autoStart设为false，导致唤醒后永久等待手动启动。现在使用运行意图与睡眠状态分离的HostRecoveryState；系统通知和临时捕获故障不修改运行意图。睡眠时关闭捕获及网络并释放按键；唤醒和显示器恢复后重建捕获、显示器能力及网桥服务，失败继续重试。
- 使用NSWorkspace willSleep、didWake、screensDidWake通知；显示器缺失时持续重新检测且保留首选ID。清除瞬时捕获故障缓存，不将睡眠当成撤销屏幕权限。旧主机异步回调不能改变新主机状态。
- HostPowerManager使用公开IOPM PreventUserIdleSystemSleep断言，默认开启并提供UI开关；不修改系统睡眠设置、不禁止显示器熄屏、不阻止手动睡眠。退出、主动停止及系统睡眠时释放，唤醒后重新取得。Apple TN3205明确雷雳不支持Wake-on-LAN，所以未实现虚假的远程网络唤醒。
- 8组C++和27个Swift XCTest通过；睡眠/显示器通知顺序、唤醒重试与显式暂停回归均通过。双端0.7.4构建完成。
- 原生--recovery-check独立回环进程以App真实通知处理、生产HostServer、SCK和VideoToolbox测试。未执行真实系统睡眠、未注入键鼠、未保存画面、不写用户主机配置。AUTOSTART/WAKE/DISPLAY三阶段分别收到640×360 H.264完整SPS/IDR首帧38411/38224/37143字节，三个session ID不同；睡眠阶段确认监听关闭、断言释放且运行意图保留，唤醒阶段确认恢复。
- 用户实际Mac App已重启至0.7.4，原生UI确认屏幕/键鼠授权有效、主机运行、Windows重新连接4096×2560/60Hz HEVC Main10以及保持唤醒开启。开机发现LaunchDaemon已运行。真实硬件睡眠、Windows最终画面与长时间熄屏仍需实机验收。

## 0.7.5 开机任务冲突（2026-10-05）

- 用户实际重启后复查：旧 `dev.thunderdisplay.boot` 由 smd 重新提交，覆盖同名绝对路径任务；bundle version16、spawn failed、exit78。此前安装当时 running 的检查不代表冷启动成功。
- 改用独立 `dev.thunderdisplay.boot.system` 系统 LaunchDaemon。管理员安装时持久 disable 旧标识、bootout 旧任务并删除旧系统文件；新 App 不再携带旧 bundle LaunchDaemon。未修改用户目录权限或全局后台项目数据库。
- 状态检查只接受新任务及期望的系统 plist 路径，新增旧 ServiceManagement 任务不能证明新任务运行的回归测试。8组C++测试、28项Swift XCTest全部通过；Mac release与Windows x64 GUI构建、签名、版本与包校验通过。
- 系统管理员安装已完成。实际任务路径 `/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist`，程序 `/Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system`，root:wheel所属、以nobody运行。初始PID2214；bootout/bootstrap重新加载后PID2323仍running。`print-disabled system`确认旧标识disabled、新标识enabled。安装helper与签名包helper SHA256均为 `159a8b2180131f937809b87669d729189d138613295edc83c2cd01facc03fa9b`。
- 用户实际App已更新至0.7.5，桌面主机PID2245监听10.0.0.2:47990；主App原生登录项仍注册。dist仅保留0.7.5的Mac包、Windows GUI包/EXE和两个运行目录，旧版本包已移除。
- 开机组件新增自身启动与console登录状态的系统日志。此次日志为console logged in:true，不能将本次登录后重载当作真实冷启动/登录前验收。尚未执行真实重启或注销；登录前桌面捕获与键鼠控制仍未实现，helper仅提供发现/等待登录。FileVault preboot仍不支持。

## 0.7.6 本地指针清晰度（2026-10-05）

- 只读实测旧WindowServer路径对当前I-beam返回23×22像素；同一当前系统NSCursor具有23×22、46×44、115×110、230×220原生图。低密度原图在Windows高DPI放大是本地指针模糊的原因之一。
- 新版优先使用当前系统指针的最高原生位图（每边不超过1024），逻辑尺寸、热点独立保留为points；向量-only系统资产由AppKit读取原资源。当前系统getter已弃用，保留WindowServer原图兼容路径，不用自绘箭头替换未知/截图形状。没有锐化。
- Windows按像素覆盖率下采样一次，1:1保留原始像素，只有缺少更高分辨率资产时放大插值。回归检查2x/5x/10x硬边缩放、透明预乘边缘、150%DPI与热点；Mac检查多密度原图选择、原数据保留、自定义截图形状与空图回退。8组C++测试与31项Swift XCTest全部通过。
- Mac release与Windows x64 GUI严格交叉编译、签名与发布包校验通过。最终签名Mac `--cursor-check`确认箭头280×400原生像素、28×40逻辑尺寸、5×5热点。实际App更新至0.7.6，PID2342监听10.0.0.2:47990，登录项及系统开机helper均正常。dist仅保留最新0.7.6构建。Windows原生HCURSOR显示与ROG观感尚未实测。
- 截图中的远程桌面持久权限尚未申请。当前本地签名无Persistent Content Capture授权资格；Apple文档要求申请获批后配置授权签名。记录外部申请待办，未把普通屏幕录制授权误报为持久远程桌面授权。
- 顺带取得用户真实重启证据：系统helper PID313于15:00:27启动，console logged in:false；15:00:32登录前网桥监听就绪，15:02:23记录用户登录。证明0.7.5系统发现任务已跨重启启动，不证明具有登录界面捕获或键鼠控制；这两项仍未实现。

## 0.7.7 原生指针倍率匹配（2026-10-05）

- 同一形状保留1x/2x/5x/10x等macOS原生表示，query7协商多图CursorImage v2；query1–6仍用单图v1，query6的sRGB行为不变。Windows选择覆盖当前DPI的最小原图，没有足够像素时选择最大原图；对应倍率1:1显示不重采样，跨显示器自动从缓存集合选择。逻辑尺寸与热点不随图像倍率改变。
- 实际签名App `--cursor-check`读到箭头28×40、56×80、140×200、280×400四个原生版本，逻辑尺寸28×40、热点5×5。没有锐化、自绘或移除系统阴影。
- 8组C++与33项Swift测试通过，覆盖新旧body、固定点坐标、过量/截断/尾随数据、PNG签名、原图DPI选择及旧像素/热点测试。真实HostServer回环协商query6/7和旧query1–5、剪贴板、UDP/分片测试通过；修正了过时测试替身未接收desktopSRGB参数的问题。
- Mac release签名与Windows x64 GUI严格交叉编译、版本和ZIP校验通过。用户Mac已更新运行版本；Windows原生HCURSOR最终观感仍未实测。dist仅保留0.7.7构建。

## 本地持久屏幕捕获资格实验（2026-10-05）

- 用 `scripts/probe-persistent-capture.py` 将同一独立诊断程序分别签名为对照 App（无 entitlement）与测试 App（声明 `com.apple.developer.persistent-content-capture=true`），使用项目现有本地身份、不同 bundle ID。两个 App 均通过 codesign 严格结构校验；不代表系统接受受限资格。
- 在正常应用运行环境执行只读 `--preflight`：对照进程进入程序并退出 0；测试进程退出 -9（SIGKILL），未打印入口信息。沙盒内重试也是相同运行/终止差异。普通屏幕录制 preflight 的值不能用来证明持久权限。
- 15:53:54 系统日志：taskgated-helper 对测试 ID 报告 `no eligible provisioning profiles found`；amfid 报告 `No matching profile found`（-413）；内核 AMFI 报告 `Code has restricted entitlements, but the validation of its code signature failed`，并拒绝加载。拦截发生在程序执行之前，没有进入 ScreenCaptureKit 的用户授权流程，也没有申请成功截图中的“远程桌面”权限。
- 实验源码与复现说明在 `tests/PersistentCaptureProbe/`；原始结果在 `build/tests/persistent-capture/preflight-results.json` 与 `system-rejection.log`。未修改正式 App、现有 TCC 授权或系统安全策略，未将诊断包放入 dist；没有将该资格等同于登录前远控。下一步依 Apple 文档申请资格并取得匹配签名配置。

## 再次冷启动核对与登录前远控缺项（2026-10-05）

- 用户澄清失败场景为“还没登录 Mac，Windows 无法显示或控制登录界面”。独立 system LaunchDaemon 已存在且启用：`/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist`，程序 `/Library/PrivilegedHelperTools/dev.thunderdisplay.boot.system`，RunAtLoad / KeepAlive，账户 nobody，当前 PID293、runs1、未退出。
- 本次真实重启日志：15:59:38进程启动且 console logged in:false，15:59:43监听就绪，16:02:46 console logged in:true；主 App PID885起于16:02:56，原生登录项状态 enabled。开机发现及登录后 App 启动不是用户所需登录前远控的验收。
- 源码核对：Boot不导入屏幕捕获或输入 API，没有 LoginWindow 会话 agent，连接只返回 HostWaitingForLogin。因此此问题是功能尚未实现，不是 LaunchDaemon 文件未安装。Apple TN2083明确系统上下文与图形会话上下文不同；不能把 root 运行或声明持久 entitlement 当作自动取得登录界面捕获、输入权限。
- 本轮只诊断并明确未完成事项，没有改为 root、安装未经验证的登录组件、注销或重启用户电脑。当前后台服务和桌面主机保持运行。

## 0.8.0 LoginWindow 图形组件实现与安装（2026-10-05）

- 新增LoginWindow专用入口及全局LaunchAgent，安装完整应用副本至 `/Library/Application Support/ThunderDisplay/ThunderDisplayHost.app`，root:wheel所属、目录/可执行文件755且其他账户不可写。LaunchAgent plist为root:wheel644、LimitLoadToSessionType=LoginWindow、RunAtLoad和失败退出KeepAlive。原系统daemon仍nobody运行，无屏幕/输入API。
- 通过系统管理员安装更新组件，不放宽用户目录权限、不添加受限entitlement或修改TCC数据库。私密端口/配对配置保存为root600；读取验证文件类型/所有权/权限/长度。配对默认关闭的选择保留，root主机强制同网桥子网，登录前不开放剪贴板/文件通道。
- 先运行64×64完整帧检查和事件发送授权检查，再发布root644状态心跳claim端口；发现daemon让出端口，12秒过期恢复回退。捕获异步调用60秒不完成时失败退出，由launchd恢复；真正用户拒绝避免反复请求。root状态文件拒绝symlink、非root及group/world可写文件。
- 会话守卫使用launchd manager类型而非单凭Security图形位，未知控制台状态不能注入输入。登录前每个键鼠包重新确认控制台会话；登录后清理捕获、连接和已按下的键鼠。桌面主机在非当前用户会话释放端口并保持恢复意图，NSWorkspace会话切换通知及周期复查负责恢复。配置/实际帧/输入错误保留至登录后UI，不能把安装或检查结果显示成实时连接成功。
- Windows新增三种登录前状态并直接保留failure，不降级capability query；现有10分钟/150次恢复规则继续生效。新版两个平台0.8.0；dist仅最新构建。
- 8组C++和38项Swift检查通过（17Wire、18HostState、3CursorSupport），包括状态过期/未来时间/错误版本、禁止unprivileged状态写入、失败历史和配对策略。实际HostServer套接字用合成视频验证Ready后禁止输入的包未到达注入桩，同时完成旧/新协商、UDP IDR恢复及剪贴板；没有系统输入注入。
- 当前真实用户Aqua桌面执行新的实际帧检查器成功，display5、UID501，未读取保存/传输像素或注入输入。root普通system及bsexec试验未获得可用于本检查的正确图形bootstrap；preflight结果因上下文不同而变化，未将其当作Root LoginWindow授权或捕获证据。
- Mac持续本地签名release与Windows -Wall/-Wextra/-Werror GUI构建通过，两端包校验完成。系统安装文件匹配签名构建。仍需用户保存工作并注销/重启，在真实未登录状态实测首帧、键鼠与登录后Windows重连；本轮未代用户注销/重启，未宣称已完成登录前远控验收。
- 最终安装校验：系统应用与发布Host SHA256均为 `1cef85a43d3cdb9eb2bb9f9fa67053748f5659b3b51eba8a68de6f307df37c36`，两个helper副本亦匹配；root应用deep/strict签名检查通过。系统daemon PID4818（nobody），当前桌面主机PID4907（UID501）正常监听10.0.0.2:47990，原生登录项仍enabled。Windows GUI SHA256 `8d8c0587a093778c4d566bb1bea9b2006128fde755bf608b774b31e4f8e1f2ab`；dist只留0.8.0两端运行目录与最新EXE/ZIP。

## 0.8.1 Windows Caps Lock 状态修复（2026-10-05）

- 根因：键盘钩子转发 Caps Lock 给 Mac 后吞掉原始 key-down；远程大小写有效，但 Windows 的切换状态及键盘指示灯不更新。
- Caps Lock 现在在转发后继续交由 Windows 处理，窗口和全屏均更新本地状态；只在第一次按下时翻转远程状态，长按重复事件不反复切换。全屏快捷键和其他 Windows 快捷键的拦截规则保留。
- 远程窗口获得焦点、显示首帧、恢复输入捕获时重新读取本地切换状态，覆盖在其他窗口或输入释放期间切换 Caps Lock 的场景。输入协议和 Mac 功能代码未改；此修复仅需 Windows 0.8.1，兼容 Mac 0.8.0，无需重新授权或更新开机组件。
- 现有快捷键回归测试通过，包含窗口/全屏 Caps Lock 放行及普通键、Win/Alt+Tab 继续拦截。Windows -Wall/-Wextra/-Werror GUI 严格交叉构建通过。Mac release、持久项目签名和 deep/strict 校验通过；Mac 只更新发布版本号，未重复运行 Swift 测试。
- 两平台 ZIP 完整性通过，dist 仅保留各自最新构建。Windows GUI SHA256：`8135e372ce9564b2b022885eb397a8ed1706fa5a7fc79faabb33ea0e990e98ec`。Mac 0.8.1（build21）Host SHA256：`4453457313f5f8830526d9720adbaf51339fe371c8db92a73e3e11337b0772f5`。
- 待 ROG 实机确认：窗口/全屏连续两次切换及长按，本地切换后返回远程，以及释放/恢复输入后的大小写和指示灯表现。未声称已观察到真实键盘指示灯变化。

## 登录前组件控制台判定修复（2026-10-05）

- 用户报告“系统启动后、用户登录前不能工作”。两个组件都按设计自启动了，失败发生在启动之后：本轮两次冷启动，17:15:49 开机后 LoginWindow agent PID279 在 96.3 秒写心跳，17:23:49 开机后 PID276 在 132.1 秒写心跳。
- 根因由系统日志直接给出。17:23:55.120 agent 记录 `LoginWindow phase: blocked; Waiting for a confirmed LoginWindow console session`；同一时刻 17:23:54.473 system daemon 记录 `Console logged in: false`。两者使用同一个 `SCDynamicStoreCopyConsoleUser`，判定却相反：`ThunderDisplayBoot.consoleLoggedIn()` 把“没有 console 用户”当作未登录，即登录前（回退就绪，符合预期）；而 `ConsoleSession.preLogin` 要求必须匹配到 `loginwindow` 且 uid<500，于是返回 false。agent 因此在整个登录界面期间停在 `.blocked`，从未探测采集，直到 17:25:55.718 用户登录后直接发布 `stopped`。
- 两次心跳的 `captureChecked`/`inputChecked` 都是 false 且 `lastFailure` 为空，与该路径一致：探测从未运行，所以从未抛出捕获错误。此前把这一现象解读为“登录前授权被拒”不成立。
- 修复：`ConsoleSession` 拆出可测试的纯判定 `isLoggedIn(consoleUser:uid:)`，`isPreLogin` 定义为其补集，主 App 与 system daemon 共用同一实现（`ThunderDisplayBoot.consoleLoggedIn()` 改为委托 `ConsoleSession.loggedIn`）。登录前不再有第二套定义，两者不可能再分歧。
- 修复：`LoginWindowHost.refresh()` 删除重复派生 `preLogin` 的分支——该分支正是掩盖真实阻塞的位置；保留真正需要的图形会话检查，以及“普通 preflight 为假但实际流可用”的既有容错。
- 可观测性：agent 首次发布 `stopped` 时，把离开时的 phase 与 detail 快照进心跳新字段 `previousPhase`/`previousDetail`，并记录墙上时钟 `recordedAt`。此前 `applicationWillTerminate` 固定覆盖为 “LoginWindow agent stopped”，把唯一的原因抹掉，本轮两次心跳都因此无法解释，只能靠日志反推。
- 可观测性：新增 `LoginWindowState.readLastRecorded()`，跳过新鲜度校验但保留 root 所有权、非符号链接、大小与版本等结构校验。`fresh()` 要求 `now >= uptime`，而 `uptime` 跨重启重置，导致重启后永远读不到上一次开机的心跳，这也是此前无法诊断的原因之一。端口让与实时监听判定仍只用 `read(maximumAge:)`。
- 新字段为可选附加项，`version` 保持 1：已安装的旧二进制写出的心跳仍可解码，用磁盘上真实心跳（PID276）验证通过，避免升级瞬间丢失记录。
- 可执行的失败说明：登录界面上下文中 ScreenCaptureKit 的拒绝原本显示为 Apple 的“用户已拒绝”，但登录界面没有用户可拒绝、也没有可打开的系统设置。现映射为明确指引（PPPC/MDM 预授权或自动登录），辅助功能（事件注入）授权失败同样处理。
- 已验证：`swift build` 通过；42 项 Swift 测试通过（17 Wire、22 HostState、3 CursorSupport），其中 `LoginWindowStateTests` 由 5 项增至 9 项，新增覆盖“上一次开机的心跳只能作历史读取”“旧格式心跳缺少新字段仍可解码”“快照字段有长度上限且不能伪造监听状态”“无 console 用户时两个判定互补”。8 组 C++ 测试通过。新编译的 `--login-window-check` 成功读取磁盘上由旧二进制写出的真实心跳。
- 未验证：本轮**没有**在真实未登录会话中复测。修复后 agent 应能越过该闸门并真正探测采集；若届时被 TCC 拒绝，心跳会记录明确的授权指引而不是笼统信息。登录前画面与输入的实机验收，以及 PPPC/MDM 描述文件或自动登录的实际可用性，仍未确认。
- 本轮未修改版本号、未重新签名或打包 dist、未注销或重启用户机器，未修改 TCC 数据库或系统安全策略。

## 0.8.2 登录前键鼠输入（2026-10-05）

- 新分支的冷启动日志确认捕获和事件发送权限预检已通过：17:43、17:52、19:20、19:31 的 LoginWindow agent 均进入监听并接受客户端协商，随后在用户登录时停止。19:31:11 的 PID283 记录输入预检通过，19:31:15 客户端连接，19:31:58 交接。不能把本次键鼠不可用继续归因于启动或授权预检失败；旧 CGEvent 发送接口没有提供送达结果，具体丢弃点尚未证明。
- 新增系统 HID 输入后端，使用公开的 `IOHIDPostEvent` 返回发送状态；键盘传递物理键码、左右修饰键和重复标志，并使用 HID manager 事件选项服务安全输入消费者。鼠标用 Quartz 逻辑坐标定位再发送 HID 事件，避免旧 16-bit framebuffer 坐标与 HiDPI 不匹配。系统授权仍生效，没有原始用户客户端 selector、虚拟 HID 受限资格或 TCC 绕过。
- 后端只由 root/LoginWindow 主机和显式 Aqua 输入诊断选择。用户桌面保留 CGEvent 路径。输入错误立即失败断开并保留系统状态码；停止登录前会话时清理按键/鼠标。普通输入、黑边坐标、三键、拖动、双击、滚轮、左右修饰键、长按和 Caps Lock 状态保留。
- 删除冗余控制台字典检查和旧权限失败映射函数，撤回“只能用 PPPC/MDM 或自动登录”的未经验证建议。保留真实帧检查、运行环境校验、状态心跳、显示模式/串流尺寸日志和登录交接。
- 真实原生输入诊断通过：签名 App 用自己的临时窗口接收系统鼠标按钮点击、输入框定位；显式启用安全输入后，真实按键和右 Shift 生成预期测试字符。字段内容没有打印/保存，窗口随后关闭，恢复鼠标位置和原有修饰键。首次诊断未取得焦点而安全取消，修正为 AppKit 完成启动后激活，再确认焦点后发送事件。
- 48 项 Swift 检查通过（17 Wire、22 HostState、3 CursorSupport、6 InputSupport），输入测试使用替代发送器，不操作系统输入；最后追加鼠标移动保留左右 Shift、断连鼠标释放保留 Caps Lock 两项断言并重跑 6 项输入检查通过。8 组 C++、Windows 严格 GUI 构建与 C `-Wall/-Wextra/-Werror` 检查通过。
- 真实 HostServer 回环通过旧/新协商、合成视频、UDP 探测/IDR 恢复、剪贴板及 Ready 后会话撤销；输入被禁止时不会送达注入桩，模拟发送失败返回 `InputUnavailable` 原因并断开连接，主机继续运行。此回环不注入系统事件。
- 待验收：真正未登录 LoginWindow 中的鼠标、密码输入、登录/注销接管。Aqua 安全输入实测和实际接口返回成功不能替代双机 LoginWindow 验收；本轮不自动注销或重启，FileVault preboot 仍不支持。
- 安装校验发现旧本地签名脚本的私密 umask 也使公开 `_CodeSignature/CodeResources` 成为0600；复制成root所有后，普通用户无法验证签名。签名及安装脚本现将这个公开清单设为0644，root系统副本仍不可写；私有签名目录、钥匙串和配置仍保持原权限。打包同时检查清单可读性。
- 0.8.2（build22）最终安装通过：发布、`/Applications` 和root系统副本的Host SHA256均为 `41fb8146cac390462ced8d463260d4ba80b6c6b241033580d2fe7ab0e51f8c3b`，系统副本deep/strict签名检查通过；系统helper匹配发布副本，LoginWindow任务仅注册图形登录会话，配置仍root600。登录项已注册、系统daemon运行；新版用户桌面恢复10.0.0.2:47990监听。
- Windows GUI SHA256：`25cdbe5be7853ea373d6b9be50f7abc8fd62c6d676904f6f686d6f6df6f4b7be`。两端ZIP完整性通过，dist只留0.8.2的App、EXE、两个ZIP及Windows运行目录，没有保留旧构建。

## 0.8.3 登录前输入准备阻塞（2026-10-05）

- 用户在真实登录界面仍无法使用，Windows显示找不到主机或连接失败。本次21:09:38冷启动，launchd在21:09:44启动LoginWindow agent PID290；网桥在21:09:59可用，21:10:02开始真实屏幕检查，21:10:03完成捕获停止与释放，未进入监听。21:14:08用户登录，21:14:13对应LoginWindow任务被移除。不是未安装或未启动。
- 屏幕检查后的新输入准备阶段，21:10:03.020系统记录 `This program is not permitted to connect to or launch Window Server before login`，随后 `Window Server is not available`；心跳停在checking、captureChecked/inputChecked均未发布为true。代码中的 `TDHIDCheck` 会读取 `CGEventSourceFlagsState`，这个新增全局状态查询是可疑触发点；没有存活进程可采样调用栈，不能把相关日志当成已经证明具体调用位置。
- 修复：HID检查移除全局输入状态读取，使用公开 `IOHIDCheckAccess` / `IOHIDRequestAccess` 完成授权，再发送不修改光标/标志/文本的空事件检查连接；实际输入检查每次发送结果。避免额外的Quartz全局输入状态连接，不绕过TCC。普通桌面输入保持原路径。
- 修复：原60秒超时放在AppKit主循环上的Timer；本次主循环停止写心跳后，它也无法执行。新增独立队列准备监控，捕获60秒、各HID准备步骤15秒、输入事件对象准备15秒上限。超时保留具体步骤并失败退出，由启动任务重建；仅在接受输入前启用。独立监控不尝试在用户输入过程中强制退出。
- 新增明确阶段：有效帧完成、事件授权、打开HID连接、检查系统发送、创建键鼠事件对象；发现daemon记录客户端到达回退服务及当时阶段，不记录远程输入或密码。
- 52项Swift检查通过，其中新增4项真实后台监控检查覆盖调用线程阻塞、完成取消、更新期限、旧操作取消不影响新操作。严格C编译检查通过。登录前实际串流与键鼠仍需要真实会话复测；本轮不自动注销或重启用户机器。
- 真实鼠标检查进一步发现，分开使用Quartz定位与HID发送时，系统事件坐标未落在测试目标，诊断在点击前取消；改成在同一公开HID调用中发送绝对位置和点击。实际Aqua桌面坐标、按钮点击、输入框聚焦、Shift与安全输入检查通过，没有手工绘制或锐化指针。
- 最终签名0.8.3（build23）使用空事件准备和同一HID连接定位后，再次通过Aqua诊断：鼠标点击、字段聚焦、键盘、Shift、安全输入字段均验证成功，未记录用户文本或密码。此结果不替代真实LoginWindow会话验收。
- 已通过管理员安装更新：发布、`/Applications`及root系统副本版本均为0.8.3（build23），Host SHA256均为 `81dd59047585be3941ea56a0f35e6c97c57b01bf6d0b5ec654173e2b4655d5c4`；安装副本deep/strict签名验证通过，root系统副本不可由普通用户修改，配置仍root600。原生登录项已注册、系统daemon运行，用户桌面主机恢复10.0.0.2:47990监听。
- Windows严格GUI构建通过，EXE SHA256为 `6b0da79f3b53af0b32dac886b99b0c33db2d9d89ab50f3418cf95b55e180bc02`；两端ZIP完整性通过。dist只保留0.8.3的Mac App、Windows运行目录、EXE和两个ZIP，旧构建已清理。

## 0.8.4 登录交接、桌面模式与冷启动（2026-10-05）

- 用户报告注销后登录桌面长期断连/端口占用、2048逻辑桌面变成2560，以及重启后不连接。本次冷启动22:14:05；root LoginWindow PID284在22:14:12启动，22:14:19有效帧完成，但HID返回0xe00002c1并在原进程每30秒重复到登录。注销后的新LoginWindow PID1460在22:20:13通过相同输入接口，22:20:14监听，22:20:15客户端协商。冷启动不是程序未执行。
- SDK中0xe00002c1为kIOReturnNotPrivileged，不是kIOReturnNotOpen。Apple公开IOHIDParamUserClient代码另检查当前本地用户上下文；其与冷启动/注销后的行为差异一致，但没有对本机内核采样证明具体判定。仅对此错误、且Quartz事件发送权限有效时，尝试标准授权CGEvent路径，不改TCC、系统信任、受限entitlement或用户身份。Quartz没有送达返回值，不以权限检查代替登录字段验收。
- 原stop先disconnect/采集退出，再异步取消监听，可能将端口保留在慢的编码器退出期间。新版先取消TCP监听、UDP发现/视频和旧控制连接，等取消处理确认关闭，再清理输入和采集。交接旧控制连接使用零linger，避免root连接残留占用；已关闭描述符不再次shutdown，避免操作复用的描述符。会话变动后旧输入被拒绝。
- 实际HostServer合成回环通过旧/新协商、UDP恢复、剪贴板和输入失败反馈；新增阻塞采集退出的测试，阻塞期间5次成功接管TCP和UDP发现端口，旧连接已关闭。主机接管端口250毫秒重试，桌面恢复清除旧延迟；Windows显示过帧后的恢复前8次250毫秒重试，仍受10分钟/150次总预算约束。
- 新增root所有的全局Aqua启动任务，启动既有签名系统应用副本，但运行在普通用户图形会话。仅允许配置中的desktopUID运行，其他用户在任何权限、UI或采集调用前退出。保留原生主App登录项与每用户实例锁。安装/卸载和发布校验包含该组件，旧配置缺少desktopUID仍可读。
- 分辨率变化来源：22:06:45.645 WindowServer重建Display8，22:06:45.647提示保存模式无匹配，切换70→67；22:06:45.910 BetterDisplay记录虚拟模式应用，22:06:46.652记录Display8模式67为2560×1600。程序无显示模式切换调用，不添加恢复/重设模式逻辑。在BetterDisplay配置保护中启用当前2048×1280 HiDPI，界面橙色分辨率保护和保存值确认。
- 完整检查54项Swift和8组C++通过；授权Quartz实际诊断通过鼠标定位/点击、字段聚焦、键盘、Shift和安全输入字段，只使用程序自有测试窗口，不记录用户文本或密码。签名App真实ScreenCaptureKit→RGB10→P010→HEVC Main10检查通过：桌面捕获前后2048×1280，输出4096×2560；未保存或传输图像。
- 未自动注销/重启用户机器。新版真实冷启动的Quartz送达、登录后快速接管，以及BetterDisplay模式保护在重登/重启后的行为仍待双机实测。
- 发布与安装：两端版本0.8.4，Mac build24；`/Applications`与系统副本通过严格签名校验，Host二进制SHA-256均为`f2fb1d30a806181e83e6f5b48df4212fc21c92163e94af5574a160771771c115`。Windows严格交叉构建通过，GUI二进制SHA-256为`6d1abbf5c4b69074642c524f4c0a4b81bc1a787ba3585bd6ed8a1a9165b23e58`。最终授权回退专项测试实际执行1项并通过。
- 安装后`gui/501/dev.thunderdisplay.desktop`通过RunAtLoad自动启动PID4752，以普通用户caoenming/UID501运行系统应用副本，实际监听`10.0.0.2:47990`；原生主App登录项已注册，系统守护进程运行，LoginWindow组件安装完成。本次证明当前Aqua会话自动启动，未将其描述为冷启动登录前输入验收。
- 两端zip发布校验通过；`dist`仅保留0.8.4 Mac应用、windows-x64目录、GUI单文件及两端zip，共5项，旧构建已清理。

## 0.8.5 冷启动输入路由与画面交接（2026-10-05）

- 23:14注销后的LoginWindow PID5097通过System HID；23:15冷启动PID285通过屏幕帧后NativeHID本地用户检查拒绝，选择Authorized Quartz并监听/协商。用户确认冷启动能看画面但键鼠无效，证明授权检查不等于实际送达。
- 备用输入改为标准cgSessionEventTap，保留CGPreflightPostEventAccess检查，并对鼠标使用CGWarpMouseCursorPosition检查返回值。未改TCC、身份、系统信任或受限资格。公开定义见[Apple会话事件入口](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cgsessioneventtap)。键盘无送达回执，实际LoginWindow验收仍待重启。
- 输入诊断使用与InputInjector相同privateState事件源；正常.app启动方式通过鼠标坐标/点击、字段聚焦、Shift和安全输入字段，不记录文本或密码。直接从CLI运行时权限预检拒绝，未绕过该拒绝；LaunchServices以应用身份启动诊断后通过。
- 协议查询8向下回退兼容旧主机；新增带当前协商会话ID的交接通知和确认回执，错误ID、尾随字节、截断、错误方向/原因被拒绝。主机最长等回执200毫秒；旧会话通知后不再接收输入或传输视频，先释放端口再清理采集。
- Windows单帧RGB GPU快照保留，仅显式交接通知后使用；不加文字、不弹设置，不以保存画面更新实时帧时间或允许输入。新桌面实际Present后结束保留，调整大小及8/10-bit协商时可重绘，最长30秒、重试/重复通知不续期。普通断网仍清除画面。
- 55项Swift、8组C++检查通过；HostServer真实回环合成测试验证通知/回执以及慢采集退出期间TCP/UDP端口接管，无OS输入注入。Windows严格交叉构建通过；原生ROG GPU交接画面与冷启动实际键鼠仍需双机复测。
- 最终安装核验：主App与root所有的系统副本均为0.8.5/build25，三份Host SHA-256一致：`0fc2a71ca4da97d516a27131c3de5dfe804c930adcab8649e6044e5ab474e5f2`；系统副本深度/严格签名通过。原生管理员安装成功后退出旧用户主机，由`gui/501/dev.thunderdisplay.desktop`拉起PID3154，以UID501运行并实际监听`10.0.0.2:47990`。系统守护进程与原生登录项状态正常。
- Windows GUI SHA-256：`a88191ddf2e2b52233a97b262013a32b48e0f19c4d3048abe7f5573c08045f9e`。发布检查确认交接GPU绘制与会话通知标记存在、无“正在进入桌面”文字覆盖。两端zip校验通过，dist仅5项0.8.5构建，旧版本已清理。
- 追加交接在途输入测试通过：收到通知后、确认回执前的键盘包未进入旧InputInjector，端口仍可在采集阻塞退出时被接管。当前改动没有替用户重启或注销。

## 0.8.6 登录前键盘定向路由（2026-10-06）

- 用户确认0.8.5鼠标已修好，但Tab、方向键、回车和字符均不响应。最新冷启动23:52:51主机启动，23:52:58完成有效帧，HID本地用户检查拒绝后选择授权Quartz，23:52:59协商；不是主机没有运行。WindowServer键盘焦点与分发日志指向系统loginwindow PID174。没有修改系统信任、TCC、受限资格或用户身份。
- 授权备用路径新增独立键盘目标：在LoginWindow图形会话获取前台PID，以内核进程信息核对root UID/real UID及不可修改的系统loginwindow路径。按键和flagsChanged仅投递一次到该进程；每次发送前重新核对身份，进程变化即失败。鼠标与滚轮仍使用原会话事件入口，原生HID成功路径保持不变。
- Windows在新会话首帧和焦点恢复时重新安装WH_KEYBOARD_LL，成功后才卸载旧hook，失败保持旧hook并显示中文/英文提示。没有证据证明ROG上hook超时移除就是本次原因，这一项用于恢复该可丢失的捕获状态。按键释放和保留最后一帧逻辑保持。
- 主机每会话仅记录一次首次收到键盘包，不记录键码、字符、密码或按键次数。真实HostServer TCP/UDP回环证明拒绝输入和交接后在途输入不触发收包标记，允许输入触发标记且系统失败仍报告并断开；测试不注入OS事件。
- 显式综合诊断两次因焦点未取得而取消；增加等待后综合诊断的鼠标点击未被自有按钮接收。本轮没有将其描述为通过或据此改动已工作的生产鼠标路径。新增独立键盘诊断在自己的NSSecureTextField中实测：Windows协议键码经正式InputInjector转换，再通过定向Quartz发送，字母、Shift、方向键、退格、Tab、Shift-Tab与回车均通过。开启SecureEventInput，结束时清空字段并恢复指针，无用户文本或密码记录。该Aqua自有窗口结果不代替真实冷启动LoginWindow。
- 56项Swift、8组C++测试及真实主机回环全部通过；HID桥C源Wall/Wextra/Werror检查与Mac签名release构建通过。Windows严格GUI交叉构建通过，GUI SHA-256为`5bc0997cb60868d17497a5e199f3969ec486a6e8a08b3fa5d93ff8d8128b0874`；Mac最终Host SHA-256为`39cbeb38b78c92f24536b6edd98941dd23a7ed7890c2b24cabdbbf36358e81a4`。
- 两端ZIP完整性与版本/GUI检查通过；dist仅保留0.8.6 App、Windows运行目录、单EXE和两个ZIP共5项，旧包已清理。真实冷启动键盘送达与ROG捕获恢复仍待双机复测，本轮不自动注销或重启用户机器。
- 原生管理员安装已完成，发布App、`/Applications`与root所有系统副本均为0.8.6/build26，三份Host SHA-256一致且deep/strict签名通过。关闭临时测试主机后，正式`gui/501/dev.thunderdisplay.desktop`任务启动PID3412，以UID501运行系统副本并实际监听`10.0.0.2:47990`；原生主App登录项与系统守护进程状态正常。此结果证明当前桌面恢复及系统副本更新，不替代下一次冷启动键盘验收。
## 0.8.7 重写登录前键鼠（2026-10-06）

- 用户确认冷启动只有画面，键鼠均无效。真实日志已收到 Windows 键盘包（14:26:16），失败点在系统事件送达。旧代码把无回执的 flagsChanged 调用当作“verified input”；这一判断不成立，已删除。
- 旧安装的主可执行文件缺少 `__CGPreLoginApp/__cgpreloginapp` Mach-O 图形标记。新版加入该标记，并在生产入口验证主映像；不改变 TCC、系统安全策略或受限 entitlement。参考 [Chromium 登录前主机标记](https://chromium.googlesource.com/chromium/src/+/master/remoting/host/remoting_me2me_host.cc) 与 [Chromium macOS 输入实现](https://raw.githubusercontent.com/chromium/chromium/main/remoting/host/input_injector_mac.cc)。这是已确认的架构差异，不能单凭它宣称新版冷启动送达已经通过。
- 删除生产登录前 HID 参数客户端初始化、失败重试、PID 定向键盘路径与探测按键。鼠标使用当前图形会话的公开 CGPostMouseEvent，键盘/滚轮使用 CGSession 事件入口，事件源为空，全部在 AppKit 主队列创建和发送。保留 root/LoginWindow bootstrap、每包控制台会话和事件发送授权守卫；系统发现 daemon 仍为 nobody。原生 HID 只用于显式 Aqua 诊断。
- 每连接输入队列有上限，仅合并相邻鼠标移动，不跨点击合并或丢弃按键释放；每次处理最多32项。断连取消未送达事件并释放已按下的键鼠；发送前再次确认仍为登录前会话，避免旧队列落到新桌面。保留最后一帧交接、无文字覆盖和原重连预算。
- 61项 Swift、8组 C++、真实 TCP/UDP 合成视频回环通过。C 桥严格语法检查、Windows 严格 GUI 交叉构建及两端 ZIP 检查通过。XCTest 由未标记的系统 XCTestRunner 加载，验证该进程拒绝初始化；打包器检查实际 Host Mach-O 节，实际应用 --login-window-check 返回 marker=true。
- 签名 release 自有窗口实际收到鼠标定位/点击、协议物理键码、Shift、方向键、退格、Tab、Shift-Tab、回车和 SecureEventInput 下的输入；内容匹配断言通过，没有记录用户输入或密码。此证据来自当前用户 Aqua，会话不同，不能代替冷启动 LoginWindow 验收。
- Mac0.8.7 build27 Host SHA256：`dce0e9d2e49460142ef083f2076cf75ab6b5e76f395bdfa448c8d0823ef3c937`。Windows GUI SHA256：`9d5b68ef9d90b733d341e2a926f1987960d5d7a67adee491c62243bfab76feb2`。dist只保留最新版本。
- 已通过原生管理员流程安装更新。发布、/Applications 和 root 所有系统应用三者均为0.8.7/build27，Host哈希相同且 deep/strict 签名通过；系统副本 marker=true。LoginWindow全局组件为LoginWindow/Interactive；发现daemon为nobody。当前 discovery PID2831 和桌面主机 PID2838 均 running，桌面实际监听10.0.0.2:47990 TCP/UDP。不存在另一个旧主机实例。安装时移除旧会话心跳，避免把旧检查当新版结果。
- 待用户保存工作后重启，停在macOS登录界面验证鼠标选择用户、聚焦、Tab/回车、物理键盘、修饰键、密码输入以及进入桌面时的交接。本轮未自动注销或重启，FileVault开机解锁仍不支持。

## 0.8.8 登录至桌面交接提速（2026-10-06）

- 先按用户要求提交已交付0.8.7：主线 `63487b0`，标签 `v0.8.7`。提交快照明确排除0.8.8的网络重试、会话轮询、版本号改动。继续开发分支为 `fix/login-handover-speed`；未合并回稳定主线。0.8.7两端校验ZIP另存于忽略的 `build/checkpoint-0.8.7/artifacts/`，dist仍仅最新版本。
- 实际日志：14:49:12.187 登录前主机释放端口，14:49:12.263 launchd报告 `pending spawn, domain in on-demand-only mode: dev.thunderdisplay.desktop`，14:49:21.927桌面App才启动，相隔9.740秒。原Windows快速阶段仅前8次，约2秒，随后每次4秒；所以反复发现和重试间隔之外，还存在桌面任务启动排队。
- 仅在收到会话ID匹配且已经显示真实画面的交接通知后，启动15秒快速重试窗口，每250毫秒一次；暂时直连上次实际显示画面的Mac地址，TCP连接阶段上限500毫秒。窗口结束恢复常规发现/重试；普通首次连接不使用这一短超时。仍最多10分钟/150次，重复交接通知不续期、不清次数，真实新帧成功后才结束本轮。
- 登录前组件额外每100毫秒检查是否完成登录，普通状态心跳仍每秒，避免高频重写状态文件。桌面启动前15秒临时每100毫秒复查，首次成功、用户停止或睡眠即结束；争用端口后100毫秒重试，常规失败仍3秒退避。
- 登录前root组件以公开launchctl请求已有桌面任务启动，不加 `-k`、不杀已有主机、不将root应用直接运行在用户会话。目标只可能是root所有配置中的desktopUID且等于当前consoleUID，固定为该用户的 `dev.thunderdisplay.desktop`；缺配置、系统用户和其他用户都不请求。GUI域未就绪时最多2秒重试，超时仅终止自身launchctl子进程，保留普通登录项回退。配置、TCC、输入授权和守护进程身份范围没有扩大。
- 仍先发送会话通知并有界等待回执，停止旧输入/视频，监听端口先释放再清理编码；新桌面实际显示首帧之前保持冻结画面，禁用远程输入。没有新增文字覆盖，不延长30秒冻结期限，不改变桌面分辨率。
- 62项Swift、8组C++通过，覆盖启动目标用户范围、整个15秒重试窗口、重复通知不能续期、总截止/次数不能重置、手动停止保留；真实HostServer合成视频TCP/UDP回环验证交接后的输入禁止、回执及编码退出阻塞时端口立即接管。Windows严格GUI交叉构建、Mac签名release、GUI/版本/Mach-O节/ZIP校验通过。
- Mac0.8.8 build28 Host SHA256：`8a39946ae15998e27c190bbb5eb61175a7e07b3f327399d4585a9dafdb28232a`。Windows GUI SHA256：`c39ec5989ae9b30d25f54f4f709c38502db023fd814defd49aaf472c4fbf5928`。
- 原生管理员更新安装完成。发布、/Applications 与系统应用三份版本/build/Host哈希一致，deep/strict签名通过。原0.8.7用户主机PID866身份核对后正常退出，公开kickstart启动正式桌面任务，PID3124以UID501运行系统副本，实际监听10.0.0.2:47990 TCP和UDP；发现daemon PID3060 running。没有遗留旧实例。此验证属于当前用户Aqua启动，未将其当成冷启动或on-demand-only阶段的实测。
- 待实际双机验证密码通过至第一帧桌面的耗时、未登录键鼠保持正常和冻结画面连续性。上述9.740秒是旧版观察，新版本未声称已实测总耗时；macOS建立图形会话和硬件捕获/编码仍需要时间。本轮没有代用户注销/重启。

## 2026-10-06：0.8.9交接黑屏与键鼠延迟

用户反馈0.8.8进入桌面黑屏、打字和鼠标延迟明显。定位到冻结标志拦截了Ready消息，新Host不能送帧，冻结到期后黑屏；此外TCP写和解码/Present共用线程、Mac转色/编码提交和输入收包共用队列，鼠标坐标还等待GPU锁。

0.8.9修复交接冻结时Ready/Ping/关键帧消息被拦截，新桌面能正常开始送帧。冻结只暂停输入，快照立即重绘；普通断线清除旧画面，真实会话交接才无文字保留最后一帧，最多30秒。Windows新增连接独立TCP写线程，键鼠优先于剪贴板分片；鼠标换算使用独立几何快照，不等待显卡锁；解码批次只显示最新输出。Mac颜色转换和VT提交使用独立的单帧工作队列，不阻塞收包。不改变码率、色彩或像素尺寸。需要更新两端与Mac开机组件；真实ROG延迟与画面连续性仍需双机复测。

62项Swift、9组C++通过。新增生产ControlOutbox回归覆盖冻结仍可Ready、键鼠顺序、相邻移动合并、输入优先级、交接清空/代次失效、溢出和停止；阻塞模拟视频线程期间，独立写线程仍被输入唤醒。真实HostServer合成TCP/UDP回环通过，包括无后续输入时提前通知、ACK、旧输入禁止、编码停止阻塞时端口接管。Mac签名Release与合成HEVC/H.264/Main10+sRGB硬件输出检查通过，Windows严格GUI交叉构建通过；最新解码合批改动构建校验随后记录。本轮未替用户注销或重启。ROG的D3D画面连续性和真实端到端延迟仍待用户复测，未把队列测试当成实机验收。

- 最终解码合批改动后的严格Windows GUI构建、包版本/GUI子系统/ZIP校验通过。Mac0.8.9 build29 Host SHA256：`72e394bd2a912c253204ec610c508f71fa1d9f27bca8ea0bf67e9f853a914c45`；Windows GUI SHA256：`9eb111bd35f091b3c964b9841aec2e267990ab15206ac9c6a67a44c422831036`。
- 原生管理员安装完成；发布、/Applications和系统App三份0.8.9 build29/Host哈希一致，deep/strict签名通过。核对UID501/绝对路径后正常退出旧主机PID857，以已注册桌面任务启动新版PID2909，执行系统App，实际监听10.0.0.2:47990 TCP与UDP；发现daemon PID2830运行。当前仅有一个GUI主机。dist仅保留0.8.9；0.8.7签名包保存在忽略的build/checkpoint-0.8.7，main/v0.8.7仍为63487b0。本轮未注销/重启用户；登录冻结与真实ROG输入延迟等待复测。

## 2026-10-06: Windows 0.8.10 transient blank-frame recovery

The recent Mac log had no socket disconnect or capture failure during the reported sub-second black flashes. This identifies risky client clearing paths, not the proven cause of the ROG flashes.

Visible pixels are independent of live-frame freshness. WM_PAINT redraws a saved image, resize and precision changes immediately refill the new swap-chain buffer, and brief decode pauses hold pixels/request IDR without opening setup. Saved-image redraw does not update presentation freshness or enable input. The existing 12-second stall timeout, actual socket disconnect and bounded authenticated handover expiry still clear stale pixels. Release-all input remains allowed while other input is paused.

Local Windows diagnostics use an asynchronous bounded queue and two rotating logs of approximately 2 MiB. Events contain presentation gaps, slow Present, occlusion, resize/precision changes, decode counters, clear/hold reasons and HRESULT/device removal codes; no input/clipboard/pairing contents. The settings diagnostics show the file path.

Nine native C++ regression groups passed, including stale-but-visible pixels, input gating, recovery deadlines, resumed-frame notifications and release while paused. Windows strict GUI x64 cross-build passed. Native ROG GPU/black-flash verification remains pending; no Mac code or startup component changes are required.


## 2026-10-07：0.8.19 Mac 多屏鼠标

- 按用户确认的规则：TD 前台无边框全屏、输入开启且画面可用时才接管 Windows 鼠标，以相对位移在 Mac 全局显示器布局中移动。窗口模式保留当前串流绝对坐标映射，鼠标离开窗口/有效画面时停止发送，拖动释放远端按钮。Windows 屏幕数量和排列不参与 Mac 跨屏坐标；Mac 实体屏和虚拟屏统一使用活动显示器的逻辑矩形。
- 覆盖负原点、上下/左右排列、不同尺寸、外缘滑动、空白区域、HiDPI 位移不重复放大、跨屏点击/拖动不跳回主屏、释放输入后重新读取实际指针位置。全屏、焦点、捕获或实时画面条件失效时撤销 Raw Input 和本地指针限制，恢复原本 Windows 指针位置。短暂视频中断与手动释放分开处理，画面恢复可重新接管。
- query11 增加会话绑定的 MouseMode=20 和 Input kind6/7；原 Capabilities/Hello/Welcome/视频形状不变，旧查询拒绝相对输入。多屏会话使用 SCK 的实际视频指针，画面仍保持选定屏幕，移到另一 Mac 屏幕后指针不出现在 TD 视频中。修正显示器变化时两次读取同一个最新 bounds 的比较，改用启动快照；热插拔、屏幕移动、逻辑/渲染尺寸变化重建连接，未变的几何保持连接。原生 recovery-check 同时检查未变布局与合成过期快照，不修改实际显示器设置。
- 77 项 Swift 测试、12 组 C++ 回归通过；真实 HostServer 回环测试使用合成画面/输入，验证 query11 的视频指针、MouseMode 会话ID、相对移动/点击/释放与窗口绝对回退顺序、query10 拒绝未经协商的相对输入，同时保留旧指针/剪贴板/心跳/登录交接验证。Mac 签名 Release 与 Windows x64 GUI 严格交叉构建通过，两端 0.8.19 安装包展开后实际文件字节校验通过。
- 当前只读诊断检测到一块虚拟 16:10 显示器（逻辑2048×1280、渲染4096×2560）；构建目录 App 的屏幕录制已授权，事件发送未授权，因此未启动需要两项权限的原生恢复验证，未操作系统权限。双机双屏的跨屏手感、窗口返回 Windows、实际拖动、虚拟屏热插拔与失焦/断线释放仍需实机验收。本轮只生成更新包。
- 0.8.19 build40 Mac Host SHA256：de5577d257d7a32d733d0bca5b9f1d85055074b83e5ffda439eb7d26dcc5ffbe。Windows Client SHA256：619a3f12b522f8adc9b161ca39f7547e19de974bafedd2d91e0be00d7c92c1e5。旧 0.8.18 分发文件保存在忽略的 build/checkpoint-0.8.18/artifacts，dist 为当前 0.8.19。

## 2026-10-07：0.8.20 码率滑块重写

- Windows 滑块直接处理 WM_PAINT / WM_PRINT / WM_PRINTCLIENT，双缓冲完整绘制轨道、选中段与圆形滑块；保留原生 Trackbar 键盘、焦点与输入几何，点击轨道可跳转并继续拖动。移除原分段 NM_CUSTOMDRAW 流程。
- 右上角可编辑整数 Mbps，留空 / 0 表示自动。网络刷新不覆盖正在输入的文字；有效输入同步滑块，回车 / 失焦确认，Ctrl+A 全选，Esc 恢复编辑前值；错误码率阻止连接提交。手填保持 1 Mbps 精度。
- 拖动吸附间隔随码率递增，使用吸附 / 释放双阈值减少边界跳动；端点仍为自动和链路最大值。方向键直接移到下一个整值，避免小步移动被吸附吞掉。
- 固定 0.8.20 源码通过码率策略（单调性、边界、吸附与释放、键盘、手填与溢出）、布局 / 滚动 / DPI、快捷键回归；Windows x64 GUI 严格编译通过，Mac build41 签名构建通过。两端安装包实际载荷和校验值检查通过。
- 保留 0.8.19 多屏输入协议与代码；本轮未操作真实键鼠、未安装、未重启。Windows 外观、合成窗口实际绘制、拖动 / 键盘 / 手填交互仍需 ROG 验收。dist 仅保留 0.8.20 两端安装包及说明和校验值。

## 2026-10-07：0.8.21 可选无压缩与独立多屏指针

- 用户要求保持4096×2560和10位，并选择新增可选无压缩模式。新增RawP010独立TCP视频，保留原HEVC/Main10/H.264；仅采集选定屏幕。Raw跳过压缩会话和Windows解码器，仍保留RGB→P010颜色转换、10位SDR 4:2:0、GPU上传及显示。Windows设置与CLI可选Raw，保存偏好，强制源像素/10位且最高60fps，不自动降低尺寸/精度或回退编码。
- query12独立同步实际Mac位置及原生指针形状，多屏全屏移动不等待视频；query13协商Raw及会话认证TCP端口。旧端保持原协议及视频指针。窗口退出TD、全屏跨Mac屏、失焦/断线释放规则保持不变。
- 82项Swift、14组原生C++回归通过；覆盖P010行填充去除、全部有效10位保留及低6位归零、带宽计算、握手/帧头/会话严格校验、位置正反映射和裁剪。真实HostServer与生产RawVideoSender合成回环验证错误会话拒绝、完整帧、独立指针、视频接收阻塞时控制Ping/Pong及旧会话交接；无真实屏幕/输入操作。HEVC/H.264/Main10+sRGB合成硬件回归通过。
- 本机生产RawVideoSender回环以4096×2560合成P010持续3秒：181完整帧，60.0fps、15.09Gbps；测的是软件传输容量，不是实际雷电吞吐。生产采集路径在同一移动测试窗口7秒对比：HEVC10约56.0fps、入队至编码回调平均23.3ms；Raw约57.5fps、入队至像素就绪平均4.1ms（RGB→P010约3.0ms、复制约1.1ms）。动画/采集本身约57–58fps，不能据此声称两端稳定60fps。采集测试临时暂停既有主机进程，结束后恢复，未改变显示器配置或注入输入。
- Windows严格x64 GUI交叉构建用于编译校验；实际ROG的P010纹理上传、透明指针覆盖层、雷电吞吐与端到端60fps仍待双机验收。
- 最终Mac 0.8.21 build42签名Release、Windows严格x64 GUI构建与两端安装包展开载荷校验通过；原编码合成检查复测通过。Mac Host SHA256：`04a61a0a99d5e25bb5e767c41c1e490c2a29414091cf2ea2e81818c52d9f9a4e`；Windows Client SHA256：`472580cbe210e2fde1a8a0767c24f054a4930d98390d34ec735402dbd5e1a72b`。原0.8.20分发保存在忽略的build/checkpoint-0.8.20/artifacts；本轮未安装或重启生产应用。

## 2026-10-07：0.8.22 实机无压缩掉帧修正

- 用户反馈掉帧后读取正在运行的0.8.21 build42日志与连接指标：13:27–13:28，Mac准备/捕获约43–46fps，发送约19.7–19.9fps、4.9–5.0Gbps，每帧发送约50.5ms；Windows还原上传约1.5ms、arrival→Present约50.8ms。nettop确认视频RTT约1.7ms、对端接收窗口严格1,048,576字节；发送缓冲持续满1MiB。雷电端口连接为40Gbps能力，不能等同实际IP吞吐。诊断未改全局网络、MTU、显示器或生产应用；tcpdump因缺少BPF权限未执行，改用nettop只读指标。
- 移除Mac SO_SNDBUF与Windows SO_RCVBUF固定1MiB设置，保留系统自动调节。query14新增RawPacked10=16，旧query13原P010仍可用。移除每个16位字的六个无效位，完整保留十位值；每四个样本五字节，每行独立，偶数但非4倍数宽度的尾部填充严格验证。4096×2560载荷19,660,800字节，60fps为9.437184Gbps，减少37.5%；不创建视频压缩/解码会话。Windows预分配紧密与P010缓冲后还原上传GPU。新增窗口、RTT、重传和待发帧替换计数，避免采集“dropped=0”掩盖发送队列的帧替换。
- 83项Swift、14组C++通过。Swift生产打包与C++生产还原分别对照独立参考，包含全部0–1023值、共享golden五字节、源行填充、320/322宽度、尾填充、截断、大端Welcome/raw握手及独占掩码。C++HostCapabilities验证27/31；真实HostServer及RawVideoSender回环同时验证query13/14的逐字节载荷、会话拒绝、指针同步及发送阻塞时Ping/Pong。Mac签名Release、Windows严格x64构建和HEVC/H.264/Main10+sRGB合成回归通过。
- 新版生产Swift打包4096×2560的20帧合成像素平均4.440ms/最大6.006ms；生产C++还原本机arm64平均2.652ms/最大5.568ms，不代表Windows CPU测量。生产TCP软件回环181完整帧、约59.9fps/9.42Gbps，无屏幕采集或输入。新两端尚未安装，真实雷电吞吐、Windows还原和端到端帧率仍待验证，未把回环当成实机60fps结果。
- 0.8.22 build43 Mac Host SHA256：`2f6ff361a5e91469bf7c2e55c65a1a8debbe297803b851918c1163fbd83d192a`；Windows Client SHA256：`85d8f6c0fbefb58c14c7db37c080e51076d2c188de485529387d4ecfb807f0a8`。两端安装包展开后载荷与构建字节一致；旧0.8.21分发保存在build/checkpoint-0.8.21/artifacts。生产0.8.21未重启或暂停。

## 2026-10-07：0.8.23 持续掉帧与收发并行修正

- 已安装0.8.22/build43，两端协商rawPacked10。13:47实机发送约25–28fps，接收窗口0.5–1MiB；恢复诊断后16:42–16:45发送约30–31fps、4.8–4.9Gbps，单帧发送约29–31ms。此时Windows接收窗口已自动增长为16,776,960字节，因此没有将早先的小接收窗口当成持续瓶颈，也没有新增Windows固定接收缓冲。Mac真实视频RTT约5–7ms、拥塞窗口约10.8MB，发送队列持续顶到4,194,304字节；只读sysctl确认本机自动发送增长上限4MiB、单连接允许上限8MiB。Windows还原/上传约4.8ms，arrival→Present约33ms，渲染约0.15ms。
- Mac只为已认证的原始视频连接申请最高8MiB SO_SNDBUF，小帧不超过一帧，已有更大容量不降低；读取并记录实际容量和设置错误，继续保持发送中一帧与最新待发一帧的上限。不修改全局sysctl、MTU、网卡或显示器。Windows保留接收窗口自动调节。
- Windows接收移到独立线程，与原MTA像素还原/上传并行。每会话预分配三个载荷缓冲：接收中、处理中的帧互不覆盖，只替换最新待处理的完整帧；处理等待超过50ms丢弃。停止唤醒队列并shutdown接收，再join线程和关闭描述符；像素处理异常也回收接收线程。解包完整保留十位值，旧P010与新紧密协议不变，没有自动降分辨率/色深或改用视频压缩。4096×2560 packed10三个缓冲与P010还原缓冲共约86.25MiB。
- 新增Windows真实队列等待、待处理替换诊断；Mac记录窗口缩放、实际发送容量以及采集complete/idle/other回调，便于区分静止画面的空闲与采集掉帧。
- 83项Swift、15组C++回归通过；新增生产RawVideoInbox检查验证处理帧被持有期间1000次接收发布不阻塞、三个缓冲地址保持复用、仅最新帧被取出、并发5000帧不破坏正在使用的像素/时间戳，以及停止唤醒和在途接收取消。新增队列ThreadSanitizer检查通过。Mac签名Release、Windows严格x64 GUI构建及原HEVC/H.264/Main10+sRGB合成编码回归通过。真实HostServer/RawVideoSender合成回环验证query13/14载荷、会话认证、阻塞视频期间控制响应、旧编码协议与登录交接。
- 新Mac生产RawVideoSender回环实际SO_SNDBUF=8,388,608字节、设置错误0；4096×2560 packed10持续3秒接收181完整合成帧，60.0fps、9.44Gbps；20帧生产打包平均3.864ms、最大7.281ms。未读取真实画面或注入输入，没有暂停/重启生产应用。该回环确认软件路径与容量设置，不代表Windows原生GPU或实际雷电端到端60fps；双机安装新版后仍需实测。
- 原0.8.22分发已备份至忽略的build/checkpoint-0.8.22/artifacts。0.8.23/build44 Mac Host SHA256：`d5d3dc64d979ef7d391e1648fb53573ae8469751f1f0b8129a6e1c1c677069d7`；最终Windows Client SHA256：`257dabaac2949bf95c90fce285efce9d5f0c8649e4e7a3d01dabce373865334b`。两端安装包均展开后核对载荷与构建字节一致，版本0.8.23、Mac build44及Windows安装说明一致；dist校验清单已更新。本轮未安装或重启生产应用。

## 2026-10-07：0.8.23 实机复测及 0.8.24 Mac 拖入废纸篓卸载

- 两份安装的Mac应用均为0.8.23/build44，生产PID31823；新的8MiB发送容量、scale8和Windows非零接收→工作队列计时生效。17:02:25–17:02:35，Mac准备42.7–46.4fps，采集完整回调相同、含idle总回调约50.6–52.6fps；rawPacked10仅30.9–31.3fps、4.9Gbps，发送约30–31ms，每5秒待发送帧替换57–78次。Windows队列等待约26–30µs，还原/上传约4.9ms、渲染约0.14ms、arrival→Present约37ms。用户确认当时拖动窗口或滚动内容，不能把这些替换帧当成静止画面空闲。
- 此轮接收窗口起初1,048,320字节、scale8，17:04增长到2,096,896字节、真实RTT约3.44–3.53ms，实测约4.9Gbps。Mac bridge0/en2均MTU1500、TSO已启用；Host CPU约29.5%。三秒只读线程采样显示raw-video线程主要在poll等待发送就绪，其次在内核send，未发现第二屏编码。8MiB申请未突破吞吐；不能据此断定硬件标称带宽、Windows窗口或驱动中的具体原因。未再次盲改缓冲或全局网络。
- 准备scripts/windows-link-check.ps1与dist/ThunderDisplay-Link-Check.cmd，仅读取TD对应连接的网卡驱动、MTU、RSS/RSC/LSO、TCP配置、网卡计数、逻辑CPU和TD性能日志。生成桌面带时间戳报告并打开，不改网卡、注册表、TCP全局设置，不采集屏幕/键鼠/剪贴板/配对码。CMD内嵌UTF-16源与可审阅源文件逐字核对；需Windows本机执行并提供报告，尚未声称Windows端检查通过。
- 0.8.24应用户要求移除状态栏与主菜单卸载项、openUninstaller方法、Uninstaller.applescript和独立卸载App的构建。新PKG只包含ThunderDisplayHost.app，升级按既有bundle标识清理旧TD卸载App。菜单标题已从实际Host二进制移除。视频路径沿用0.8.23，不将本包描述成掉帧修复。
- 已安装root拥有的固定路径cleanup脚本，由短暂的10秒周期后台任务检查/Applications中的主应用；连续离开至少30秒后清理已知TD启动任务、helper和系统副本，尝试以目标普通用户取消原生登录项，然后退出TD并最后移除清理任务自身。nobody发现服务权限保持；不删除废纸篓里的主应用，不删除用户连接偏好或权限授权。恢复应用取消待清理状态，安装器在替换应用前暂停检查、完成后恢复，失败时恢复原检查；从废纸篓恢复后重新启用开机组件也会恢复检查任务。
- 隔离的生产脚本测试将所有系统路径改到临时目录，并替换全部服务控制、进程终止、用户切换及包登记操作，绝不调用真实卸载。9组场景验证正常应用/恢复取消、首次缺失等待、宽限期、到期仅清理TD且保留Trash及无关文件、UID/可写目录/符号链接拒绝、时钟倒退重新等待及替换应用保护；到期路径验证先按用户处理原生登录项，再退桌面服务，最后移除清理任务。最终脚本重测通过。83项Swift、15组C++和两端构建通过，shell/plist校验及PKG/EXE实际载荷展开通过。
- 最终0.8.24/build45 Host SHA256：`a0fa23bbcdb2357cc095118e6490e6f0b9bcf8b39beca6ed83b8d76c0687aa4b`；Windows Client SHA256：`e3b45f8181fcef9cfdfa9fb432f60c297d05366e0c07260e84e63e2dc81a2440`。旧0.8.23分发保存在build/checkpoint-0.8.23/artifacts。真实拖入废纸篓的系统卸载尚未执行；本轮未安装、暂停、重启或卸载当前生产应用，Windows报告仍待提供。

## 2026-10-07：Windows 报告与雷电 MTU 核查

- 用户提供 `dist/ThunderDisplay-Link-Report-20261007-173409.txt`。Windows 11 build26200，17:33:15记录client.start version=0.8.24；Mac当前应用也确认为0.8.24，系统副本PID35126。USB4(TM) P2P Network Adapter使用Microsoft usb4p2pnetadapter.sys 10.0.26100.9278（2026-08-24），链路标称20Gbps。TCP接收自动调节normal，启发式disabled；IPv4/IPv6 RSC均enabled且operational，无失败原因；LSOv2及校验卸载开启。RSS查询无条目，不能据此假设RSS已关闭或需要强制启用。
- Windows IPv4/IPv6 MTU均62000；累计及两秒测量接收错误/丢弃均0，短测2.09Gbps对应非持续运动，不能作为吞吐上限。CPU快照总占用9%、最高逻辑核63%，单次采样不证明驱动达到单核上限。Windows正常完整帧还原/上传约4.7–6.2ms、队列约23–44µs、render约0.13–0.18ms、arrival→Present约36–38ms。最近日志没有接收队列替换；部分0.5秒以上frame.gap可由静止画面引起，未全部计作掉帧。
- 17:39:18–17:39:19当前Mac准备38.6fps，完整采集38.6fps，实际发送31.2fps/4.9Gbps，发送30.367ms，5秒待发送替换41次；Windows处理4.9ms、arrival→Present37.027ms。保存当前日志至build/tests/td-0.8.24-current.log。只读libproc连接元数据确认视频与控制TCP实际MSS均1460；Mac bridge0/en2 MTU1500，到Windows的克隆路由MTU1500。networksetup只读查询确认bridge0/en2均支持1280–65518。
- 已准备build/tests/td-mtu-probe.sh：只接受本机10.0.0.2/bridge0、对端10.0.0.3及原MTU1500和已知en2/en3/en4成员，短暂测试62000 MTU、9000/62000字节不分片ICMP，EXIT/INT/TERM均恢复成员/网桥/对端路由MTU1500。不持久修改网络偏好、不重启TD；sudo无缓存权限，已通过系统管理员授权请求执行。尚未把MTU差异当成已证实的帧率根因，也未声称测试通过。
- 用户完成系统授权，9000和62000字节IPv4不分片ICMP各2包均成功；恢复时先降低bridge0而成员仍62000被系统拒绝。立即修正恢复顺序为成员→网桥→路由，并再次系统授权恢复；四个接口和克隆路由均确认1500。保存原测试与恢复日志，未把第一次退出0当成恢复成功。
- 17:52:32–17:53:20做完整受控对照：同一采集显示器6短暂显示560×320动画，不激活应用、不注入输入、不保存画面；先测MTU1500，再临时62000并重启唯一TD桌面服务，Windows自动重连。libproc确认新视频连接MSS61960，真实大包参数生效。1500时采集59.2–59.3fps、发送31.3–31.6fps/4.9–5.0Gbps、每5秒替换138–140；62000时采集58.5–59.7fps、发送31.1–31.4fps/4.9Gbps、每5秒替换135–142，发送约31.8–32.1ms，Windows处理4.8ms、arrival→Present约36.9–37.1ms。没有重传，接收窗口1,048,320、拥塞窗口16,776,960。该实验没有显示MTU带来的帧率改善，不持久修改MTU，也不把大包配置发布成修复。
- 实验退出按修正顺序恢复全部接口及路由1500并再次重连TD；最终生产PID37480、状态恢复，动画进程已退出。实际实验与日志见build/tests/td-mtu-video-trial.log和td-mtu-video-trial-telemetry.log。动画证明当前采集路径能接近60fps；仍需独立的单流/多流网络测量区分整条链路与每连接限制，未提前断言硬件或驱动根因。
- 准备独立scripts/link-throughput-server.cpp与windows-link-throughput.ps1，分1/2/4连接各8秒传生成字节，Mac仅绑定10.0.0.2:43189且接收10.0.0.3；握手严格检查版本/连接数/索引/时长，约5分钟超时退出。Windows保留TCP接收自动调节，不经视频还原或GPU；报告记录实际收到的字节、时间与Gbps，并保留失败前的完整结果。不读取截图、键鼠或配对码，不更改网络/防火墙配置；本机防火墙只读查询为disabled。
- Mac严格C++17构建通过；真实1/2/4连接本机回环分别校验服务端实际发送与客户端实际收到总数一致，均明显超过本任务9.44Gbps的必要软件吞吐；错误握手被拒绝且未收到载荷。本机回环不等同Windows/雷电结果，WindowsC#接收端原生执行待用户提供报告。独立CMD的UTF16嵌入源码与可审阅PS1逐字核对，各行低于CMD长度上限；dist增加Bandwidth-Check.cmd、源文件与当前完整项目可运行的Mac服务器启动command，校验清单新增对应三项并验证原安装包/诊断文件哈希未变。没有发布新的未验证视频版本。
- 用户提供19:55:34的测速失败报告：Windows C#已编译并进入Run，但连接Mac测速端失败，未产生任何带宽测量；IPv4 MTU仍62000。当前进程核查无测速端，Mac生产Host PID923、bridge0地址10.0.0.2/MTU1500。将测速端整体等待上限从5分钟延长到30分钟（每组实际发送仍8秒），严格构建通过，并独立于交互命令启动有限时长的测速进程PID1820。lsof确认10.0.0.2:43189正在LISTEN，启动日志确认30分钟期限；Mac防火墙只读确认disabled，未修改规则或网络参数。当前Windows真实1/2/4连接结果仍待重试报告。

## 2026-10-07：独立测速 v1 接收窗口异常与 v2 修正

- 用户提供20:20:11及20:29:04两次Windows报告，1/2/4连接均仅约0.011/0.022/0.044Gbps。第二次Mac逐连接日志确认测试窗口始终65,280字节、scale8，RTT约3ms、无重传、8MiB发送缓冲持续满；服务端每组发送总数与Windows接收总数一致。测试进程/线程background标志均0，工作线程QoS为DEFAULT(21)，没有把后台限速推断当作已证实的原因。TD视频连接另行观察窗口约2,096,896字节；20:19–20:20静止期间TD约0.4–0.5Gbps，20:20:29运动时33.1fps/5.2Gbps，不能描述成低速测试全程同时保持4.9Gbps。第一版本机回环漏掉了实际Windows窗口差异，这两份报告不足以判断雷电容量或多连接可获得的视频加速。
- v2 Windows接收端仅对临时测速socket在Connect/SYN之前申请8MiB SO_RCVBUF，校验getter实际容量并写入每组报告；固定测试缓冲与TCP通告窗口不是同一指标，后者继续由Mac TCP诊断核实。此设置替代这些测试socket原本的自动接收调节，未修改TD生产接收策略、全局TCP、网卡、MTU或防火墙。接收线程改为AboveNormal，与TD相同；报告首行明确含v2，避免旧副本被误认为更新。
- v2 Mac工作线程明确为USER_INTERACTIVE，已认证测试socket改为非阻塞send与10ms POLLOUT等待，与TD发送调度一致；保留8MiB发送缓冲与逐秒窗口/拥塞/RTT/重传/优先级记录。严格C++17构建通过，1/2/4连接本机回环收发字节总数一致并验证生成模式，只有软件路径验证意义，不能作为实机雷电吞吐或60fps验收。
- 重新生成dist独立CMD和可审阅PS1，验证UTF16嵌入内容逐字一致、命令行长度及SHA256清单，原安装包和原链路报告工具保持原字节。有限30分钟的v2 Mac服务PID4271已启动，lsof确认10.0.0.2:43189 LISTEN。用户须重新复制新CMD到Windows；原生v2结果仍待提供，没有据此发布新的TD视频版本，也未把掉帧标记为修复。

## 2026-10-07：外接屏使掉帧更明显的反馈与单屏基线

- 用户补充接入Mac外接屏后掉帧明显，未接时较轻。20:39只读system_profiler与TD --diagnose均检测到唯一虚拟16:10屏，活动ID5、逻辑2048×1280、渲染4096×2560、60Hz；当前生产Host PID923仍在发送rawPacked10。代码复核确认视频只创建选中display的一个SCStream，跨屏指针读取系统指针及各屏逻辑边界，没有为另一显示器创建编码/视频流。尚未确定物理外接屏的接法和刷新率，已请求用户提供接法并接回以做相同条件对照。
- 临时非激活动画在选中ID5屏显示20秒，1200个60Hz计时tick，到期正常关闭；没有注入输入、切换源屏、修改分辨率/色深、启动第二条采集或重启TD。20:42:46–20:43:07前后显示器元数据一致，仍为单虚拟屏；剔除首尾混合统计后，3组5秒日志中准备/完整采集37.3–37.6fps、发送32.0–32.3fps，发送平均28.9ms，RGB转换约1.88ms、P010打包约4.48ms、采集年龄约0.77ms。Windows还原/上传约13.96ms、arrival→Present约44.91ms，此时比17:52对照中的约4.8ms还原/上传更慢；没有据此猜测Windows驱动或GPU根因。
- 这轮单屏仍存在发送瓶颈，外接屏可能额外影响采集、转换或共享连接，但不能从单屏日志推断其影响。双屏相同动画对照及v2原生Windows测速均仍待条件就绪；性能记录保存于忽略的build/tests/td-display-motion-single*.log/json。没有盲改SCK队列、刷新率、全局网络或发布视频更新。

## 2026-10-07：0.8.25 精确区域更新与源刷新采集

- 用户当前没有外接屏，要求在已有设备上优先稳定60或更高帧率。随后收到20:48:10的v2 Windows原生报告：1/2/4连接分别4061969156/4309895256/4261934892字节，8.017/8.034/8.064秒，实际4.053/4.292/4.228Gbps；SO_RCVBUF getter均8388608。Mac各组发送字节数逐一一致，通告窗口8MiB，确认v1的64KiB差异已消除。TD在测试时保持连接，不能把此结果称为无其他流量的硬件极限；多个连接没有明显增加总吞吐，未发布并行连接或MTU调整。完整4096×2560 packed10在60fps仍需9.437184Gbps。
- query15新增独占RawDelta10=32，旧query13/14保持原字节。Mac发送线程对完整packed10图像逐字节比较128×64亮度瓦片及对应UV，基图像只取上一条成功发送的快照；等待时被替换的采集图像不进入基图像。首次或变化片总大小超过完整帧时发送完整图像，否则只传变化片，所有十位数值、尺寸、sRGB SDR 4:2:0保持不变，没有视频压缩/解码会话或近似阈值。
- Windows接收线程按TCP顺序严格校验并应用全部变化片，再把完整独立快照复制到原三个复用缓冲。显示或处理线程丢弃快照不跳过区域更新；所有基帧、尺寸、顺序/重复、截断、尾填充和额外字节均验证。额外接收图像与载荷缓冲在4096×2560共约37.5MiB。新增x64 SSSE3能力检测和批量十位还原，旧CPU标量回退，arm64 NEON对应验证；相同golden、全部0–1023、320/322宽度及尾行对照通过。本机arm64软件还原40个4096×2560合成帧平均标量2.683ms、NEON1.714ms，完整P010结果一致；该数字不是WindowsSSSE3或GPU实测，Rosetta不可用，Windows路径只做严格编译和原生验收待办。
- 做受控源采集对照，临时暂停且最终恢复生产PID923；独立55秒恢复保护、try/finally恢复和动画自动关闭均生效，没有更改系统显示模式或全局网络。40秒动画2398次实际draw，前后仍唯一4096×2560/60Hz虚拟屏ID5。7秒生产采集/颜色/打包/精确区域诊断：queue3与queue5的1/60间隔分别56.4/56.2fps；minimumFrameInterval=0分别59.9/60.0fps，数据0.300/0.309Gbps、比较/复制约2.48/2.46ms。没有采用无收益的队列扩容，生产仍queue3。参考[Apple minimumFrameInterval说明](https://developer.apple.com/documentation/ScreenCaptureKit/SCStreamConfiguration/minimumFrameInterval)，只在新RawDelta10且源Hz已知、请求达到源Hz时跟随源更新；低请求或未知源率仍保留1/fps，避免高刷源把60fps请求无限放大。
- 新模式取消客户端60fps硬上限，按两端当前显示模式协商，协议仍1–240fps；未自动切换当前60Hz源。tests/raw_update_transport_check.py使用真实Swift RawVideoSender和真实C++ RawVideoUpdates，固定回环4Gbps接收节奏、dense启动制造待发送替换，再测试稀疏区域变化。最终60fps目标收到217完整帧，其中4完整更新，稳定段59.9984fps；120fps目标439帧、12完整更新，119.999fps。逐帧完整图像与独立合成参考一致，测试无截图/键鼠/真实网络数据；不是雷电或WindowsD3D端到端60/120fps的证明。
- 84项Swift、16组C++和9组隔离卸载回归通过；真实HostServer TCP/UDP合成检查新增query15完整/零片更新、会话认证拒绝和控制响应，同时query13/14及原HEVC、鼠标和登录交接验证保留。Mac签名Release、Windows严格x64 GUI构建通过；两端安装包展开后核对实际应用载荷、版本、说明及既有卸载/升级脚本，发布0.8.25/build46。旧0.8.24分发保存在build/checkpoint-0.8.24/artifacts。Host SHA256为412a14f308b8f9d2aa32178120bbfb33b9add50b49cda71c1426a94860efc006；Windows Client为be003dc9605583b1c333349262a2af9a695b096c9680689da549cc12f0a0bd41。dist校验清单重建并核对所有包及诊断工具。
- 当前生产应用仍0.8.24，未自动安装新包；两端安装后的实际滚动/拖动、Windows像素还原与GPU显示、持续全屏变化及外接屏影响仍待验收。不能声称已稳定端到端60fps；全屏每帧全变在现有吞吐下仍有带宽限制。


## 2026-10-07: 0.8.26 Windows-to-Mac debug switch

The Windows Connection diagnostics checkbox defaults off, persists independently in HKCU and can toggle an active connection. Capability query 16 / message 23 adds session-bound start/stop acknowledgments and new performance log batches. The client shares only new TD performance/display entries, bounded to 64 lines and one 4000-byte batch per second. A single replaceable diagnostic slot in the control outbox gives input/handshake priority, and handover clears old diagnostics. The Mac uses a bounded utility queue, unified `windows-client` logging, and private two-file rotation under ~/Library/Logs/ThunderDisplay.

Actual receive, uploaded/decoded and successful live Present rates are computed from counters and measured elapsed time; negotiated target FPS is a separate field. Busy drops, pending replacement, intervals over two target periods and maximum Present interval are recorded. Payload receive and exact reconstruction/copy timings are measured separately. Optional DXGI frame statistics use QPC elapsed time and have an unavailable/disjoint/reset/stale state; they are not equated with Present call counts or guaranteed reliable in multiple-display scenarios. Static source/hidden-window pauses are not automatically called lost frames. No synchronous DwmFlush or new media/input-thread disk I/O was added.

Validation completed before packaging: 87 Swift tests, 17 portable C++ groups, 9 isolated trash cleanup scenarios, strict Windows x64 GUI cross-build, signed Mac Release build. The production HostServer / RawVideoSender socket harness passed v9–16 and legacy scenarios. The new v16 scenario verified logs reaching the Mac file, start/stop acknowledgment, batches ignored after stop, 0600 log permissions, 2 MiB rotation, and responsive ping while raw video is deliberately not drained. Tests use synthetic frames and temporary paths; no production screenshot, input event or service modification occurred.

Live 0.8.25 evidence before this change (21:23:07–21:24:12, build/tests/td-current-fps-check.log): Mac capture/send 59.8–60.1 fps, sender pending replacement/stale discard zero throughout that span. Windows aggregate feedback reported zero replaced pictures, but did not include a measured Present count; frame arrival-to-Present maximum spiked to about 75–87 ms despite average windows near 5–7 ms later in the sample. Earlier source-idle windows ranged 9–44 fps and one window had three sender pending replacements. Therefore the existing evidence confirms an active period of approximately 60 fps transmission, not a blanket guarantee of perfectly paced Windows panel output. The new switch is intended to establish that distinction on the next real Windows run.

0.8.26 does not change video precision, resolution, exact-update video data, capture display choice, uninstall behavior or multi-display pointer policy. Native Windows UI/GPU execution and actual returned 0.8.26 frame statistics remain to be verified after installing both packages and enabling the switch. The local socket/portable tests do not substitute for that hardware check.

Final 0.8.26 delivery checks: both installer payloads were expanded and matched to the built apps/client; embedded versions are 0.8.26 / Mac build 47. Windows START-HERE instructions match the source exactly. The Mac persistent-identity signature verifies deep/strict outside the sandbox (the restricted trust lookup alone reports CSSMERR_TP_NOT_TRUSTED). All dist file SHA256 sums were regenerated and checked. No production application was installed or restarted.

## 2026-10-07: live 0.8.26 and the 240 Hz capture discrepancy

After the user installed both packages and enabled Windows debug sharing, the Mac received real Windows performance batches. Across 120 consecutive Windows windows, 22:08:07.519–22:10:07.255 (120.75 measured seconds), receive averaged 60.009 fps and successful live Present averaged 59.935 fps. Available DXGI statistics averaged 59.936 fps. One window fell to 55.41 Present fps; the maximum Present interval was 65.567 ms, with nine receiver pending replacements and zero busy drops over the span. Payload receive maxima of 44–51 ms coincided with replacement bursts, but include receiver thread scheduling time and do not by themselves prove a network fault. The driver reports a 20 Gbps link; selected content used about 0.283 Gbps in this sample. Mac and Windows clock values were not subtracted to claim end-to-end latency.

The user clarified that both displays support 240 Hz. The active connection requests 240 fps, and the sole active Mac display is the 4096×2560, 2048×1280 logical, non-mirrored virtual display at 240 Hz. CVDisplayLink measured 239.994 callbacks/s. Production rawDelta10 follows source updates with zero minimum frame interval; its actual complete ScreenCaptureKit callbacks and sent frames remain approximately 60 fps. Display mode and display-clock callbacks must not be presented as captured-frame rates. The diagnostic late-interval threshold follows the requested 240 fps and therefore marks ordinary 16.7 ms intervals; its count is not the number of lost 60 fps frames.

Temporary metadata-only probes, with a moving window requesting 240 Hz updates, tested zero versus explicit 1/240 intervals, queue depths 3/5, three alternative display filters, small/full-size ten-bit streams, and a small eight-bit diagnostic control. Whole-display complete frames remained about 60 fps; independent ten-bit window capture reached about 120 fps. No probe saved or transmitted images. Production pixel precision and resolution were unchanged.

To remove interference from a second active TD capture session, the installed host was cleanly stopped for a roughly 15-second isolated probe, with automatic restoration and an independent watchdog. Full 4096×2560 ten-bit display capture still produced about 60 complete frames/s both at zero interval (242 complete, 7 idle; 4.008 s) and at 1/240 (243 complete, 2 idle; 4.025 s); independent 800×360 ten-bit window capture measured 119.834 fps. The original installed host resumed under PID 16955, Windows reconnected with target 240, and debug logs continued arriving. No production binary, display mode, network setting or picture setting was changed.

This identifies the current whole-display capture path as the first barrier to delivering more than 60 distinct frames/s, independently of TD packing or transmission. It does not prove a universal macOS 60 fps limit or identify which part of desktop composition, virtual display implementation, or ScreenCaptureKit causes the difference. The capture ceiling and occasional Windows receive/presentation pauses remain unresolved. Evidence: build/tests/td-0.8.26-live-summary.md and the associated display-clock, capture-cadence, window-cadence, filter-cadence, isolated-cadence and restored-host logs.

## 2026-10-08: 0.8.27 partial CPU/GPU work and multimedia scheduling

Windows now keeps per-tile local revisions for the reconstructed baseline, each of the three independent packed buffers, and the last successfully submitted picture. A reused receiver buffer copies only its accumulated changed regions; the pixel worker unpacks the union of changes since its last successful submission. Partial changes are uploaded to R16_UNORM luma / R16G16_UNORM chroma baseline textures. Two GPU-local copies freeze the complete ten-bit planes into one of three retained picture slots, which feed the existing sRGB/RGB10 shader directly. An in-use picture is not overwritten; the mutable GPU baseline is never given to the presenter. Immediate-context upload/copy/draw commands remain ordered under the graphics mutex. This removes CPU whole-frame snapshot copies, whole-frame unpacking and CPU-to-GPU whole-frame transfer for sparse updates. Broad updates still use a full-frame path, and the wire protocol is unchanged.

The first raw picture on each real Windows renderer runs a small synthetic GPU check before any desktop upload: all 1024 meaningful ten-bit values, both plane pitches, a partial Y/UV update, and an earlier immutable GPU snapshot are checked by synthetic-only staging readback. Success logs video.raw.gpu.check. Pixel, receiver and presenter threads register with MMCSS Playback/high, with the prior above-normal priority as fallback; no system-wide priority/timer/registry change is made. Mac raw video uses writev for the header plus first pixel bytes, correctly handling partial writes, cancellation and socket backpressure. New debug timing separates CPU unpack and GPU upload, and reports processed bytes and upload region count.

Validation completed: 87 Swift cases, 18 C++ groups and 9 isolated trash-cleanup scenarios; strict Windows x64 GUI cross-build including avrt; signed Mac Release 0.8.27/build 48; production HostServer/RawVideoSender v9–16 and legacy socket scenarios. The new pixel tests cover three independent cached snapshots, dropped pictures, failed-submission retry, empty/full updates, malformed data, region merging, 322/4094-pixel padded edges, 242/2562-line chroma edges, and exact scalar versus vector output. Concurrent inbox tests also verify revision metadata remains immutable while held. Native x86_64 test code compiled, but this Mac lacks Rosetta and cannot execute that binary; real Windows x64/GPU checks remain required.

Local arm64 synthetic CPU comparison, 4096×2560 and 36 changed 128×64 tiles per frame over 180 frames: old whole-frame snapshot/unpack 1.630 ms/frame; partial path 0.413 ms/frame. Average snapshot copies 19,660,800 → 1,958,912 bytes; unpacked pixels 31,457,280 → 1,054,583 bytes. Final ten-bit pixel arrays match exactly. GPU work, networking and native Windows scheduling are excluded, so this is evidence of reduced CPU work, not a guarantee of Windows frame rate. Evidence: build/tests/td-0.8.27-partial-pixels-bench.log.

An isolated alternative whole-display CGDisplayStream ten-bit probe at requested 240 fps produced 241 complete frames in four seconds at both 1280×800 and 4096×2560 (about 60.25 fps). Updating a running SCK configuration to 1/240 also stayed about 60 fps. The installed host was automatically restored after the short isolated test. This experimental deprecated API is not included in production. The [BetterDisplay maintainer's virtual-240-Hz explanation](https://github.com/waydabber/BetterDisplay/discussions/4954) describes virtual-screen refresh limitations consistent with these local measurements; it does not replace actual verification of another physical display or virtual implementation. The current virtual whole-desktop capture ceiling remains, and 0.8.27 does not claim to supply real 120/240 fps from it.

Both 0.8.27 installers were expanded: Mac application versions and executable bytes match the signed build; Windows GUI binary bytes, new pipeline markers and START-HERE text match the build/source. Mac binary SHA256 101de4ccdb2e6d235d8d1a373d4ea4bb2661506ebbd6ae84a76fc04e1789752b; Windows binary SHA256 51840ade67693f471fbd86a5f2ee58ef473d46d6428dd3b5a5d9be9bfd44aa93. Previous installers/metadata are backed up under build/checkpoint-0.8.26/artifacts. The user has been asked to install both 0.8.27 packages, reconnect and leave debug sharing enabled; real Windows GPU self-check, frame rate and longest interval are pending that update. No production installer was automatically run and no global network configuration was changed.

## 2026-10-08: resizable Windows diagnostics without automatic scrolling

Diagnostic updates skip identical text and preserve both the current first visible line and selection while repainting once. The read-only edit no longer uses ES_AUTOVSCROLL. Its default height is 220 logical pixels (formerly 88), with a themed bottom resize grip, 120–960 bounds and a height stored in HKCU even without connecting. Mouse capture uses screen coordinates and the DPI at drag start; capture loss commits the height, Escape cancels, double-click resets, and focused arrow/Page/Home/End keys resize. The panel stays adjustable during a connection. Card bounds, footer and page scrollbar range follow the preferred height; width follows the window. Layout changes preserve the log viewport as far as the edit control permits.

The portable layout/scroll/cursor checks passed, including minimum/default/maximum/corrupt height values, footer separation, unchanged preceding cards and document position preservation across range changes. Strict Windows x64 GUI compilation passed with warnings as errors. Both 0.8.29 installers were expanded and their payloads verified; Mac build 50 retains the persistent signing identity. This environment cannot execute the native Windows UI: actual ROG drag, dark-mode painting, selection and reading-position acceptance remains outstanding. No production app was installed or restarted.

## 2026-10-08: first-frame transfer failure and 0.8.29 recovery candidate

After both endpoints updated to 0.8.27, control pairing and Windows diagnostic sharing worked, and Mac continued capture/preparation near 60 fps. Windows received no complete video frame and reconnected roughly every twelve seconds. Mac queued the first 19,660,816-byte raw-update payload slowly (about 8.3 seconds until queued), with a TCP send window of 65,280 bytes and about 8 MiB still buffered. The local SDK declares tcpi_snd_wnd in bytes; the separately reported scale must not be multiplied into it again. The Windows GPU had not yet received a frame.

A brief 0.8.26 Mac comparison eventually produced a picture with the unchanged 0.8.27 Windows client and passed synthetic GPU ten-bit/partial/immutable validation. Several subsequent windows were around 60 receive/upload/Present fps. However, a fresh signed build with the old send path still reproduced failure, so that isolated success did not establish writev as the cause. Each comparison automatically restored the installed 0.8.27 host. Evidence: build/tests/td-0.8.27-connect-failure.log, td-connect-ab-old.log, td-connect-ab-fixed.log and td-connect-ab-restored.log.

0.8.28 reserved 16 MiB receive storage after connect, removed Playback registration from the raw receive thread, and replaced blocking receive timeouts with nonblocking recv/select. [Microsoft's socket option documentation](https://learn.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options) states a blocking receive timeout leaves a connection indeterminate. Actual 0.8.28 logs confirmed the requested storage was applied, but Mac still saw a 65,280-byte TCP window. Startup byte progress revealed about 1.35 MB/s, reaching only about 15.6 MB of the 19.7 MB first frame before the twelve-second presentation deadline discarded the connection. This candidate did not restore the user's connection. Evidence: build/tests/td-0.8.28-live-check.log and the Mac Windows-client log.

The known working v2 synthetic throughput tool reserves receive storage before TCP connect/SYN, unlike the initial application fix. 0.8.29 now reserves at least 16 MiB before connect and logs phase=before_connect. It also counts each actual recv byte immediately, so bandwidth remains measurable before the first complete picture. A raw first frame may continue past twelve seconds only while bytes are arriving; five seconds without byte progress expires, and total first-frame time never exceeds sixty seconds. Compressed startup deadlines and post-presentation frame-health recovery remain unchanged. Recovery-policy tests cover a slow progressing baseline, no progress, exact boundaries, clock reversal and the total deadline. Global TCP, MTU and network adapter settings are unchanged.

Strict Windows x64 GUI compilation and both signed/extracted 0.8.29 installer payload checks passed. The production HostServer socket scenarios and paced synthetic raw-update transport passed after restoring the Mac send path; loopback complete-image rates were 59.9571 and 120.004 fps for requested 60/120, with exact pixels. This is not a physical-link or Windows display result. Previous installers are backed up in build/checkpoint-0.8.28/artifacts. Windows 0.8.29 remains compatible with installed Mac 0.8.27. The user has been asked to update Windows and reconnect with diagnostic sharing enabled; actual 0.8.29 recovery is pending this update.

The automatic-scroll edit was withdrawn on the user's instruction; this recovery work makes no change to the log view behavior.

### 0.8.29 actual Windows acceptance

The user installed Windows 0.8.29 and reconnected to unchanged Mac 0.8.27. Actual TCP receive window immediately became 16,777,216 bytes (scale 9) instead of 65,280 bytes. The first 19,660,816-byte payload completed in 45.486 ms and GPU self-check passed. After excluding the first three seconds, 140 measured windows spanning 141.004 seconds recorded receive 60.005, upload 60.005 and successful Present 59.998 fps. There were no connection ends, zero busy drops and one pending replacement. The largest display interval was 89.863 ms, so restored connectivity and average 60 fps do not establish stutter-free cadence. Evidence: build/tests/td-0.8.29-windows-live.log, td-0.8.29-live-summary.json and td-0.8.29-live-check.log. Mac 0.8.27 still uses writev during this successful measurement; the original suggestion that writev caused the failure was disproved by the restored connection. No Mac update was needed.

All current regression checks passed: 87 Swift cases, 18 C++ groups including raw first-frame progress deadline coverage, 9 isolated trash-cleanup fixtures, strict Windows GUI build, Mac build/signature and extracted installer verification. No automatic-scroll change was retained.


## 2026-10-08: fresh 0.8.30 rebuild after version collision

The user requested a new build because 0.8.29 was already used. Both platforms were rebuilt from the fixed build/release-0.8.30-source snapshot, with Windows version/resource 0.8.30 and signed Mac build 51. Strict Windows GUI compilation, native Mac build/signing, setup layout/scroll/cursor checks and recovery freshness tests passed. Both installers were expanded and matched to their built payloads. Windows payload SHA256: 15f462cf0b9f360767f8454f34ffd4d1e9117c749f4a90d910afc36cab9de11e. Mac host payload SHA256: b68d914e16580ef18dc264fb018f19eb0e775883ad2cc5dc540a7f70458e7582. dist retains only the 0.8.30 installers and existing bandwidth tools; SHA256SUMS was regenerated. Native Windows UI interaction was not executed in this rebuild. No production app was installed or restarted.


## 2026-10-08: 0.8.31 native ten-bit packing and Windows presentation stalls

Reviewed the primary implementations in [OBS ScreenCaptureKit capture](https://github.com/obsproject/obs-studio/blob/master/plugins/mac-capture/mac-sck-video-capture.m), [Moonlight frame pacing](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp), its DXGI vsync source, and Sunshine macOS capture. OBS exchanges the latest IOSurface; Moonlight bounds queued pictures and takes the newest picture after display readiness. TD already uses bounded latest-picture queues; the remaining actual stage measurements determined this change. No upstream source was copied.

Mac now calls a small optimized C helper for P010 masking and packed ten-bit output. On arm64, sixteen samples use two 16-byte loads and a 16+4-byte packed write; scalar row tails do not read padding. All ten meaningful bits are retained. The synthetic 4096x2560 comparison was byte-identical: prior Swift packing median/p95 3.591/4.906 ms, native packing 0.909/1.426 ms. The final short actual capture comparison saw prior packing near 3.9 ms versus native about 1.0–1.3 ms, with P010 locking separated in the log. These are packing-stage measurements, not overall frame-rate improvements. A native delta classifier/gather candidate was slower in the live path (roughly 3.7–4.2 ms versus prior approximately 2 ms for the earlier sparse scene), so the original Swift exact-update code remains. The deprecated alternative capture probe is also absent from production. Direct ScreenCaptureKit x420 output was explored separately but rejected: 650 of 3264 sampled Y/UV components differed from the existing RGB10→P010 reference, maximum difference 113/1023; native buffer color attachments were nil. Existing sRGB/BT.709 conversion remains.

Longer measurements exposed a separate Windows problem. In build/tests/td-0.8.31-fourth-trial-summary.json, the installed-before steady stage recorded receive 60.228 fps but Present 40.838 fps over 15.059 seconds; the temporary native candidate recorded receive 60.194 but Present 38.173 over 85.374 seconds. Both had many replacements and long GPU/display waits, with maximum Present interval 1.066861 seconds in the latter stage. Windows logged Present calls around 0.2–1.0 seconds, while Mac capture/send continued near 60 fps and many payload receives were under 4 ms. Earlier shorter sparse tests had about 60 successful Present fps and lower longest intervals, which were insufficient to establish sustained stability. The final 12:54 comparison had larger payloads of approximately 14–16 MB, source/send near 30 fps and 3.4–3.8 Gbps; Windows stalls remained. The native helper still reduced packing time, but this larger-payload stage was not a clean 60 fps acceptance scene. Exact window activity during these stalls has been requested from the user and is not established.

Windows now uses [DXGI_PRESENT_DO_NOT_WAIT](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present), treating WAS_STILL_DRAWING as a retry rather than a fatal error. Failed live submissions restore the picture only if no newer picture exists and the session generation is still current. The consumed display-readiness permit is retained when no Present was queued, so the last static frame can retry without another source frame or DXGI signal. Retry work is bounded by the requested frame interval, releases the graphics lock, and is interruptible by shutdown. Window paint/resize calls now set a request consumed by the presenter instead of waiting on the graphics mutex or making DXGI calls on the message thread; [Microsoft documents that Present can wait for that window thread](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#multithread-considerations). This removes the known lock cycle; whether it resolves this specific hardware's long blocking calls remains an actual Windows verification item. Input geometry remains cached independently; retained repaints never mark a frame as newly received. The 0.8.30 diagnostics UI changes from the separate task are preserved.

Receiver diagnostics now separate payload readiness/select waits, single recv-call duration, and gaps before the receiver runs again. WSAGetLastError is captured immediately after a failing recv before timing calls. Source-PTS and complete-arrival maximum intervals plus rate-limited payload-stall events help distinguish source, transport and scheduling. Mac logs split tile-update construction and socket send duration, with bounded slow-frame events. The pre-connect 16 MiB receive buffer and first-frame progress policy from 0.8.29 are unchanged.

Regression validation passed: 87 Swift cases, 19 C++ groups and 9 isolated trash-cleanup fixtures. Native packing covers all 1024 values, nonzero unused bits, unaligned input/output, tight and padded rows, all vector tails and the existing Windows unpacker. Address/undefined-behavior sanitizers passed. Latest-picture tests cover static retry, newer arrivals, retired generations and shutdown. A local CMake Debug configure/build and raw-pixels/latest-picture CTest pair passed. The production raw-update transport test now also covers 240 fps: paced synthetic 4 Gbps loopback measured 60.0103, 120.007 and 239.978 complete-image fps. Every reconstructed packed image was exact, including dense startup/drop transitions. The steady scene changes only corner pixels; it does not test ScreenCaptureKit, physical Thunderbolt throughput or Windows GPU/display. Previous production HostServer socket checks also passed in this turn.

At 4096x2560, packed ten-bit 4:2:0 requires 19,660,800 bytes per complete frame. Ignoring network overhead, 60/120/240 fps require 9.437/18.874/37.749 Gbps. The prior physical test near 4.2 Gbps can carry approximately 26.7 complete frames/s, or a theoretical average update payload below 2.1875 MB at 240 fps. It cannot guarantee high frame rate for arbitrary whole-screen changes; the current virtual whole-desktop source has previously measured near 60 fps despite requesting 240. No duplicate frames are counted as higher source throughput.

Both final 0.8.31 installers were built and expanded with exact payload matching; Mac build 52 preserves signing identity. Mac host executable SHA256: 6afc4fcdc3fef88458403c66b55c466bba42b28882c7c29ae2b771eb38e709e6. Windows executable SHA256: c216362a233afa37cecfba9b8e32b264dbff2bd9dc142c78faffd38a876550be. Prior 0.8.30 packages are retained in build/checkpoint-0.8.30/artifacts. Temporary motion and host comparisons ended and restored the installed Mac app; no installer was automatically run. Windows 0.8.31 remains compatible with installed Mac 0.8.27. The user has been asked to update Windows first with diagnostics sharing enabled; no claim of stable 60 or actual 240 fps is made before that acceptance.


### 0.8.31 actual Windows update and motion verification

The user installed Windows 0.8.31 and reconnected with debug sharing. Logs explicitly report version=0.8.31, 4096x2560, target_fps=240, vsync=1, receive storage 16 MiB applied before connect, and successful GPU ten-bit/partial/immutable self-check. The prior 0.8.30 debug records also had vsync=1 and the same negotiated dimensions/rate. The installed Mac remained 0.8.27; no Mac installer was run. In ordinary mixed activity, lower receive/Present rates coincided with larger source timestamp intervals (for example 183–200 ms), and no old display.present.slow events appeared. The mixed period includes idle frames and is not a stable-motion FPS acceptance average.

A separate nonactivating synthetic moving panel was made visible on the selected 4096x2560 screen across spaces, ran for 55 seconds and closed. It did not restart TD or record pixels/input. Excluding the first four seconds, 51.479177 seconds of actual Windows windows recorded receive/upload/successful Present 59.888292 fps each, zero busy drops and zero pending replacements, no connection ends and no slow or busy Present events. Maximum measured Windows render work was 0.982 ms. DXGI displayed-picture statistics were also available and generally near 60 fps; the requested target remained 240. This verifies that the previous continuous 0.2–1.0 second display blocking did not recur during the motion run.

The longest successful-Present interval was still 75.818 ms; three one-second windows exceeded 33.333 ms. At 13:17:36 a 18,941,362-byte update took 30.322 ms to receive, including 26.326 ms waiting for data, maximum single recv 108 microseconds and receiver gap 2 microseconds; the source interval was 33.333 ms. At 13:18:12 a 17,819,936-byte update took 28.745 ms, with 25.480 ms readiness wait, maximum recv 116 microseconds and receiver gap 2 microseconds. The worst windows had equal receive/upload/Present counts rather than a backlog of received pictures. Remaining jitter therefore includes source/large-update arrival timing; it must not be described as eliminated or as an ongoing slow GPU Present. Peak measured one-second payload throughput was 3.02 Gbps. The Mac still used the earlier scalar packer, around 4.7–5.3 ms with maxima near 9.5 ms during the sparse portion; the separately built 0.8.31 Mac helper can reduce this stage, but its installed combination is not measured here.

Evidence: build/tests/td-0.8.31-windows-live.log, td-0.8.31-live-summary.json, td-0.8.31-win-motion.stage.json, td-0.8.31-win-motion.windows.log, td-0.8.31-win-motion.mac.log and td-0.8.31-win-motion.summary.json. The temporary panel terminated normally and the installed Mac host continued. Existing source requests may be lower when idle; late_intervals is based on the requested 240 fps budget and is not a standalone lost-frame counter for a 60 fps source. Full-frame 240 fps remains beyond both the measured link and a nominal 20 Gbps payload budget.
