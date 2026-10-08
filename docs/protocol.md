# Protocol v1

所有多字节整数使用大端；有符号坐标 / 滚轮使用 int32 二进制补码。不得直接传输 C / Swift struct 内存。

## TCP

每条消息：`uint32 payloadLength` + payload；payload 长度 1–4096。payload 首字节为类型。

| 类型 | 值 | 后续字段 |
| --- | --- | --- |
| Hello | 1 | version:u16=1, udpPort:u16, width:u16, height:u16, fps:u16, bitrate:u32 bits/s, codecMask:u8, token:32 bytes（启用时 ASCII hex；关闭时零填充） |
| Welcome | 2 | version:u16=1, session:u64, codec:u8, width:u16, height:u16, fps:u16, bitrate:u32 |
| Input | 3 | kind:u8, code:u16, flags:u16, x:i32, y:i32 |
| RequestIDR | 4 | 无 |
| Ping | 5 | 无 |
| Pong | 6 | 无 |
| Failure | 7 | UTF-8 失败说明 |
| Ready | 8 | 无 |
| CapabilityQuery | 9 | 无（旧查询）或 extension:u8=2（0.5.0 Main10） |
| Capabilities | 10 | version:u16=1, sourceWidth:u32, sourceHeight:u32, sourceHz:u16, maximumWidth:u32, maximumHeight:u32, maximumHz:u16, codecMask:u8=3/7, streamBits:u8=8/10, flags:u8, nameLength:u16, name:UTF-8 |

Hello payload 48 bytes；Welcome 22 bytes；Input 14 bytes。FPS 有效范围 1–240（0.4.0 接受包括 75 / 90 / 144 在内的刷新率）。
配对验证默认关闭，Host 在所选本机 IPv4 上监听，并限制 TCP 对端为该接口同子网。启用验证后，32 位 token 必须匹配；空码或错误码返回 `PairingRequired:` Failure，不自动降级。未启用验证时，同子网任何设备均可成为单控制客户端；该模式用于独立直连网段。关闭验证的新 Client 不能连接要求配对码的旧 Host；两端应同时更新。
codec：1=H.264、2=HEVC Main、4=HEVC Main10；codecMask 是位集合，3 表示 8-bit 两编码均支持，7 增加 Main10。
视频 UDP 目标是 TCP 对端 IP 加 Hello 中的端口，不能由客户端指定任意第三方 IP。
流程：连接 → Hello 配置 / 可选鉴权 → 初始化编码 / 捕获 → Welcome → Windows 初始化 GPU 解码 → Ready → 视频 / 输入。
每连接一个独立随机 session，不继承前一会话的帧编号。
任何新客户端都不能抢占已连接客户端；首条消息超时和初始化超时 15 秒，控制通道心跳超时 10 秒。

Input kind：1=绝对鼠标位置、2=鼠标按钮、3=键盘、4=像素滚轮、5=释放所有按键 / 按钮。
code：按钮 0=左、1=右、2=中；键盘为 Windows virtual key（保留左右修饰键），0x10D 特别表示小键盘 Enter。
flags：bit0=down、bit1=Shift、bit2=Control、bit3=Option、bit4=Command、bit5=CapsLock。
鼠标 x/y 范围 0–65535，相对于完整串流源（1:1 裁剪时计入裁剪偏移）；Mac 扣除捕获内容的黑边后映射到选中显示器的 CoreGraphics 坐标。
query11 新增的 kind6 / kind7 使用下文的多屏规则；旧 kind1–5 保持原含义。
滚轮 x/y 为水平 / 垂直像素增量。TCP 保持按钮、按键和移动的顺序；只有队列末尾连续的鼠标移动可以合并。

## 自动画质协商（0.4.0）

自动模式：连接 → CapabilityQuery → Capabilities → 计算 Settings → Hello → 原有 Welcome / Ready 流程。手动 8-bit 可直接发送原 v1 Hello；自动 10-bit 或强制 10-bit 同样先查能力。Hello / Welcome / 视频包长度未变化。0.5.0 Query 扩展兼容 0.4.x；更早 Host 无查询能力时使用手动 8-bit 模式。

sourceWidth / Height 是 Mac 当前模式的渲染像素，区别于 HiDPI 桌面逻辑尺寸（UI 单独显示）。sourceHz 来自当前模式刷新率；VRR 返回 0 时使用 NSScreen 当前屏幕的最大刷新率。maximum 是系统列出的最大像素面积模式及该尺寸的最高刷新率，不把不同模式的尺寸 / 刷新率组合。源屏幕配置变化后更新下一次协商的快照，已有连接不修改编码尺寸。

