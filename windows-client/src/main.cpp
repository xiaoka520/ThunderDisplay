#include "cursor.hpp"
#include "cursor_wire.hpp"
#include "setup.hpp"
#include "image_clipboard.hpp"
#include "../version.h"
#include <shellapi.h>
#include <commctrl.h>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <set>

namespace {
ClientOptions parse() {
    int argc; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) throw std::runtime_error("Cannot parse command line");
    std::vector<std::wstring> args(argv+1,argv+argc); LocalFree(argv);
    ClientOptions o; o.explicitSettings=!args.empty();
    for(size_t i=0;i<args.size();++i) {
        auto arg=utf8(args[i]);
        if(arg=="--help" || arg=="-h") {
            std::ostringstream help; help<<"ThunderDisplayClient [--host IPv4] [--token CODE] [--connect] [--mode auto|quality60|ultra120|low240]\n"
                <<"  [--token-file PATH] [--codec auto|hevc|h264] [--fps 1..240]\n"
                <<"  [--width 2560] [--height 1600] [--bitrate 120] [--port 47990]\n"
                <<"  [--display-pixels] [--no-clipboard] [--fullscreen] [--vsync] [--depth 0|8|10] [--lang auto|zh|en]\n"
                <<"Ctrl+Alt+Enter: fullscreen (configurable in setup). Ctrl+Alt+Shift+Esc: release/capture input. Close window: disconnect.\n";
            MessageBoxW(nullptr,wide(help.str()).c_str(),L"ThunderDisplay",MB_OK); std::exit(0);
        }
        if(arg=="--fullscreen") { o.fullscreen=true; continue; }
        if(arg=="--vsync") { o.vsync=true; continue; }
        if(arg=="--display-pixels") { o.nativePixels=false; continue; }
        if(arg=="--no-clipboard") { o.clipboard=false; continue; }
        if(arg=="--connect") { o.autoConnect=true; continue; }
        if(arg=="--pairing") { o.pairing=true; continue; }
        if(arg=="--no-pairing") { o.pairing=false; continue; }
        if(++i>=args.size()) throw std::runtime_error("Missing value for "+arg);
        auto value=utf8(args[i]);
        if(arg=="--width" || arg=="--height" || arg=="--fps" || arg=="--mode") o.autoQuality=false;
        auto number=[&](unsigned lo,unsigned hi) {
            size_t end=0; auto v=std::stoul(value,&end);
            if(end!=value.size() || v<lo || v>hi) throw std::runtime_error("Invalid value for "+arg); return unsigned(v);
        };
        if(arg=="--lang") {
            if(value=="auto") uiLanguage=0; else if(value=="zh") uiLanguage=1; else if(value=="en") uiLanguage=2;
            else throw std::runtime_error("--lang must be auto, zh or en");
        } else if(arg=="--host") o.host=value;
        else if(arg=="--token") { o.token=value; o.pairing=true; o.autoConnect=true; }
        else if(arg=="--token-file") { std::ifstream file{std::filesystem::path(args[i])}; if(!(file>>o.token)) throw std::runtime_error("Cannot read token file"); o.pairing=true; o.autoConnect=true; }
        else if(arg=="--port") o.port=uint16_t(number(1,65535));
        else if(arg=="--depth") { auto depth=number(0,10); if(depth!=0 && depth!=8 && depth!=10) throw std::runtime_error("--depth must be 0 (auto), 8 or 10"); o.colorDepth=uint8_t(depth); }
        else if(arg=="--width") o.settings.width=uint16_t(number(320,4096));
        else if(arg=="--height") o.settings.height=uint16_t(number(240,4096));
        else if(arg=="--fps") { o.settings.fps=uint16_t(number(1,240)); o.autoFrameRate=false; }
        else if(arg=="--bitrate") { o.settings.bitrate=uint64_t(number(10,20000))*1000000; o.customBitrate=true; }
        else if(arg=="--codec") {
            if(value=="auto") o.settings.codecMask=3; else if(value=="h264") o.settings.codecMask=1;
            else if(value=="hevc") o.settings.codecMask=2; else throw std::runtime_error("Unknown codec");
        } else if(arg=="--mode") {
            if(value=="auto") { o.autoQuality=true; }
            else if(value=="quality60") { o.autoFrameRate=false; o.settings.fps=60; o.settings.bitrate=80000000; }
            else if(value=="ultra120") { o.autoFrameRate=false; o.settings.fps=120; o.settings.bitrate=120000000; }
            else if(value=="low240") { o.autoFrameRate=false; o.settings.fps=240; o.settings.bitrate=150000000; }
            else throw std::runtime_error("Unknown display mode");
        } else throw std::runtime_error("Unknown option "+arg);
    }
    for(auto& c:o.token) c=char(std::tolower(static_cast<unsigned char>(c)));
    if(o.pairing && (o.token.size()!=32 || !std::all_of(o.token.begin(),o.token.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})))
        throw std::runtime_error("Supply the 32 character pairing code from the Mac setup window");
    if(o.settings.width%2 || o.settings.height%2) throw std::runtime_error("Resolution must be even");
    if(o.settings.fps<1 || o.settings.fps>240) throw std::runtime_error("FPS must be 1 to 240");
    if(!o.pairing) o.token.clear();
    return o;
}
struct App {
    HWND window=nullptr;
    WindowIcons icons;
    std::unique_ptr<SetupWindow> setup;
    bool openOnFirstFrame=false, fullscreenRequested=false;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ClientSession> session;
    std::set<uint16_t> pressed,physicalKeys,suppressedKeys,localKeys;
    uint16_t fullscreenHotkey=td::DefaultFullscreenHotkey;
    bool capture=true,fullscreen=false,remoteCaps=false;
    unsigned buttons=0;
    WINDOWPLACEMENT placement{};
    HHOOK hook=nullptr;
    bool renewKeyboardHook=true, keyboardHookFailed=false;
    HCURSOR localCursor=nullptr; UINT cursorDPI=0;
    struct NativeCursor { std::vector<td::PNGImage> images; double width,height,hotX,hotY; };
    std::optional<NativeCursor> nativeCursor;
    HCURSOR macCursor() {
        auto dpi=GetDpiForWindow(window);
        if(localCursor && cursorDPI==dpi) return localCursor;
        auto raster=td::macStyleCursor(dpi);
        if(nativeCursor) {
            const auto& source=*nativeCursor;
            std::vector<td::CursorDimensions> sizes;
            for(const auto& image:source.images) sizes.push_back({int(image.width),int(image.height)});
            const auto& image=source.images[td::bestCursorRepresentation(dpi,source.width,source.height,sizes)];
            std::vector<uint32_t> pixels(image.pixels.size()/4);
            std::memcpy(pixels.data(),image.pixels.data(),image.pixels.size());
            raster=td::rasterCursor(dpi,int(image.width),int(image.height),source.width,source.height,source.hotX,source.hotY,pixels);
        }
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=raster.size; info.bmiHeader.biHeight=-raster.size;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr; auto color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!color) return LoadCursorW(nullptr,IDC_ARROW);
        std::memcpy(bits,raster.pixels.data(),raster.pixels.size()*4);
        std::vector<uint8_t> mask(size_t(raster.size/8)*raster.size,255);
        for(int y=0;y<raster.size;++y) for(int x=0;x<raster.size;++x)
            if(raster.pixels[size_t(y)*raster.size+x]>>24) mask[size_t(y)*(raster.size/8)+x/8]&=uint8_t(~(0x80>>(x%8)));
        auto monochrome=CreateBitmap(raster.size,raster.size,1,1,mask.data());
        ICONINFO icon{}; icon.xHotspot=DWORD(raster.hotX); icon.yHotspot=DWORD(raster.hotY); icon.hbmMask=monochrome; icon.hbmColor=color;
        auto created=monochrome?static_cast<HCURSOR>(CreateIconIndirect(&icon)):nullptr;
        if(monochrome) DeleteObject(monochrome); DeleteObject(color);
        if(!created) return LoadCursorW(nullptr,IDC_ARROW);
        auto old=localCursor; localCursor=created; cursorDPI=dpi;
        SetCursor(created); if(old) DestroyCursor(old);
        return localCursor;
    }
    DWORD clipboardSequence=0;
    std::optional<std::string> pendingClipboard;
    std::optional<td::Bytes> pendingImage;
    std::optional<td::PNGImage> pendingImagePixels;
    uint64_t clipboardDeadline=0;
    bool clipboardListening=false;
    static App* instance;
    uint16_t flags() {
        uint16_t f=0;
        if(pressed.count(VK_LSHIFT)||pressed.count(VK_RSHIFT)) f|=2;
        if(pressed.count(VK_LCONTROL)||pressed.count(VK_RCONTROL)) f|=4;
        if(pressed.count(VK_LMENU)||pressed.count(VK_RMENU)) f|=8;
        if(pressed.count(VK_LWIN)||pressed.count(VK_RWIN)) f|=16;
        if(remoteCaps) f|=32; return f;
    }
    void release() {
        if(session) session->send(td::input(5,0,0)); pressed.clear(); buttons=0; ReleaseCapture();
    }
    bool ready() const { return session && !session->handingOver() && session->connected() && renderer && renderer->hasFrame(); }
    void refreshKeyboardHook() {
        // Windows can silently remove a low-level hook when its owner stalls
        // during device initialization. Renew once per stream and on focus.
        renewKeyboardHook=false;
        auto replacement=SetWindowsHookExW(WH_KEYBOARD_LL,App::keyboard,GetModuleHandleW(nullptr),0);
        keyboardHookFailed=!replacement;
        if(!replacement) return;
        auto previous=hook; hook=replacement;
        if(previous) UnhookWindowsHookEx(previous);
        // Preserve held/suppressed keys so their ups cannot leak to the shell.
    }
    void clipboardChanged() {
        if(!session || !session->clipboardEnabled()) return;
        auto sequence=GetClipboardSequenceNumber(); if(sequence==clipboardSequence) return;
        if(!OpenClipboard(window)) { SetTimer(window,7,100,nullptr); return; }
        clipboardSequence=sequence;
        if(session->imageClipboardEnabled()) {
            try {
                auto image=td::readClipboardPNG();
                if(!image.empty()) { session->sendClipboardImage(image);CloseClipboard();return; }
            } catch(const std::exception&) { /* Unsupported image: retain any copied text. */ }
        }
        auto memory=GetClipboardData(CF_UNICODETEXT);
        if(memory) {
            auto text=static_cast<const wchar_t*>(GlobalLock(memory));
            if(text) {
                auto size=std::min<size_t>(GlobalSize(memory)/sizeof(wchar_t),td::ClipboardLimit+1),length=size_t(0);
                if(size) {
                    while(length<size && text[length]) ++length;
                    if(length<size) session->sendClipboard(utf8(std::wstring(text,length)));
                }
                GlobalUnlock(memory);
            }
        }
        CloseClipboard();
    }
    void receiveClipboard() {
        if(!session || !session->clipboardEnabled()) { pendingClipboard.reset();pendingImage.reset();pendingImagePixels.reset(); return; }
        if(auto text=session->takeClipboard()) { pendingClipboard=std::move(*text);pendingImage.reset();pendingImagePixels.reset(); clipboardDeadline=micros()+2000000; }
        if(auto image=session->takeImage()) {
            try { pendingImagePixels=td::decodePNG(*image,false);pendingImage=std::move(*image);pendingClipboard.reset();clipboardDeadline=micros()+2000000; }
            catch(const std::exception&) { pendingImage.reset();pendingImagePixels.reset(); }
        }
        if(pendingImage && pendingImagePixels) {
            if(micros()>clipboardDeadline) { pendingImage.reset();pendingImagePixels.reset();return; }
            if(td::writeClipboardPNG(window,*pendingImage,*pendingImagePixels)) {
                clipboardSequence=GetClipboardSequenceNumber();pendingImage.reset();pendingImagePixels.reset();
            } else SetTimer(window,8,100,nullptr);
            return;
        }
        if(!pendingClipboard || micros()>clipboardDeadline) { pendingClipboard.reset(); return; }
        auto decoded=wide(*pendingClipboard); auto size=(decoded.size()+1)*sizeof(wchar_t);
        auto memory=GlobalAlloc(GMEM_MOVEABLE,size); if(!memory) return;
        auto data=GlobalLock(memory); if(!data) { GlobalFree(memory); return; }
        std::memcpy(data,decoded.c_str(),size); GlobalUnlock(memory);
        if(!OpenClipboard(window)) { GlobalFree(memory); SetTimer(window,8,100,nullptr); return; }
        if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)) { memory=nullptr; pendingClipboard.reset(); }
        clipboardSequence=GetClipboardSequenceNumber(); CloseClipboard();
        if(memory) { GlobalFree(memory); SetTimer(window,8,100,nullptr); }
    }
    std::wstring shortcutName() const {
        std::wstring name;
        auto mods=uint8_t(fullscreenHotkey>>8);
        if(mods&td::HotControl) name+=L"Ctrl+";
        if(mods&td::HotAlt) name+=L"Alt+";
        if(mods&td::HotShift) name+=L"Shift+";
        wchar_t keyName[64]{};
        auto scan=MapVirtualKeyW(uint8_t(fullscreenHotkey),MAPVK_VK_TO_VSC);
        if(GetKeyNameTextW(LONG((scan<<16)|((mods&HOTKEYF_EXT)?(1u<<24):0)),keyName,64)>0) name+=keyName;
        else name+=std::to_wstring(uint8_t(fullscreenHotkey));
        return name;
    }
    void title() {
        const std::string detail=session?session->currentStatus():"";
        std::wstring state=tr(L"等待连接",L"Ready to connect");
        if(session) {
            if(ready()) state=tr(L"已连接",L"Connected");
            else if(session->retryStopped()) state=tr(L"重连已停止，请手动重新连接。",L"Reconnection stopped. Connect again manually.");
            else if(session->interrupted()) state=tr(L"画面已中断，正在恢复…",L"Video interrupted. Recovering…");
            else if(session->connected()) state=tr(L"控制通道已建立，等待首帧…",L"Control channel established. Waiting for the first frame…");
            else if(detail.find("InputUnavailable:")==0) state=tr(L"Mac 键盘或鼠标发送失败",L"Mac keyboard or mouse input failed");
            else if(detail.find("PreLoginCaptureUnavailable:")==0) state=tr(L"Mac 登录界面捕获或控制未获授权",L"Mac login-screen capture or control is unavailable");
            else if(detail.find("PreLoginStarting:")==0) state=tr(L"正在检查 Mac 登录界面权限…",L"Checking Mac login-screen access…");
            else if(detail.find("HostWaitingForLogin:")==0) state=tr(L"Mac 登录界面组件未就绪…",L"Mac LoginWindow component is not ready…");
            else if(detail.find("Discovering")==0) state=tr(L"正在查找雷雳网桥上的 Mac…",L"Discovering a Mac on Thunderbolt Bridge…");
            else if(detail.empty() || detail.find("Connecting")==0) state=tr(L"正在连接 Mac…",L"Connecting to Mac…");
            else state=tr(L"连接失败，正在自动重试；可断开后修改设置。",L"Connection failed. Retrying automatically; disconnect to change settings.");
        }
        if(session && !ready() && session->attempts()) state+=std::wstring(tr(L" · 尝试 ",L" · Attempt "))+std::to_wstring(session->attempts())+L"/"+std::to_wstring(session->totalAttempts());
        if(setup && session && session->retryStopped()) setup->setActive(false);
        std::wstring description=wide(detail);
        if(session && session->retryStopped()) description=tr(L"已达到 10 分钟或 150 次尝试上限，自动重连已停止。点击“连接 Mac”可重新开始。",L"The 10-minute or 150-attempt limit was reached. Automatic recovery stopped. Click Connect to Mac to try again.");
        else if(detail.find("InputUnavailable:")==0) description=tr(L"Mac 系统拒绝了键鼠事件，已断开并释放输入；诊断区显示具体错误。请更新 Mac 的开机组件并检查控制权限。",L"macOS rejected keyboard or mouse input. The session was closed and held input released. Diagnostics retain the exact error. Update Mac startup components and check control access.");
        else if(detail.find("PreLoginCaptureUnavailable:")==0) description=tr(L"Mac 登录界面组件已报告捕获或输入失败；系统发现服务仍在运行。诊断区保留系统错误。",L"The Mac LoginWindow component reported capture or input failure. Discovery remains active; diagnostics retain the system error.");
        else if(detail.find("PreLoginStarting:")==0) description=tr(L"正在验证登录界面的有效画面与输入授权，成功后自动重连。",L"Verifying a valid login-screen frame and input authorization. Reconnection is automatic.");
        else if(detail.find("HostWaitingForLogin:")==0) description=tr(L"系统发现服务正在运行，但登录界面组件尚未就绪。请检查 Mac 的开机组件安装状态。",L"System discovery is active but the LoginWindow component is not ready. Check startup component installation on the Mac.");
        else if(detail.find("Video stalled:")==0) description=tr(L"视频持续中断，正在重新建立串流；旧画面已清除。",L"Video stalled. Re-establishing the stream; the old image has been cleared.");
        else if(detail.find("No host discovered")==0) description=tr(L"未找到 Mac。请确认两端网桥 IPv4 在同一网段，Mac 服务已启动；也可手动填写 Mac 地址。",L"No Mac found. Check bridge IPv4 addresses are on the same subnet and the Mac host is running, or enter its address manually.");
        else if(detail.find("No active Ethernet")==0) description=tr(L"没有可用的雷雳 / 以太网 IPv4 接口。请先连接雷雳线并配置两端网络地址。",L"No active Thunderbolt / Ethernet IPv4 interface. Connect the cable and configure network addresses on both devices.");
        else if(detail=="Host disconnected") description=tr(L"Mac 断开了连接。请确认配对码正确、Mac 两项权限已授权、服务正在运行。",L"The Mac disconnected. Check the pairing code, both Mac permissions, and that the host is running.");
        else if(detail=="Host is unreachable" || detail=="TCP connect failed" || detail=="TCP connection timed out") description=tr(L"无法连接 Mac。请核对 IP 和端口，并检查网桥连接、防火墙与 Mac 服务状态。",L"Cannot connect to the Mac. Check the IP, port, bridge link, firewall, and host status.");
        else if(detail.find("10-bit SDR requires")==0) description=tr(L"强制 10-bit 需要 Mac 支持 HEVC Main10，并选择 HEVC 编码。可改为自动或 8-bit SDR。",L"10-bit SDR requires a Main10-capable Mac and HEVC. Choose Auto or 8-bit SDR.");
        else if(detail.find("Main10Unavailable:")==0) description=std::wstring(tr(L"10-bit 捕获 / 编码不可用：",L"10-bit capture / encoding unavailable: "))+wide(detail.substr(18));
        else if(detail.find("10-bit unavailable")==0) description=std::wstring(tr(L"10-bit 链路不可用，正在改用 8-bit SDR。\r\n",L"10-bit unavailable; reconnecting with 8-bit SDR.\r\n"))+wide(detail);
        else if(detail.find("HEVC unavailable")==0) description=std::wstring(tr(L"HEVC 解码不可用，正在改用 H.264。\r\n",L"HEVC decoding unavailable; reconnecting with H.264.\r\n"))+wide(detail);
        else if(detail.find("Input queue overflow")==0) description=tr(L"输入队列已满，正在重连并释放按键。",L"Input queue full. Reconnecting and releasing held keys.");
        else if(detail=="Host timed out") description=tr(L"Mac 响应超时，请检查链路、Mac 服务或休眠状态。",L"The Mac timed out. Check the link, host, and sleep state.");
        else if(detail.find("PairingRequired:")==0) description=tr(L"此 Mac 已开启配对码验证。请断开连接，勾选配对码并填写 Mac 的配对码后重试。",L"This Mac requires pairing. Disconnect, enable pairing, enter the Mac code, and retry.");
        else if(session && session->connected()) {
            auto pos=description.find(L"target ");
            if(pos!=std::wstring::npos) description.replace(pos,7,tr(L"目标 ",L"target "));
        }
        if(keyboardHookFailed && ready() && capture) {
            state=tr(L"键盘捕获失败，请切回此窗口重试",L"Keyboard capture failed. Refocus this window to retry");
            description+=std::wstring(L"\n")+tr(L"Windows 无法重新安装键盘捕获，鼠标连接仍在运行。",L"Windows could not reinstall keyboard capture. The mouse connection is still active.");
        }
        const std::pair<const wchar_t*,const wchar_t*> diagnostics[]={
            {L"Video interrupted; requesting a fresh keyframe",L"画面中断，正在请求新的关键帧"},
            {L"Hardware encoder bitrate limit: requested ",L"硬件编码器码率限制：请求 "}, {L" Mbps, accepted ",L" Mbps，实际接受 "},
            {L"Scaling: ",L"画面缩放："}, {L"no sharpening",L"不锐化"}, {L"Video processor compatibility",L"兼容缩放"}, {L"fallback: ",L"回退原因："},
            {L"Clipboard active: yes",L"剪贴板同步：已开启"}, {L"Clipboard active: no",L"剪贴板同步：未开启"},
            {L"Text clipboard: requested (64 KiB)",L"文本剪贴板：已请求同步（最大 64 KiB）"}, {L"Text clipboard: unavailable on this Mac host",L"文本剪贴板：此 Mac 版本不支持"}, {L"Text clipboard: disabled",L"文本剪贴板：已关闭"},
            {L"Cursor: live macOS system cursor (video cursor hidden)",L"指针：实时 macOS 系统指针（串流指针已隐藏）"},
            {L"Cursor: video (native cursor access unavailable on this Mac)",L"指针：使用串流指针（这台 Mac 无法读取实时原生指针）"},
            {L"Cursor: video (update Mac to 0.7.0 for live system cursors)",L"指针：串流指针（动态系统指针需更新 Mac 至 0.7.0）"},
            {L"HostWaitingForLogin: Mac boot service is running. Log in on the Mac to start desktop capture.",L"Mac 开机服务已运行，正在等待用户登录；登录后将自动重连桌面主机。"},
            {L"PreLoginCaptureUnavailable: ",L"登录界面捕获或输入不可用："},
            {L"InputUnavailable: ",L"键鼠输入不可用："},
            {L"PreLoginStarting: Checking LoginWindow screen capture and input access.",L"正在检查登录界面的屏幕捕获与输入权限。"},
            {L"HostWaitingForLogin: LoginWindow agent unavailable. Log in or check pre-login component installation.",L"登录界面组件未就绪；请检查开机组件安装。"},
            {L"Cursor: video",L"指针：串流指针"},
            {L"Bitrate above 300 Mbps requires Mac Host 0.6.2 or later. Update the Mac or lower the custom bitrate.",L"超过 300 Mbps 需 Mac 主机 0.6.2 或更新版本，请更新 Mac 或降低自定义码率。"},
            {L"Native pixels unavailable; reconnecting at display resolution: ",L"原始像素模式不可用；正在按 Windows 分辨率重连："},
            {L"Last fallback: ",L"本次连接的回退记录："}, {L"10-bit unavailable; reconnecting with 8-bit SDR: ",L"10-bit 不可用，已回退 8-bit SDR："},
            {L"HEVC unavailable; reconnecting with H.264: ",L"HEVC 不可用，已回退 H.264："},
            {L"Color pipeline: ",L"色彩链路："}, {L"Stream pixels: ",L"串流像素："}, {L"Viewport: ",L"显示像素："}, {L"Display scale: ",L"显示缩放："}, {L"Waiting for conversion",L"等待画面转换"},
            {L"sRGB SDR / BT.709 matrix",L"sRGB SDR / BT.709 矩阵"}, {L"Explicit sRGB shader conversion",L"显式 sRGB 转换"},
            {L"Bilinear compatibility",L"兼容双线性缩放"},
            {L"Desktop color unavailable; reconnecting with legacy color: ",L"桌面色彩转换不可用，正在以兼容色彩重连："},
            {L"Explicit DXGI conversion",L"显式色彩转换"}, {L"Legacy conversion",L"兼容色彩转换"},
            {L"Mac display: ",L"Mac 显示器："}, {L"Auto quality negotiated: ",L"自动画质协商："},
            {L"No video packets; check the Mac capture and UDP firewall",L"未收到视频包；请检查 Mac 捕获状态和 UDP 防火墙"},
            {L"Video packets received; waiting for a complete keyframe",L"已收到视频包，等待完整关键帧"},
            {L"Keyframe received; decoder has not accepted input",L"已收到关键帧，解码器尚未接收输入"},
            {L"Compressed frames submitted; decoder has not produced output",L"已提交压缩帧，解码器尚未输出画面"},
            {L"Decoded frames ready; restore the remote display window",L"已解码画面，请恢复远程显示窗口"},
            {L"Video playing",L"画面传输正常"}, {L"First frame timed out: ",L"首帧超时，正在重连："},
            {L"Video packets: ",L"视频包："}, {L"Complete frames: ",L"完整帧："},
            {L"Submitted: ",L"提交解码："}, {L"Decoded: ",L"解码输出："}
        };
        for(auto& entry:diagnostics) {
            size_t pos=0; auto replacement=std::wstring(tr(entry.second,entry.first));
            while((pos=description.find(entry.first,pos))!=std::wstring::npos) {
                description.replace(pos,wcslen(entry.first),replacement); pos+=replacement.size();
            }
        }
        auto logPath=ClientDiagnostics::instance().path();
        if(!logPath.empty()) description+=std::wstring(tr(L"\r\n诊断日志：",L"\r\nDiagnostics log: "))+logPath;
        if(setup) setup->status(state,description);
        auto caption=std::wstring(L"ThunderDisplay " TD_VERSION_WIDE L" | ")+state+L" | "+shortcutName()+tr(L" 全屏 · Ctrl+Alt+Shift+Esc 释放输入",L" fullscreen · Ctrl+Alt+Shift+Esc release input");
        if(!capture) caption+=tr(L" | 输入已释放",L" | Input released");
        SetWindowTextW(window,caption.c_str());
        if(!ready()) InvalidateRect(window,nullptr,FALSE);
    }
    void connect(ClientOptions options) {
        disconnect(false);
        capture=true; remoteCaps=(GetKeyState(VK_CAPITAL)&1)!=0; fullscreenHotkey=options.fullscreenHotkey; fullscreenRequested=options.fullscreen; openOnFirstFrame=true;
        try {
            if(options.autoQuality && options.displayBounds.right<=options.displayBounds.left) {
                auto screens=detectDisplays();
                if(screens.empty()) throw std::runtime_error("No display found. Use manual settings or refresh display detection.");
                options.display=screens.front().preferred; options.displayBounds=screens.front().bounds; options.displayBits=screens.front().bits;
            }
            if(options.displayBounds.right>options.displayBounds.left && options.displayBounds.bottom>options.displayBounds.top) {
                auto& b=options.displayBounds;
                SetWindowPos(window,nullptr,b.left+40,b.top+40,std::max<LONG>(480,std::min<LONG>(1280,b.right-b.left-80)),
                    std::max<LONG>(320,std::min<LONG>(800,b.bottom-b.top-80)),SWP_NOZORDER);
            }
            renderer=std::make_unique<Renderer>(window,options.vsync); renderer->setPixelExact(options.pixelExact); renderer->setScalingQuality(options.scalingQuality);
            session=std::make_unique<ClientSession>(options,*renderer,window);
            setup->setActive(true); title(); showDisplay(); session->start();
        } catch(const std::exception& e) {
            disconnect(false);
            setup->status(tr(L"无法启动远程显示",L"Unable to start remote display"),wide(e.what()));
            setup->present();
        }
    }
    void disconnect(bool showSetup=true) {
        renewKeyboardHook=true;
        release(); pendingClipboard.reset();pendingImage.reset();pendingImagePixels.reset();nativeCursor.reset();cursorDPI=0; KillTimer(window,7); KillTimer(window,8);
        if(session) session->stop();
        session.reset(); renderer.reset();
        // Remove notifications from the old worker before a new session can start.
        MSG queued{}; while(PeekMessageW(&queued,window,StatusMessage,CursorMessage,PM_REMOVE)) {}
        if(fullscreen) toggleFullscreen();
        ShowWindow(window,SW_HIDE); openOnFirstFrame=false;
        if(setup) { setup->setActive(false); title(); if(showSetup) setup->present(); }
    }
    void showDisplay() {
        if(!session) return;
        ShowWindow(window,SW_RESTORE); SetForegroundWindow(window);
        remoteCaps=(GetKeyState(VK_CAPITAL)&1)!=0; title();
    }
    void toggleFullscreen() {
        if(!fullscreen) {
            placement.length=sizeof(placement); GetWindowPlacement(window,&placement);
            MONITORINFO m{}; m.cbSize=sizeof(m); GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&m);
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);
            SetWindowPos(window,HWND_TOP,m.rcMonitor.left,m.rcMonitor.top,m.rcMonitor.right-m.rcMonitor.left,m.rcMonitor.bottom-m.rcMonitor.top,SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE); SetWindowPlacement(window,&placement);
            SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
        }
        fullscreen=!fullscreen;
    }
    uint16_t physicalFlags() const {
        uint16_t f=0;
        if(physicalKeys.count(VK_LSHIFT)||physicalKeys.count(VK_RSHIFT)) f|=2;
        if(physicalKeys.count(VK_LCONTROL)||physicalKeys.count(VK_RCONTROL)) f|=4;
        if(physicalKeys.count(VK_LMENU)||physicalKeys.count(VK_RMENU)) f|=8;
        if(physicalKeys.count(VK_LWIN)||physicalKeys.count(VK_RWIN)) f|=16;
        if(remoteCaps) f|=32;
        return f;
    }
    static LRESULT CALLBACK keyboard(int code,WPARAM w,LPARAM l) {
        auto* a=instance;
        if(code<0 || !a) return CallNextHookEx(nullptr,code,w,l);
        auto* k=reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
        const bool down=w==WM_KEYDOWN || w==WM_SYSKEYDOWN, up=w==WM_KEYUP || w==WM_SYSKEYUP;
        if(!down && !up) return CallNextHookEx(nullptr,code,w,l);
        const uint16_t key=td::keyboardKey(uint16_t(k->vkCode),k->scanCode,(k->flags&LLKHF_EXTENDED)!=0);
        const bool firstDown=down && a->physicalKeys.insert(key).second;
        if(up) a->physicalKeys.erase(key);
        auto modifiers=a->physicalFlags();
        // Finish swallowing keys whose downs belonged to the remote display,
        // even after focus or fullscreen changes. A stray Win key-up opens Start.
        if(up) {
            const bool local=a->localKeys.erase(key)!=0,blocked=a->suppressedKeys.erase(key)!=0;
            if(local) return 1;
            if(blocked) {
                if(GetForegroundWindow()==a->window && a->capture && a->ready()) PostMessageW(a->window,KeyboardMessage,key,modifiers);
                return 1;
            }
        }
        if(GetForegroundWindow()!=a->window) return CallNextHookEx(nullptr,code,w,l);
        if(down && a->localKeys.count(key)) return 1; // Auto-repeat must not toggle twice.
        if(down && td::matchesFullscreenHotkey(a->fullscreenHotkey,key,modifiers)) {
            a->localKeys.insert(key); PostMessageW(a->window,ReleaseInputMessage,1,0); return 1;
        }
        if(!a->ready()) return CallNextHookEx(nullptr,code,w,l);
        if(down && key==VK_ESCAPE && (modifiers&30)==14) {
            a->localKeys.insert(key); PostMessageW(a->window,ReleaseInputMessage,2,0); return 1;
        }
        if(!a->capture) return CallNextHookEx(nullptr,code,w,l);
        // The hook runs before Windows updates its state. Predict this one
        // transition for the remote packet; repeats must not toggle it again.
        if(key==VK_CAPITAL && firstDown) { a->remoteCaps=!a->remoteCaps; modifiers^=32; }
        if(a->fullscreen && td::taskManagerShortcut(key,modifiers)) {
            if(down) { a->localKeys.insert(key); PostMessageW(a->window,ReleaseInputMessage,3,0); }
            return 1; // Launch on the app thread; Ctrl/Shift stay intercepted so language-switch and Sticky Keys shortcuts cannot leak.
        }
        if(td::localDesktopShortcut(a->fullscreen,key,modifiers)) {
            if(down) PostMessageW(a->window,ReleaseInputMessage,0,0);
            return CallNextHookEx(nullptr,code,w,l); // Task Manager modifiers remain visible to Windows.
        }
        const bool modifier=td::localModifier(a->fullscreen,key);
        PostMessageW(a->window,KeyboardMessage,key,modifiers|(down?1:0));
        if(modifier || up) return CallNextHookEx(nullptr,code,w,l);
        a->suppressedKeys.insert(key);
        return 1; // Includes Win, Alt+Tab, Alt+F4 and shell shortcuts during focused fullscreen.
    }
    bool mousePoint(LPARAM l,int32_t& x,int32_t& y) {
        return renderer->pointerPosition(short(LOWORD(l)),short(HIWORD(l)),buttons!=0,x,y);
    }
    LRESULT message(UINT m,WPARAM w,LPARAM l) {
        switch(m) {
        case KeyboardMessage:
            if(capture && ready() && GetForegroundWindow()==window) {
                auto key=uint16_t(w),inputFlags=uint16_t(l);
                if(inputFlags&1) pressed.insert(key); else pressed.erase(key);
                session->send(td::input(3,key,inputFlags));
            }
            return 0;
        case ReleaseInputMessage:
            release();
            if(w==1) toggleFullscreen();
            if(w==2) {
                capture=!capture;
                if(capture) remoteCaps=(GetKeyState(VK_CAPITAL)&1)!=0;
            }
            if(w==3) {
                wchar_t directory[MAX_PATH]{}; auto length=GetSystemDirectoryW(directory,MAX_PATH);
                const auto executable=std::wstring(directory)+L"\\Taskmgr.exe";
                if(!length || length>=MAX_PATH || reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",executable.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32) {
                    capture=false;
                    if(setup) { setup->status(tr(L"无法打开任务管理器，已释放输入。可使用 Ctrl+Alt+Del。",L"Task Manager could not open. Input released; use Ctrl+Alt+Del.")); setup->present(); }
                    SetWindowTextW(window,tr(L"ThunderDisplay · 输入已释放",L"ThunderDisplay · Input released")); return 0;
                }
            }
            title(); return 0;
        case WM_CLOSE: disconnect(); return 0;
        case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: td::UITheme::system().apply(window); return 0;
        case CursorMessage:
            if(session) if(auto payload=session->takeCursor()) {
                try {
                    td::CursorPayload decoded(*payload);
                    std::vector<td::PNGImage> images;
                    for(const auto& png:decoded.images) images.push_back(td::decodePNG(png,true,1024,1024*1024));
                    nativeCursor=NativeCursor{std::move(images),decoded.width,decoded.height,decoded.hotX,decoded.hotY};cursorDPI=0;
                    POINT point{};GetCursorPos(&point);RECT area{};GetWindowRect(window,&area);
                    if(capture && ready() && GetForegroundWindow()==window && PtInRect(&area,point)) SetCursor(macCursor());
                } catch(const std::exception& error) { if(setup) setup->status(tr(L"系统指针同步失败",L"System cursor synchronization failed"),wide(error.what())); }
            }
            return 0;
        case WM_DESTROY: return 0;
        case WM_SETFOCUS:
            remoteCaps=(GetKeyState(VK_CAPITAL)&1)!=0;
            if(ready()) refreshKeyboardHook(); else renewKeyboardHook=true;
            return 0;
        case WM_KILLFOCUS: release(); return 0;
        case WM_CLIPBOARDUPDATE: clipboardChanged(); return 0;
        case WM_TIMER:
            if(w==7) { KillTimer(window,7); clipboardChanged(); return 0; }
            if(w==8) { KillTimer(window,8); receiveClipboard(); return 0; } break;
        case ClipboardMessage:
            if(w==1) clipboardSequence=GetClipboardSequenceNumber();
            else receiveClipboard(); return 0;
        case WM_CAPTURECHANGED: if(buttons) release(); return 0;
        case StatusMessage: title(); return 0;
        case DisconnectedMessage: renewKeyboardHook=true;nativeCursor.reset();cursorDPI=0;release(); title(); if(setup && GetForegroundWindow()==window && !ready() && !(session && session->handingOver())) setup->present(); return 0;
        case FramePresentedMessage:
            if(ready() && renewKeyboardHook) refreshKeyboardHook();
            if(ready() && openOnFirstFrame) {
                openOnFirstFrame=false; showDisplay(); if(fullscreenRequested && !fullscreen) toggleFullscreen();
            }
            title(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps);
            bool image=false;
            if(renderer) {
                try { image=renderer->repaintImage(); }
                catch(const std::exception& e) { diagnosticLog("display.repaint.error",e.what()); }
            }
            // A newly presented desktop frame may precede the worker's input
            // handover flag update. Do not paint black over those valid pixels.
            if(!(renderer && renderer->hasImage()) && !image) {
                RECT r{}; GetClientRect(window,&r); FillRect(dc,&r,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(220,220,220));
                auto text=std::wstring(tr(L"等待远程画面…\n连接状态与详细信息见设置窗口。",L"Waiting for remote display…\nSee the setup window for connection status and details."));
                DrawTextW(dc,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
            }
            EndPaint(window,&ps); return 0;
        }
        case WM_SIZE:
            if(renderer) try { renderer->resize(); } catch(const std::exception& e) { diagnosticLog("display.resize.error",e.what()); }
            return 0;
        case WM_DPICHANGED: icons.apply(window); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR:
            if(LOWORD(l)==HTCLIENT && capture && ready()) { SetCursor(session->localCursorEnabled()?macCursor():nullptr); return TRUE; }
            break;
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            if(!capture || !ready()) break;
            int32_t x,y; if(!mousePoint(l,x,y)) break;
            if(m==WM_MOUSEMOVE) session->send(td::input(1,0,flags(),x,y));
            else {
                const uint16_t button=(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP)?0:(m==WM_RBUTTONDOWN||m==WM_RBUTTONUP)?1:2;
                const bool down=m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_MBUTTONDOWN;
                session->send(td::input(2,button,flags()|(down?1:0),x,y));
                if(down) { buttons|=1u<<button; SetCapture(window); } else { buttons&=~(1u<<button); if(!buttons) ReleaseCapture(); }
            }
            return 0;
        }
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            if(capture && ready()) {
                const int32_t delta=int32_t(short(HIWORD(w)))*48/WHEEL_DELTA;
                session->send(td::input(4,0,flags(),m==WM_MOUSEHWHEEL?delta:0,m==WM_MOUSEWHEEL?delta:0)); return 0;
            }
            break;
        }
        return DefWindowProcW(window,m,w,l);
    }
    static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* a=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE) { a=reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); a->window=h; SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a)); }
        return a?a->message(m,w,l):DefWindowProcW(h,m,w,l);
    }
    ~App() { if(clipboardListening) RemoveClipboardFormatListener(window); if(hook) UnhookWindowsHookEx(hook); if(session) session->stop(); session.reset(); renderer.reset(); if(window) DestroyWindow(window); setup.reset(); if(localCursor) { SetCursor(LoadCursorW(nullptr,IDC_ARROW)); DestroyCursor(localCursor); } instance=nullptr; }
};
App* App::instance=nullptr;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    diagnosticLog("client.launch");
    SetConsoleOutputCP(CP_UTF8); SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    td::UITheme::enableNativeAppTheme();
    bool winsock=false,mf=false,com=false;
    try {
        auto options=parse();
        INITCOMMONCONTROLSEX controls{}; controls.dwSize=sizeof(controls); controls.dwICC=ICC_STANDARD_CLASSES|ICC_HOTKEY_CLASS;
        if(!InitCommonControlsEx(&controls)) throw std::runtime_error("Common controls initialization failed");
        WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("Winsock startup failed"); winsock=true;
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM startup"); com=true;
        check(MFStartup(MF_VERSION,MFSTARTUP_FULL),"Media Foundation startup"); mf=true;
        {
            App app; App::instance=&app;
            WNDCLASSW wc{}; wc.lpfnWndProc=App::wndproc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"ThunderDisplay";
            wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(IDI_THUNDERDISPLAY));
            if(!RegisterClassW(&wc)) throw std::runtime_error("Window class registration failed");
            HWND h=CreateWindowExW(0,wc.lpszClassName,L"ThunderDisplay",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,wc.hInstance,&app);
            if(!h) throw std::runtime_error("Window creation failed");
            app.icons.apply(h);
            td::UITheme::system().apply(h);
            app.clipboardListening=AddClipboardFormatListener(h)!=FALSE;
            app.setup=std::make_unique<SetupWindow>(options); app.fullscreenHotkey=app.setup->fullscreenShortcut();
            app.setup->onConnect=[&app](ClientOptions o){app.connect(std::move(o));};
            app.setup->onDisconnect=[&app]{app.disconnect(false);};
            app.setup->onShowDisplay=[&app]{app.showDisplay();};
            app.setup->onLanguage=[&app]{app.title();};
            app.setup->onShortcut=[&app](uint16_t shortcut){app.fullscreenHotkey=shortcut; app.title();};
            app.hook=SetWindowsHookExW(WH_KEYBOARD_LL,App::keyboard,wc.hInstance,0);
            if(!app.hook) throw std::runtime_error("Keyboard hook failed");
            app.setup->present();
            // Only an explicit CLI token starts streaming automatically.
            if(options.autoConnect) app.connect(options);
            MSG msg{};
            while(GetMessageW(&msg,nullptr,0,0)>0) {
                wchar_t kind[32]{}; if(msg.hwnd) GetClassNameW(msg.hwnd,kind,32);
                if(_wcsicmp(kind,HOTKEY_CLASSW)!=0 && _wcsicmp(kind,L"ComboBox")!=0 && msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && GetAncestor(msg.hwnd,GA_ROOT)==app.setup->window && !app.session) {
                    SendMessageW(app.setup->window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0); continue;
                }
                if(app.setup->handleDialogMessage(msg)) continue;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
        }
        MFShutdown(); CoUninitialize(); WSACleanup(); return 0;
    } catch(const std::exception& e) {
        std::cerr<<"ThunderDisplay: "<<e.what()<<std::endl;
        auto message=std::wstring(tr(L"启动失败：\n",L"Unable to start:\n"))+wide(e.what());
        MessageBoxW(nullptr,message.c_str(),L"ThunderDisplay",MB_OK|MB_ICONERROR);
        if(mf) MFShutdown(); if(com) CoUninitialize(); if(winsock) WSACleanup(); return 1;
    }
}
