# 独立本地持久捕获资格实验

`python3 scripts/probe-persistent-capture.py` 使用项目现有本地签名生成两个独立测试 App，输出到 `build/tests/persistent-capture/`。正式 App 和 dist 不受影响。

对两个 App 的 `Contents/MacOS/PermissionProbe` 分别传入 `--preflight`，先判断进程能否启动。此模式不会请求授权；`ordinaryScreenPreflight` 仅对应普通屏幕录制，不能证明持久权限。

2026-10-05 实测：无资格的对照 App 正常退出 0；带 `com.apple.developer.persistent-content-capture` 的 App 被 SIGKILL，未进入程序。系统 AMFI / taskgated-helper 日志确认受限资格缺少匹配的 provisioning profile。codesign 严格结构校验通过不等于系统允许使用资格。

仅当进程成功启动后才有必要运行 GUI 的 `--request`。此流程由用户完成系统授权，只检查 ScreenCaptureKit 是否收到一帧，不读取或保存图像像素、不传输画面、不注入输入、不注册登录项。成功收到普通捕获帧也不能单独证明“远程桌面”授权，仍须核对系统分类。

Apple 要求先申请并获批，再按其说明将资格加入 app profile：[Persistent Content Capture](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.persistent-content-capture)。当前结果不需要修改系统信任、TCC 数据库或安全策略。