Client 检测所选 Windows 显示器的首选原生尺寸及该尺寸支持的刷新率。分辨率按 Mac 当前渲染像素比例缩放到目标尺寸，同时限制 4096×2304，不放大源图像并向下取偶数。FPS 取源当前刷新率、目标原生模式上限与 240 的最小值；以像素数×FPS×0.45（HEVC）或 0.72（H.264）估算码率，向上取 10 Mbps 档位并限制 80–300 Mbps。该策略为画质优先目标，不保证所有 GPU 均可持续满帧率。连接不修改系统显示模式。

Capabilities nameLength 不超过 256 bytes，flags bit0=HDR/EDR 能力、bit1=P3；这些是显示器信息。0.5.0 新能力查询为 `[9,2]`，codecMask=3 表示 8-bit HEVC / H.264，=7 另含 Main10，streamBits=8 / 10 表示可提供的最大串流精度；旧查询 `[9]` 始终返回 3 / 8 保持旧客户端兼容。新客户端若旧主机关闭扩展查询，会改用旧查询重试。Hello codecMask bit2 / Welcome codec=4 / UDP codec=4 表示 HEVC Main10，其他消息长度保持不变。实际串流仍为 SDR 4:2:0，不协商 HDR / 4:4:4。接收器校验长度、尺寸、刷新率、版本和保留位；该预鉴权信息不允许捕获桌面或注入输入，也不绕过原有配对 / 同子网校验。

## UDP 视频

每个编码 access unit 转换成带 4-byte start code 的 Annex B；关键帧前附加 VPS/SPS/PPS（H.264 为 SPS/PPS）。
随后按 1160 bytes 分片，每包含以下 40-byte header，最大总长度 1200 bytes。

| Offset | 字段 | 大小 |
| --- | --- | --- |
| 0 | magic=`TDB1` (`0x54444231`) | 4 |
| 4 | version=1 | 1 |
| 5 | codec | 1 |
| 6 | flags，bit0=key frame | 2 |
| 8 | session | 8 |
| 16 | frame ID | 4 |
| 20 | 捕获 PTS，相对会话起点，微秒 | 8 |
| 28 | 完整 access unit 长度 | 4 |
| 32 | fragment index，起点 0 | 2 |
| 34 | fragment count | 2 |
| 36 | 本片 payload 长度 | 2 |
| 38 | reserved=0 | 2 |

单帧上限 4 MiB。index/count/length 必须与完整帧长度吻合，不接受截断、额外字节和冲突元数据。
frame ID 按 u32 递增允许回绕；同会话中的有效序号距离必须小于 2^31。
客户端只接受 TCP Host IP 且 session/codec 匹配的视频。

重组最多保留 3 帧。25 ms 未完成或依赖序号存在间隙且后续帧过期时，丢弃待组帧并请求 IDR；新的完整 IDR 可以跳过缺失帧恢复。
连续 P 帧按顺序提交，不能任意丢掉参考 P 帧后继续解码其依赖帧。
请求 IDR 最快每 250 ms 一次。队列溢出同样需要从 IDR 恢复。
PTS 用于解码时间戳，不与 Windows 本地时钟相减估算网络延迟。

## 本地发现

Windows 对本机启用的 IPv4 Ethernet 接口的子网广播地址发送 `TDDISC1?`（8 bytes），目标 UDP 47990（或 `--port`）。
Mac 发现 socket 接收广播，但仅回答与选定串流接口处于同一 IPv4 子网的发送者；答复由绑定该接口的 UDP socket 发出，内容 `TDHOST1 <TCP port>`。
客户端取响应源 IP 连接 TCP，Mac 开启配对验证时继续验证配对码；发现响应不携带配对码或视频权限。
缺少正确的网桥地址、子网掩码或防火墙放行时使用手动 IP。

## 队列与生命周期

Mac socket 事件、捕获回调、编码状态、CGEvent 注入都在同一串行 pipeline 队列上；VideoToolbox 回调回到该队列。
编码入队只允许一个 in-flight frame。UDP socket 非阻塞，发送失败时丢帧并安排下一帧 IDR。
保留一个最新捕获表面供 Ready / IDR 请求使用，避免静止桌面等待下一次内容更新；编码 PTS 始终单调递增。
捕获启动为异步操作，启动 / 断线竞态通过队列检查和停止 SCStream 处理。
TCP 输出最多 64 KiB，停滞后关闭会话；退出和断线均释放注入的键鼠。

Windows 网络 / 解码在工作线程上；D3D immediate context 开启多线程保护，renderer mutex 保护缩放和呈现。
同步 MFT 交替 ProcessInput/ProcessOutput；异步 MFT 根据 NeedInput/HaveOutput 事件驱动，解码前的压缩帧队列最多 2 帧。
GUI 输入输出队列最多 256 条，超限关闭 TCP 让 Host 释放按键。Swap chain 最大帧延迟 1；无平滑播放缓冲。

## 可选 UDP 视频通道探测（0.3.3）

Ready 后，Client 从 Hello 中声明的同一个 UDP 视频接收 socket 向 Host 的控制端口发 `TDVIDEO1 <session>`。Host 仅接受当前 ready 会话、TCP 对端 IP 和 Hello UDP 端口均匹配的探测；使用被查询的发现 socket 回复 `TDVIDEO1 <session> <videoPort>`。Client 核对 IP、响应源端口和 session，向实际 videoPort 再发送同一探测，建立对应 UDP 返回路径；Host 收到后请求 IDR，静止桌面也会重发。尚未得到端口时每 500 ms 探测，得到后每 2 秒维持路径。该扩展不修改 v1 Hello / Welcome / 视频包格式，旧端忽略不认识的探测。探测不会绕过显式 UDP 阻断规则；首帧详细状态会报告零视频包。

Client 初始 IDR 不 flush 新建解码器；依赖丢失后才在新关键帧处 flush。异步队列先 pump 输入事件再判断容量。首帧 12 秒无解码输出报告阶段并重连，自动模式 HEVC 已收完整帧但未输出或运行时报解码错误时改试 H.264。

## 0.6.0 原始像素与文本剪贴板

客户端优先 `[9,3]` 查询。Host 仍返回同一 Capabilities 形状，flags 新增 bit2（值 4：文本剪贴板）和 bit3（值 8：最高 4096×4096）；查询 `[9,2]` 和 `[9]` 会屏蔽新增位，旧查询继续受原 4096×2304 限制。旧 Host 关闭扩展查询时，新 Client 按 3→2→1 重连查询。只在 query3 被接受的连接上允许超过 2304 行的 Hello。其他 Hello / Welcome / UDP 结构不变。

自动原始像素策略在不放大的前提下按 Mac 渲染源尺寸串流，最大边 4096；保留比例并取偶数。FPS 和码率沿用源 / 目标上限和 80–300 Mbps 算法。关闭原始像素或不具备 feature bit3 时使用面板适配策略。编码 / 解码失败先回退面板尺寸，再按现有 Main10 / HEVC 规则回退；强制 10-bit 不偷偷回退色深。

消息 11：`u8 11, u8 enabled`，只能在 Ready 之后且声明 feature bit2 时使用。Client 请求启用；Host 按 Mac 开关答复实际启用状态。Host 开关变化也发送状态；Client 收到允许后才读取 / 发送新文本，断线清空队列与缓存。关闭状态可能与在途文本交叉，Host 丢弃关闭后的在途文本。

消息 12：`u8 12, u32 transferID, u32 totalUTF8Bytes, u32 offset, byte[] payload`。非零 ID，每片最多 3072 字节，总量最多 65536；允许 0 字节文本。偏移必须连续，同一 ID / 总量；新 offset0 可替换旧文本，完整后验证 UTF-8，拒绝 NUL、过长、乱序、非法编码。两端使用相同 golden bytes。文本超限直接跳过，不截断。Mac 每 10 ms 最多提交一片，Windows 在键鼠 / 心跳队列空闲时提交一片，避免把大剪贴板堵在输入前面。

粘贴板访问在两端 UI 线程，变化序号在本地写入后更新，避免回传循环；连接时只建立变化基线，不导出已有内容。断线和旧会话回调不会改动新会话剪贴板。


## 0.6.2 高码率、大帧与本地指针

优先能力查询 `[9,4,localCursor]`，localCursor 为 0 / 1；也接受不带第三字节的 `[9,4]`，默认使用视频指针。flags bit4（16）表示 10–1000 Mbps 与 16 MiB 单帧，bit5（32）表示本地指针设置。query4 未成功时按 4→3→2→1 回退；query3 屏蔽 bit4 / bit5，query2 / query1 继续保持原兼容形状。所有 Hello / Welcome / UDP 字节结构不变。

只有 query4 成功的连接接受超过 300 Mbps 的 Hello 并发送超过 4 MiB 的视频帧；旧查询仍受旧边界保护。新 Client 对不支持 bit4 的 Host 明确提示更新或降低码率，不能静默把 1000 Mbps 改回 300。码率是 VideoToolbox 长期平均目标值，不是网络恒定吞吐或硬件性能承诺。

16 MiB 仍最多三帧重组内存；超出协商帧大小的包在分配前拒绝。普通 P 帧及旧会话仍使用 25 ms 分片期限；新会话的 IDR 取 `clamp(size * 16_000_000 / requestedBitrate + 10_000, 25_000, 300_000)` 微秒，允许在请求带宽下完成较大的 IDR，同时保持有界失效恢复。单个 UDP 包仍最多 1200 bytes。

localCursor=1 时，Host 创建 ScreenCaptureKit stream 前即设置 showsCursor=false；Client 在 bit5 确认后才显示本地箭头。旧 Host 无 bit5 时保留视频指针并报告不支持。仅控制视频里的指针，不改变 Mac 物理屏幕指针；开关在重新连接时生效。当前本地箭头按 Windows DPI 生成，远端动态形状尚未传输。

GUI 模式只有 Auto / Custom，均按检测到的 Mac 当前刷新率与 Windows 目标显示模式协商 FPS（上限 240），不对用户列出固定 FPS 档位。CLI 显式 --fps 和历史模式别名保留兼容，不影响新的 GUI 两档模式。

## 0.7.0 当前协议：动态原生指针、图片与 64-bit 码率

以上版本章节保留历史结构。0.7.0 Client 优先 `[9,5,localCursor]`，失败时按5→4→3→2→1回退。query4及更早连接保留视频指针；query5只在主机系统指针 getter 可用时提供 bit5（32）。新bit6（64）表示64位码率 / 64 MiB单帧 / 动态指针 / 图片剪贴板扩展。query5保留 bit2文本、bit3原始像素、bit4大帧；仅 bit6成立才发新消息。不同版本没有追加旧消息字段。

HelloWide=15，version=2，其他字段顺序沿用Hello，将码率改u64，长度52；WelcomeWide=16，version=2，码率改u64，长度26。query5及后续会话接受该结构。query5–9请求范围10–20000Mbps；query10允许检测到的更高速率，传输校验上限1000000Mbps；旧Hello仍u32，最高1000Mbps。Welcome返回硬件接受的码率目标，若低于请求，客户端明确显示限制。硬件实际输出吞吐不由此字段测量。视频40-byte header不变，最高64MiB约57853分片，仍不超过u16；query4继续16MiB，更早4MiB。在分配之前校验协商边界，重组最多3帧；IDR期限按接受码率计算，P帧25ms。

0.8.18 使用 `[9,10,localCursor]` 进行能力查询；旧 Host 关闭查询后依次回退到既有版本。Windows 从 TCP 实际绑定的 IPv4 定位网卡，使用 `GetIfEntry2` 的接收链路速率限制 Mac→Windows 码率；连接前可按 Mac IPv4 路由或唯一活动 Thunderbolt / USB4 网卡预估，多个候选时不猜测。驱动未报告有效值时回退 20 Gbps。设置页面的定期网卡查询在独立后台任务进行，不进入收包、解码或输入循环。码率滑块 0 表示自动，右端表示跟随链路上限；保存绝对请求值和右端跟随状态，自动码率在原有画质策略与链路上限间取较小值。旧 Host 的协议上限及硬件编码器接受上限仍单独约束实际请求，旧协议的线格式与分片上限保持不变。

CursorImage=13、ClipboardImage=14，分块均为 `u8 type, u32 ID, u32 total, u32 offset, payload`，片长最多3072，非零ID，偏移连续，允许offset0替换未完成传输。指针总量512KiB，图片32MiB；截断、乱序、跨ID、越界先拒绝。ClipboardImage需Ready、bit6、双方剪贴板开关已确认。文字仍消息12 / 64KiB。图片PNG解码限制单边8192、最多16M像素，并检查原生转换尺寸；变化序号、会话和启停抑制回传及过期内容。

CursorImage完整body为 `u8 version=1, u32 logicalWidth16.16, u32 logicalHeight16.16, i32 hotspotX16.16, i32 hotspotY16.16, PNG`。尺寸与热点是系统指针逻辑单位，PNG维度独立，Windows只乘自身DPI一次，HiDPI视频不再乘2。隐藏指针传透明1×1 PNG。macOS运行时读取WindowServer原始像素、尺寸、原生热点和像素变化；这些系统getter为可选私有接口，不猜状态或绘制形状。主机首次协商前只读检查，不可用时不广告本地指针能力，SCK保留showsCursor=true。poll60Hz，无变化不发，AppKit common run-loop modes避免菜单/拖动时暂停。指针片优先于图片，最新形状替换旧排队形状，不积累指针动画。

开机helper仍使用TDHOST1发现，仅选中bridge同子网，固定47990；无console用户时回应Failure `HostWaitingForLogin: ...`，不接收Hello / Ready / Input，也不捕获画面。Client遇此状态保留查询版本自动重试，不误降级。登录后关闭发现 / TCP socket，Aqua用户Host接管。


## 0.7.2 desktop sRGB / query6

0.7.7 adds query7 `[9,7,localCursor]` and falls back 7→6→5→4→3→2→1. It retains query6 sRGB/feature flags and all Hello/Welcome/video fields. Only a query7 peer receives a multi-representation cursor payload. Query1–6 behavior is preserved.

For query7, CursorImage body is `u8 version=2, u32 logicalWidth16.16, u32 logicalHeight16.16, i32 hotspotX16.16, i32 hotspotY16.16, u8 count, {u32 PNGLength, PNG}[count]`. Count is1–8, aggregate body ≤512KiB; truncated lengths, invalid signatures/geometry, excessive counts and trailing data are rejected. Each PNG is decoded with a1024-side/1M-pixel limit. Logical dimensions and hotspot are shared; the PNGs are actual native macOS representations of the same cursor. Hidden cursor uses one transparent1×1 image. Windows chooses the smallest representation covering its current DPI, or the largest available if none covers it; exact native DPI displays pixels directly. Monitor changes select from the same cached family without reconnecting. Older peers retain version1 with one high-density PNG.

Client first sends [9,6,localCursor], then falls back 6->5->4->3->2->1 for older hosts. Query6 retains query5 features and adds capabilities flag bit7 (128). It requires bit6 (64). Only a query6 peer gets the new flag and sRGB capture/encoding; query1-5 payloads and color behavior are unchanged. Hello/Welcome/UDP sizes remain unchanged.

For a negotiated desktop-sRGB peer: primaries are BT.709/sRGB, transfer is IEC 61966-2-1 sRGB (VUI transfer_characteristics=13), YCbCr matrix is BT.709, range is studio/limited NV12 or P010. Capture RGB is sRGB; the pixel transfer preserves this curve. Windows explicitly converts YUV range and matrix into sRGB-encoded full RGB, without an additional video OETF/EOTF, automatic contrast or saturation. It presents RGB_FULL_G22_NONE_P709 (the DXGI sRGB presentation definition). Chroma is left-sited horizontally and centered vertically, sampled with decoder padding excluded.

Decoder shader resources that are unavailable trigger DesktopColorUnavailable and a query5 reconnect with an explicit legacy-color diagnostic. This feature is SDR color consistency, not P3/HDR or 4:4:4 support.


## 0.8.19 多屏鼠标 / query11

Client 优先 `[9,11,localCursor]`，旧 Host 按现有查询链逐级回退至 query10 及更早版本。原 Capabilities、Hello、Welcome、视频字节形状不变。query11 支持新增的 MouseMode；仅桌面 Host 的活动屏幕布局包含至少两个不同矩形时启用跨屏能力，LoginWindow 保持原输入方式。镜像布局不会被当成扩展桌面。

Welcome 之后、Ready 前发 MouseMode=20：`u8 type, u64 session, u8 enabled`，长度10。enabled只能0/1，客户端校验当前会话ID、长度和保留值。旧查询不发送该消息，也不接受相对输入。enabled只表示能力；Windows必须同时满足无边框全屏、前台焦点、输入捕获开启、实时会话可用才接管鼠标。窗口模式保持kind1/2绝对映射，鼠标可离开TD回到Windows。

Input仍14字节。新增kind6为相对移动，code=0，x/y各为-32767至32767的有符号桌面逻辑点增量，flags沿用原修饰键。kind7为当前位置的按钮，code=0/1/2，x=y=0。Mac以实际Quartz屏幕矩形处理负原点、上下/左右排列、尺寸差异和边缘滑动，不把布局中的空白区域作为显示器。移动与按钮保持顺序，相对移动不可像绝对移动一样被末条覆盖。相对点击及断线释放沿用上次全局位置，避免跳回捕获屏幕；release-all后下一次相对输入重新读取实际指针位置。

Windows只在上述全屏条件成立时注册前台Raw Input，并将本地指针限制在当前TD窗口中心。原始位移不受Windows屏幕边缘限制；legacy鼠标移动消息不会同时发绝对位置。按钮和滚轮继续使用窗口消息，按钮改发kind7。失焦、断线、退出全屏、输入释放以及实时画面不可用时撤销注册/限制并恢复本地指针。窗口拖动离开有效画面时发送release-all并释放本地按钮捕获。

捕获画面始终保持选定的Mac屏幕，Windows屏幕数量/排列不参与远端坐标计算。跨屏会话关闭本地原生指针协商，SCK显示实际视频指针；离开选定Mac屏幕后指针从视频消失，返回后重新出现。实体和虚拟屏幕统一读取系统活动布局。屏幕热插拔、位置/逻辑尺寸/源像素尺寸变化后重建会话和输入快照。

## 0.8.21 独立多屏指针 / query12

以上 query11 描述保留旧端行为。query12 新增 CursorPosition=21：`u8 type, u64 session, u8 visible, u16 x, u16 y`，长度14；visible只能0/1，隐藏时x=y=0。坐标为选定捕获画面的0–65535归一化位置，考虑源内容区域和信箱黑边；指针位于其他Mac屏幕或源内容外时隐藏。Windows按实际视频缩放/裁剪几何还原位置，热点和形状仍来自原生CursorImage。错误长度、会话、状态拒绝处理。

query12及更新端在多屏相对鼠标会话中自动协商独立指针，不依赖单屏的localCursor选项。主机每秒最多60次读取实际Mac全局位置，只在位置或可见状态改变时发送；Windows只保存最新位置，用透明子窗口覆盖在视频上，移动不等待视频帧。失焦、释放、断线、窗口模式时隐藏覆盖层。query11或原生指针读取不可用的主机保持SCK视频指针。捕获/编码仍只有用户选定的一块屏幕。

## 0.8.21 无压缩 P010 / query13

Client优先 `[9,13,localCursor]`，旧Host按现有查询链回退。只有query13广告codecMask bit3（8）的RawP010；其他codec位沿用1/2/4。Raw请求必须是HelloWide v2，codecMask恰为8、bitDepth=10；混合Raw与编码位、旧Hello请求Raw均拒绝。WelcomeWide codec=8、bitDepth=10，码率字段返回 `width * height * 3 * 8 * fps`。客户端最高请求60fps；显式Raw不可自动降尺寸、降色深或回退HEVC/H.264。

原控制TCP和编码UDP结构保持不变。Welcome后、Ready前发送RawVideoEndpoint=22：`u8 type, u64 session, u16 TCPPort`，长度11。主机仅在选定接口启动临时TCP监听，接收与控制连接相同IPv4的客户端；12字节认证为 `u32 magic=0x54445241, u64 session`，沿用协议的大端整数。错误或过期会话关闭该连接，监听继续等待正确连接；控制配对规则仍先于原始视频认证。

视频TCP每帧为20字节头 `u32 magic=0x54445246, u32 frameID, u32 payloadBytes, u64 ptsMicroseconds`，随后为紧密排列的P010：Y平面在前，交错UV平面在后，每行width个16位字，UV共height/2行，无行填充。宽高为偶数且分别在320–4096/240–4096范围内；payload必须严格等于width*height*3。P010十个有效位位于每字的高十位，低六位清零，不截断任何有效位；sRGB曲线、BT.709矩阵、limited范围沿用Main10路径。4096×2560一帧31,457,280字节，60fps有效载荷15.0994944Gbps，网络开销另计。

Raw在Mac跳过VTCompressionSession创建，仅执行RGB→P010转换及紧密复制；Windows不创建视频解码器，直接将P010上传有界D3D纹理池并使用原10位颜色/显示管线。视频发送线程独立于键鼠控制，仅保留一个正在发送帧和一个可被更新帧替换的待发送帧；待发超过50ms丢弃。正在发送的TCP帧必须完整发送，不能在帧中途跳到新帧；异常关闭连接并重连。客户端验证固定尺寸与单调frameID后读取载荷，允许丢弃旧帧产生ID间隔，最多保存一个CPU接收图像，不为不可信头申请额外内存。

## 0.8.22 紧密十位传输 / query14

query14在原codecMask上增加bit4（16），表示RawPacked10；能力掩码为27或31，streamBits=10。query13仍只广告原来的RawP010 bit3，不改变旧端收发字节。新Client在用户选择无压缩后，query14且bit4成立时请求独占mask16，否则支持query13时仍请求mask8。压缩模式协商前继续屏蔽两个Raw位；Raw混合掩码、未协商的mask16和旧Hello均拒绝。

WelcomeWide codec=16、10位；控制端点、认证、20字节帧头和会话规则沿用query13，整数为大端。像素载荷为紧密十位：P010每字高十位的值取出，每行从低位开始排列，每四个样本占五字节。样本a/b/c/d的40位字为 `a | b<<10 | c<<20 | d<<30`，以低字节在前发送；Y后接交错UV，不改变色彩、有效像素或4:2:0布局。每行`ceil(width*10/8)`字节；宽度不是4的倍数时，末两个样本用三字节且末字节高4位必须为零，Client拒绝非零填充。每帧严格为`ceil(width*10/8)*(height+height/2)`字节；4096×2560为19,660,800字节，60fps有效载荷9.437184Gbps，减少37.5%。

Windows预先分配一个紧密帧缓冲和一个P010还原缓冲，每个样本左移6位还原原P010高十位，低六位清零；随后复用既有GPU上传/10位颜色管线，仍不创建视频解码器。Host与Client不再设置固定1MiB的SO_SNDBUF/SO_RCVBUF，保留系统缓冲/窗口自动调节；只改应用自身连接，不改变全局TCP、MTU或网桥设置。Host每5秒记录窗口、RTT、重传和待发替换数，区分发送吞吐与采集吞吐。

## 0.8.25 精确变化区域 / query15

query15增加RawDelta10 bit5（32），能力掩码为59或63。新端选择无压缩后优先请求独占mask32，旧端继续mask16或8；未协商mask32、旧Hello和混合掩码拒绝。WelcomeWide codec=32，像素仍为query14全部有效十位，不创建视频压缩/解码会话。该模式取消客户端60fps硬上限，按两端当前刷新率协商，协议仍限定1–240fps；不切换Mac显示模式。源刷新率已知且请求达到源刷新率时，SCK minimumFrameInterval=0直接跟随源更新；低于源刷新率或源率未知时保留1/fps限制。采集队列仍3，待发送/待显示仍只取最新图像。

TCP认证、端点和20字节TDRF帧头不变；payloadBytes在16与完整packed10大小+16之间。每个载荷先含16字节大端前缀：`u32 magic=0x54444431, u32 baseFrameID, u16 kind, u16 reserved=0, u32 tileCount`。kind0要求baseFrameID=0、tileCount=0，随后为完整packed10图像；首帧必须kind0。kind1要求baseFrameID等于上一条完整收到并应用的TCP帧ID，随后按严格递增顺序传`u16 tileIndex + packed pixels`，tileCount可以为0。瓦片按128×64亮度像素固定网格从左到右、从上到下编号，边缘裁剪到实际宽高；每片先有h行Y，再有h/2行交错UV，每行ceil(w*10/8)字节，尾部两个样本填充规则沿用query14。类型、保留位、尺寸、基帧、索引范围/顺序/重复、截断、尾填充和多余字节均严格验证；所有元数据在修改基图像前完成验证。

0.8.27不改变此线格式。接收端为每个瓦片维护本地64位修订号，三个独立packed10缓冲各自记录已复制修订；只复制从该缓冲最后一次更新之后变化的区域。像素线程另记录上次成功提交的修订，只还原并上传累计变化的Y/UV区域。接收必须继续按顺序应用所有TCP载荷，丢弃待显示快照不丢弃协议基帧；失败提交不确认修订，下一次重试包含尚未提交的变化。GPU使用R16_UNORM亮度和R16G16_UNORM色度纹理存储P010相同的16位字，全部十位有效值保留；可变基图像只用于上传，独立GPU快照交给显示线程，仍在使用的快照不能复用。0.8.28恢复Mac分段send，并为Windows视频连接预留至少16MiB接收缓冲、使用非阻塞recv和select就绪等待。TCP仍为原字节流，对分段边界不作任何协议假设；连接前后均不修改全局网络策略。

Mac发送线程逐字节比较当前完整图像与上一条成功发送的图像，未发送而被替换的采集帧不参与基图像。变化片总字节加头达到完整帧时发送kind0；没有近似比较、量化或颜色变化。发送成功才更新基图像，传输失败关闭连接，不在TCP帧中途换帧。

Windows接收线程按TCP顺序先应用全部变化片，再复制完整独立快照到原三个缓冲的latest-frame队列。图像处理或显示线程丢弃旧快照不会漏掉变化片，也不依赖被丢弃的显示帧。额外接收状态和载荷缓冲均限定完整packed10图像大小（4096×2560两份约37.5MiB）；沿用预分配P010缓冲及有界GPU纹理池。x64在确认SSSE3后批量还原样本，旧CPU保留标量路径；arm64用NEON。两条路径均逐位还原原十位值，低六位为0。

变化区域只减少未变化像素的重复传输，不保证全屏持续变化时低于完整帧带宽；4096×2560每帧全变的60fps仍需约9.44Gbps，加头部和网络开销。不能用局部动画或本机回环的60/120fps代替Windows真实端到端验收。


## Windows debug log sharing (capability query 16, 0.8.26)

Query 16 retains query 15 exact-update video and adds opt-in `ClientDiagnostics` (message 23). Older clients and query 15 hosts keep the existing latency aggregate; a query 16 client falls back to an older query without sending message 23. No video format or input policy changes.

Windows Settings / Connection diagnostics has “Debug: share performance logs with Mac”. It defaults off; its HKCU `DebugLogs` choice persists independently of other connection settings, and changes during an active connection without reconnecting. `--debug-logs` enables it for an explicit launch. Sharing starts only after Ready and the Mac acknowledgment. Unsupported Mac versions are reported in the connection details.

All fields use network byte order: `u8 message=23, u8 version=1, u64 session, u8 state, bytes body`. State 0 stops sharing, state 1 starts it (both exactly 11 bytes). The Mac echoes those packets as acknowledgment. State 2 contains 1–4000 bytes of printable ASCII/newline log text, at most 33 nonempty lines and 1024 bytes per line. Session must match the authenticated control peer, Ready must have arrived, and query 16 must have been negotiated. A batch crossing toggle-off is ignored; old-session packets cannot be accepted. The Mac rate-limits batches to at most two per second, while the client sends at most one batch per second.

Only newly generated `client.start`, `client.debug`, `network.link`, `connection.stream`, `video.*` and `display.*` entries are shareable. No historical disk-log read, screen pixels, clipboard text/images, pairing code or input events are exported. The client keeps at most 64 lines, discards oldest on overflow and reports the queue loss. Disable clears the queue. Diagnostic batches use a single replaceable low-priority control slot: keys/mouse/handshake take precedence, and session transition clears old diagnostics. Existing local file logging and Mac diagnostic-file writes run off the media/input threads.

Each `video.performance` window measures actual elapsed time and separate `receive_fps` (complete frames), `upload_fps` (raw GPU upload or decoder output), `present_fps` (successful live Present calls), and received video bytes / Gbps. It also includes busy drops, pending frame replacement, long Present intervals and the maximum interval in microseconds. `target_fps` is explicitly a target and is never substituted for measured FPS. Long intervals may reflect an idle source or hidden window and are not automatically counted as lost frames. `video.raw.receive` separately measures payload receive and exact reconstruction/copy work; existing raw upload, decoder, rendering and frame-age logs are shared too.

`dxgi_display_fps` is an optional DXGI GetFrameStatistics / PresentCount cross-check against compositor/monitor presentation, calculated using SyncQPCTime and QPC frequency. It is -1 when unavailable, disjoint, reset, stale, or without a valid baseline; it is separate from successful Present calls. DXGI statistics can be unreliable across multiple displays; no DwmFlush or extra synchronous GPU wait is introduced merely to obtain them. See [Microsoft GetFrameStatistics documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics) and [DXGI_FRAME_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics).

The Mac writes the accepted new log entries to `~/Library/Logs/ThunderDisplay/windows-client.log` (private 0600, two rotated files, about 2 MiB each) and unified logs under subsystem `dev.thunderdisplay.host`, category `windows-client`. Mac receive timestamps are kept separately from Windows log timestamps; no cross-machine clock subtraction is used. The LoginWindow host uses its own account log directory.
