#include "setup.hpp"
#include "setup_layout.hpp"
#include "../version.h"
#include <wincrypt.h>
#include <cwctype>
#include <cctype>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

int uiLanguage=0;
const wchar_t* tr(const wchar_t* chinese,const wchar_t* english) {
    bool zh=uiLanguage==1 || (uiLanguage==0 && PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE);
    return zh?chinese:english;
}
std::wstring wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n); return out;
}
std::string utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr); return out;
}
namespace {
enum { Heading=10,Intro,HostLabel,Host,HostHint,UsePairing,Code,ShowCode,Remember,
    ModeLabel,Mode,CodecLabel,Codec,WidthLabel,Width,HeightLabel,Height,
    RateLabel,Rate,PortLabel,Port,Fullscreen,Vsync,Connect=IDOK,Disconnect=100,ShowDisplay,
    Status,Detail,Shortcuts,LanguageLabel,Language,ScreenLabel,Screen,Detect,DisplayInfo,StreamInfo,DisplaySection,ConnectionSection,QualitySection,DiagnosticSection,AutoHint,HotkeyLabel,Hotkey,PixelExact,DepthLabel,Depth,NativePixels,ScalingLabel,Scaling,Clipboard,LocalCursor,RateValue,RateAuto,RateMax,LinkInfo,RateUnit,DebugLogs,LogResize };
const wchar_t* registryPath=L"Software\\ThunderDisplay\\Client";
std::wstring value(HWND h) {
    int n=GetWindowTextLengthW(h); std::wstring s(size_t(n)+1,0); GetWindowTextW(h,s.data(),n+1); s.resize(n);
    auto begin=s.find_first_not_of(L" \t\r\n"),end=s.find_last_not_of(L" \t\r\n");
    return begin==std::wstring::npos?L"":s.substr(begin,end-begin+1);
}
void select(HWND h,int index) { SendMessageW(h,CB_SETCURSEL,index,0); }
int selection(HWND h) { return int(SendMessageW(h,CB_GETCURSEL,0,0)); }
void checkBox(HWND h,bool yes) { SendMessageW(h,BM_SETCHECK,yes?BST_CHECKED:BST_UNCHECKED,0); }
bool checked(HWND h) { return SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED; }
DWORD readNumber(HKEY key,const wchar_t* name,DWORD fallback) {
    DWORD result=0,size=sizeof(result),type=0;
    return RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&result),&size)==ERROR_SUCCESS && type==REG_DWORD && size==sizeof(result)?result:fallback;
}
std::wstring readString(HKEY key,const wchar_t* name) {
    wchar_t text[256]{}; DWORD size=sizeof(text),type=0;
    if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text),&size)!=ERROR_SUCCESS || type!=REG_SZ) return L"";
    text[255]=0; return text;
}
void putNumber(HKEY key,const wchar_t* name,DWORD number) { RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&number),sizeof(number)); }
std::wstring shortcutText(uint16_t shortcut) {
    std::wstring text; const auto mods=uint8_t(shortcut>>8);
    if(mods&HOTKEYF_CONTROL) text+=L"Ctrl + ";
    if(mods&HOTKEYF_ALT) text+=L"Alt + ";
    if(mods&HOTKEYF_SHIFT) text+=L"Shift + ";
    wchar_t key[64]{}; const auto scan=MapVirtualKeyW(uint8_t(shortcut),MAPVK_VK_TO_VSC);
    if(GetKeyNameTextW(LONG((scan<<16)|((mods&HOTKEYF_EXT)?(1u<<24):0)),key,64)>0) text+=key;
    else text+=std::to_wstring(uint8_t(shortcut));
    return text;
}
std::wstring linkRateText(uint64_t rate) {
    if(!td::knownLinkRate(rate)) return tr(L"未知",L"Unknown");
    if(rate%1000000000==0) return std::to_wstring(rate/1000000000)+L" Gbps";
    return std::to_wstring(rate/1000000)+L" Mbps";
}
}
SetupWindow::SetupWindow(ClientOptions initial): saved(std::move(initial)) {
    loadPreferences();
    selectedBitrate=saved.customBitrate?saved.settings.bitrate:0; bitrateMaximum=saved.customBitrate && saved.bitrateMaximum;
    WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"ThunderDisplaySetup";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=nullptr;
    wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(IDI_THUNDERDISPLAY));
    background=CreateSolidBrush(theme.background); cardBackground=CreateSolidBrush(theme.card); fieldBackground=CreateSolidBrush(theme.field);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register setup window");
    window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"ThunderDisplay",WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,840,960,nullptr,nullptr,wc.hInstance,this);
    if(!window) throw std::runtime_error("Cannot create setup window");
    icons.apply(window);
    wc.lpszClassName=L"ThunderDisplaySetupContent"; wc.lpfnWndProc=contentProcedure;
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register setup content");
    content=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_COMPOSITED,wc.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,1,1,window,nullptr,wc.hInstance,this);
    if(!content) throw std::runtime_error("Cannot create setup content");
    dpi=GetDpiForWindow(window);
    DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND; DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    build(); updateTheme(); refreshDisplays(); translate(); updateQualityControls(); layout();
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int w=std::min(MulDiv(840,int(dpi),96),int(work.right-work.left)),h=std::min(MulDiv(960,int(dpi),96),int(work.bottom-work.top));
    SetWindowPos(window,nullptr,work.left+(work.right-work.left-w)/2,work.top+(work.bottom-work.top-h)/2,w,h,SWP_NOZORDER);
    status(tr(L"等待连接",L"Ready to connect")); setActive(false);
    SetTimer(window,11,250,nullptr); pollNetworkLink();
}
SetupWindow::~SetupWindow() {
    if(window && IsWindow(window)) DestroyWindow(window);
    if(font) DeleteObject(font); if(headingFont) DeleteObject(headingFont); if(sectionFont) DeleteObject(sectionFont); if(hintFont) DeleteObject(hintFont);
    if(background) DeleteObject(background); if(cardBackground) DeleteObject(cardBackground); if(fieldBackground) DeleteObject(fieldBackground);
}
HWND SetupWindow::add(int id,const wchar_t* kind,DWORD style,int x,int y,int w,int h,const wchar_t* text) {
    const bool combo=_wcsicmp(kind,L"COMBOBOX")==0;
    if(combo) style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
    auto child=CreateWindowExW(0,kind,text,
        WS_CHILD|WS_VISIBLE|style,0,0,1,1,content,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    if(!child) throw std::runtime_error("Cannot create setup control");
    theme.apply(child);
    if(combo) SetWindowSubclass(child,comboProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    if(_wcsicmp(kind,L"EDIT")==0) SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
    controls.push_back({child,x,y,w,h,nullptr,0,0,_wcsicmp(kind,L"EDIT")==0 || _wcsicmp(kind,L"ComboBox")==0 || _wcsicmp(kind,HOTKEY_CLASSW)==0}); fields[id]=child; return child;
}
void SetupWindow::build() {
    add(Heading,L"STATIC",SS_LEFT,28,24,480,44,L"ThunderDisplay");
    add(LanguageLabel,L"STATIC",SS_LEFT,580,35,70,24); add(Language,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,650,28,134,160);
    add(Intro,L"STATIC",SS_LEFT,28,78,756,28);
    add(Connect,L"BUTTON",BS_OWNERDRAW|WS_TABSTOP,28,120,240,42);
    add(Disconnect,L"BUTTON",BS_OWNERDRAW|WS_TABSTOP,284,120,240,42);
    add(ShowDisplay,L"BUTTON",BS_OWNERDRAW|WS_TABSTOP,540,120,244,42);
    add(Status,L"STATIC",SS_LEFT,28,178,756,28);
    add(DisplaySection,L"STATIC",SS_LEFT,48,236,400,28);
    add(ScreenLabel,L"STATIC",SS_LEFT,48,282,140,26); add(Screen,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,200,274,446,180);
    add(Detect,L"BUTTON",BS_OWNERDRAW|WS_TABSTOP,662,274,102,32);
    add(DisplayInfo,L"STATIC",SS_LEFT,48,322,716,42); add(StreamInfo,L"STATIC",SS_LEFT,48,366,716,26);
    add(ConnectionSection,L"STATIC",SS_LEFT,48,432,400,28);
    add(HostLabel,L"STATIC",SS_LEFT,48,480,140,24); add(Host,L"EDIT",ES_AUTOHSCROLL|WS_TABSTOP,200,474,564,32);
    add(HostHint,L"STATIC",SS_LEFT,200,516,564,40);
    add(UsePairing,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,560,400,26);
    add(Code,L"EDIT",ES_AUTOHSCROLL|ES_PASSWORD|WS_TABSTOP,200,600,564,32);
    SendMessageW(fields[Code],EM_SETLIMITTEXT,32,0);
    add(ShowCode,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,200,644,140,26);
    add(Remember,L"BUTTON",BS_AUTOCHECKBOX|BS_MULTILINE|WS_TABSTOP,350,644,414,30);
    add(QualitySection,L"STATIC",SS_LEFT,48,0,400,28);
    add(ModeLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Mode,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,200,0,564,220);
    add(CodecLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Codec,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,200,0,564,180);
    add(AutoHint,L"STATIC",SS_LEFT,48,0,716,42);
    add(WidthLabel,L"STATIC",SS_LEFT,48,0,72,24); add(Width,L"EDIT",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,128,0,130,32);
    add(HeightLabel,L"STATIC",SS_LEFT,286,0,72,24); add(Height,L"EDIT",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,366,0,130,32);
    add(RateLabel,L"STATIC",SS_LEFT,48,0,140,24);
    add(RateValue,L"EDIT",ES_NUMBER|ES_RIGHT|ES_AUTOHSCROLL|WS_TABSTOP,550,0,160,32);
    SendMessageW(fields[RateValue],EM_SETLIMITTEXT,7,0);
    SetWindowSubclass(fields[RateValue],bitrateInputProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    add(RateUnit,L"STATIC",SS_RIGHT,718,0,46,24,L"Mbps");
    add(Rate,TRACKBAR_CLASSW,TBS_HORZ|TBS_NOTICKS|TBS_FIXEDLENGTH|WS_TABSTOP,48,0,716,40);
    SetWindowSubclass(fields[Rate],sliderProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(fields[Rate],TBM_SETRANGEMIN,FALSE,0); SendMessageW(fields[Rate],TBM_SETRANGEMAX,FALSE,td::BitrateSliderSteps);
    SendMessageW(fields[Rate],TBM_SETPAGESIZE,0,50);
    add(RateAuto,L"STATIC",SS_LEFT,48,0,120,22); add(RateMax,L"STATIC",SS_RIGHT,400,0,364,22);
    add(LinkInfo,L"STATIC",SS_LEFT,48,0,716,50);
    add(PortLabel,L"STATIC",SS_LEFT,418,0,72,24); add(Port,L"EDIT",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,498,0,266,32);
    add(Fullscreen,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,350,26); add(Vsync,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,414,0,350,26);
    add(DepthLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Depth,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,200,0,564,180);
    add(HotkeyLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Hotkey,L"EDIT",ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,200,0,564,32);
    SetWindowSubclass(fields[Hotkey],hotkeyProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(fields[Hotkey],HKM_SETHOTKEY,saved.fullscreenHotkey,0);
    add(NativePixels,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[NativePixels],saved.nativePixels);
    add(LocalCursor,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[LocalCursor],saved.localCursor);
    add(Clipboard,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[Clipboard],saved.clipboard);
    add(DiagnosticSection,L"STATIC",SS_LEFT,48,0,400,28);
    add(DebugLogs,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[DebugLogs],saved.debugLogs);
    add(Detail,L"EDIT",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP,48,0,716,logHeight);
    add(LogResize,L"BUTTON",BS_OWNERDRAW|BS_NOTIFY|WS_TABSTOP,48,0,716,28);
    SetWindowSubclass(fields[LogResize],logResizeProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    add(Shortcuts,L"STATIC",SS_LEFT,28,0,756,60);
    SetWindowTextW(fields[Host],wide(saved.host).c_str()); SetWindowTextW(fields[Code],wide(saved.token).c_str());
    SetWindowTextW(fields[Width],std::to_wstring(saved.settings.width).c_str()); SetWindowTextW(fields[Height],std::to_wstring(saved.settings.height).c_str());
    SetWindowTextW(fields[Port],std::to_wstring(saved.port).c_str());
    checkBox(fields[UsePairing],saved.pairing); checkBox(fields[Remember],remember); checkBox(fields[Fullscreen],saved.fullscreen); checkBox(fields[Vsync],saved.vsync);
}
void SetupWindow::translate() {
    const bool wasApplyingPreset=applyingPreset; applyingPreset=true;
    auto label=[&](int id,const wchar_t* zh,const wchar_t* en) { SetWindowTextW(fields[id],tr(zh,en)); };
    SetWindowTextW(window,tr(L"ThunderDisplay " TD_VERSION_WIDE L" · 连接 Mac",L"ThunderDisplay " TD_VERSION_WIDE L" · Connect to Mac"));
    label(Intro,L"WINDOWS 客户端  ·  雷雳直连  ·  " TD_VERSION_WIDE,L"WINDOWS CLIENT  ·  THUNDERBOLT  ·  " TD_VERSION_WIDE);
    label(LanguageLabel,L"语言",L"Language"); label(DisplaySection,L"你的显示屏",L"Your display"); label(ScreenLabel,L"显示到",L"Display on"); label(Detect,L"重新检测",L"Refresh");
    label(ConnectionSection,L"连接 Mac",L"Connect to Mac"); label(QualitySection,L"画质与性能",L"Quality & performance"); label(DiagnosticSection,L"连接诊断",L"Connection diagnostics");
    label(HostLabel,L"Mac IPv4 地址",L"Mac IPv4 address"); label(HostHint,L"留空自动查找，或填写 Mac 界面显示的网桥地址。",L"Discover automatically, or enter the bridge address shown on your Mac.");
    label(UsePairing,L"使用配对码（直连默认关闭）",L"Use pairing code (optional on a direct link)"); label(ShowCode,L"显示配对码",L"Show code");
    label(Remember,L"在此电脑记住配对码（加密）",L"Remember code on this PC (encrypted)");
    label(ModeLabel,L"显示模式",L"Display mode"); label(CodecLabel,L"视频模式",L"Video mode"); label(WidthLabel,L"宽度",L"Width"); label(HeightLabel,L"高度",L"Height");
    label(RateLabel,L"视频码率",L"Video bitrate"); label(RateAuto,L"自动",L"Auto"); label(PortLabel,L"端口",L"Port");
    label(Fullscreen,L"连接后全屏",L"Fullscreen after connecting"); label(Vsync,L"垂直同步",L"VSync");
    label(Connect,L"连接 Mac",L"Connect to Mac"); label(Disconnect,L"断开",L"Disconnect"); label(ShowDisplay,L"打开远程画面",L"Show remote display");
    label(HotkeyLabel,L"全屏快捷键",L"Fullscreen shortcut"); label(DepthLabel,L"串流色深",L"Stream precision");
    label(NativePixels,L"清晰度优先：保留 Mac HiDPI 像素（自动模式）",L"Preserve Mac HiDPI pixels (Auto mode, best detail)");
    label(LocalCursor,L"本地指针 · macOS 原生（同步系统形状与点击热点）",L"Local native macOS cursor (system shape and click hotspot)");
    label(Clipboard,L"双向剪贴板：文字与图片（连接后复制）",L"Sync copied text and images (new copies only)");
    label(DebugLogs,L"调试：将性能日志发送到 Mac（可在连接中切换）",L"Debug: share performance logs with Mac (change while connected)");
    label(LogResize,L"拖动调整日志高度",L"Drag to resize log height");
    label(Shortcuts,L"在全屏快捷键框中直接按组合键（需含 Ctrl 或 Alt）。\r\nCtrl+Alt+Shift+Esc 释放输入 · Ctrl+Shift+Esc 任务管理器",L"Press a key combination in the shortcut field (include Ctrl or Alt).\r\nCtrl+Alt+Shift+Esc releases input · Ctrl+Shift+Esc opens Task Manager");
    auto combo=[&](int id,std::initializer_list<const wchar_t*> items,int fallback) {
        int index=selection(fields[id]); if(index<0) index=fallback;
        SendMessageW(fields[id],CB_RESETCONTENT,0,0);
        for(auto* item:items) SendMessageW(fields[id],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item)); select(fields[id],index);
    };
    combo(Language,{tr(L"跟随系统",L"System default"),L"中文",L"English"},uiLanguage);
    combo(Mode,{tr(L"自动",L"Auto"),tr(L"自定义",L"Custom")},saved.autoQuality?0:1);
    combo(Codec,{tr(L"自动（优先 HEVC）",L"Auto (prefer HEVC)"),L"HEVC / H.265",L"H.264",tr(L"无压缩 · 10 位（高速链路）",L"Uncompressed · 10-bit (fast link)")},saved.uncompressed?3:saved.settings.codecMask==3?0:saved.settings.codecMask==2?1:2);
    combo(Depth,{tr(L"自动（支持时使用 10-bit SDR）",L"Auto (10-bit SDR when supported)"),L"8-bit SDR",L"10-bit SDR"},saved.colorDepth==10?2:saved.colorDepth==8?1:0);
    describeDisplay();
    applyingPreset=wasApplyingPreset;
}
void SetupWindow::layout() {
    if(controls.empty() || layingOut) return;
    layingOut=true;
    struct LayoutScope { bool& busy; ~LayoutScope() { busy=false; } } scope{layingOut};
    auto scale=[&](int value) { return MulDiv(value,int(dpi),96); };
    if(!font || fontDPI!=dpi) {
        if(font) DeleteObject(font); if(headingFont) DeleteObject(headingFont); if(sectionFont) DeleteObject(sectionFont); if(hintFont) DeleteObject(hintFont);
        font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        headingFont=CreateFontW(-scale(32),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        hintFont=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        sectionFont=CreateFontW(-scale(17),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"); fontDPI=dpi;
        for(auto& c:controls) { c.appliedFont=nullptr; c.regionWidth=c.regionHeight=0; }
    }
    RECT r{}; GetClientRect(window,&r); int logicalWidth=MulDiv(r.right,96,int(dpi)),logicalHeight=MulDiv(r.bottom,96,int(dpi));
    td::SetupLayout metrics{logicalWidth,logicalHeight,checked(fields[UsePairing]),selection(fields[Mode])!=0,logHeight};
    SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS; si.nMax=scale(metrics.contentHeight())-1; si.nPage=UINT(std::max<LONG>(1,r.bottom));
    scroll.resize(si.nMax+1,int(si.nPage)); si.nPos=scroll.position(); SetScrollInfo(window,SB_VERT,&si,TRUE);
    GetClientRect(window,&r); logicalWidth=MulDiv(r.right,96,int(dpi)); metrics.width=logicalWidth;
    if(layoutWidth==r.right && layoutHeight==r.bottom && layoutPaired==metrics.paired && layoutManual==metrics.manual && layoutLogHeight==logHeight) { scrollContent(); return; }
    layoutWidth=r.right; layoutHeight=r.bottom; layoutPaired=metrics.paired; layoutManual=metrics.manual; layoutLogHeight=logHeight;
    const auto logFirstLine=SendMessageW(fields[Detail],EM_GETFIRSTVISIBLELINE,0,0);
    struct Placement { HWND child; int x,y,w,h; UINT flags; };
    std::vector<Placement> placements; placements.reserve(controls.size());
    for(auto& c:controls) {
        int id=GetDlgCtrlID(c.hwnd),x=c.x,y=c.y,w=c.w,h=c.h; bool visible=true;
        switch(id) {
        case Heading: w=std::max(260,logicalWidth-275); break;
        case Language: x=logicalWidth-162; w=134; break;
        case LanguageLabel: x=logicalWidth-240; break;
        case Intro: case Status: case Shortcuts: w=metrics.fullWidth(); break;
        case DisplaySection: case ConnectionSection: case QualitySection: case DiagnosticSection: w=logicalWidth-96; break;
        case DisplayInfo: case StreamInfo: case AutoHint: case LinkInfo: w=logicalWidth-96; break;
        case Screen: w=std::max(80,metrics.fieldWidth()-118); break;
        case Detect: x=logicalWidth-150; break;
        case Scaling: case Hotkey: case Depth: case Code: case Mode: case Codec: case HostHint: w=metrics.fieldWidth(); break;
        case Host: w=metrics.fieldWidth()-154; break;
        case PortLabel: x=logicalWidth-186; y=480; w=46; break;
        case Port: x=logicalWidth-132; y=474; w=84; break;
        case Remember: w=logicalWidth-x-48; break;
        case Connect: case Disconnect: case ShowDisplay: {
            int column=id==Connect?0:id==Disconnect?1:2; int cell=(metrics.fullWidth()-30)/3; x=28+column*(cell+15); w=cell; break;
        }
        case WidthLabel: case Width: case HeightLabel: case Height: {
            int column=(id==WidthLabel||id==Width)?0:1;
            auto cell=metrics.column(column,2,18); bool input=id==Width||id==Height;
            x=cell.x+(input?65:0); w=input?cell.width-65:60; y=metrics.qualityTop()+(input?194:200); visible=metrics.manual; break;
        }
        case RateLabel: x=48; w=140; y=metrics.qualityTop()+238; break;
        case RateValue: x=logicalWidth-262; w=160; y=metrics.qualityTop()+232; break;
        case RateUnit: x=logicalWidth-94; w=46; y=metrics.qualityTop()+238; break;
        case Rate: x=48; w=logicalWidth-96; y=metrics.qualityTop()+270; break;
        case RateAuto: x=48; w=120; y=metrics.qualityTop()+308; break;
        case RateMax: x=logicalWidth-348; w=300; y=metrics.qualityTop()+308; break;
        case Fullscreen: case Vsync: {
            auto cell=metrics.column(id==Vsync?1:0,2,18); x=cell.x; w=cell.width; y=metrics.qualityTop()+410; break;
        }
        case DebugLogs: x=48; w=logicalWidth-96; y=metrics.diagnosticsTop()+58; break;
        case Detail: x=48; w=logicalWidth-96; y=metrics.logTop(); h=metrics.logHeight(); c.h=h; break;
        case LogResize: x=48; w=logicalWidth-96; y=metrics.logResizeTop(); break;
        }
        if(id==QualitySection) y=metrics.qualityTop()+18;
        if(id==ModeLabel) y=metrics.qualityTop()+66;
        if(id==Mode) y=metrics.qualityTop()+60;
        if(id==CodecLabel) y=metrics.qualityTop()+110;
        if(id==Codec) y=metrics.qualityTop()+104;
        if(id==DepthLabel) y=metrics.qualityTop()+154;
        if(id==Depth) y=metrics.qualityTop()+148;
        if(id==HotkeyLabel) y=metrics.qualityTop()+462;
        if(id==Hotkey) y=metrics.qualityTop()+456;
        if(id==NativePixels) { y=metrics.qualityTop()+506; w=logicalWidth-96; }
        if(id==LocalCursor) { y=metrics.qualityTop()+582; w=logicalWidth-96; }
        if(id==Clipboard) { y=metrics.qualityTop()+544; w=logicalWidth-96; }
        if(id==AutoHint) { y=metrics.qualityTop()+192; h=40; visible=!metrics.manual; }
        if(id==LinkInfo) y=metrics.qualityTop()+340;
        if(id==DiagnosticSection) y=metrics.diagnosticsTop()+18;
        if(id==Shortcuts) y=metrics.shortcutsTop();
        if(id==Code || id==ShowCode || id==Remember) visible=metrics.paired;
        c.x=x; c.y=y;
        HFONT face=id==Heading?headingFont:(id==DisplaySection||id==ConnectionSection||id==QualitySection||id==DiagnosticSection)?sectionFont:font;
        if(id==DisplayInfo||id==StreamInfo||id==HostHint||id==AutoHint||id==LinkInfo||id==RateAuto||id==RateMax||id==Shortcuts||id==Intro) face=hintFont;
        if(c.appliedFont!=face) {
            SendMessageW(c.hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(face),FALSE); c.appliedFont=face;
            wchar_t kind[32]{}; GetClassNameW(c.hwnd,kind,32);
            if(_wcsicmp(kind,L"ComboBox")==0) { SendMessageW(c.hwnd,CB_SETITEMHEIGHT,0,scale(24)); SendMessageW(c.hwnd,CB_SETITEMHEIGHT,WPARAM(-1),scale(24)); }
            if(id==Rate) SendMessageW(c.hwnd,TBM_SETTHUMBLENGTH,scale(24),0);
        }
        UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOREDRAW|SWP_NOCOPYBITS;
        bool wasVisible=(GetWindowLongPtrW(c.hwnd,GWL_STYLE)&WS_VISIBLE)!=0;
        if(wasVisible!=visible) flags|=visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW;
        placements.push_back({c.hwnd,scale(x),scale(y),scale(std::max(1,w)),scale(h),flags});
    }
    // Move all children before scheduling a paint; do not expose intermediate rows.
    auto batch=BeginDeferWindowPos(int(placements.size()));
    for(auto& p:placements) {
        if(!batch) break;
        batch=DeferWindowPos(batch,p.child,nullptr,p.x,p.y,p.w,p.h,p.flags);
    }
    if(!batch || !EndDeferWindowPos(batch)) {
        for(auto& p:placements) SetWindowPos(p.child,nullptr,p.x,p.y,p.w,p.h,p.flags);
    }
    const auto updatedLogFirstLine=SendMessageW(fields[Detail],EM_GETFIRSTVISIBLELINE,0,0);
    SendMessageW(fields[Detail],EM_LINESCROLL,0,logFirstLine-updatedLogFirstLine);
    for(auto& c:controls) if(c.rounded) {
        RECT bounds{}; GetWindowRect(c.hwnd,&bounds); int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
        if(c.regionWidth==w && c.regionHeight==h) continue;
        auto region=CreateRoundRectRgn(0,0,w+1,h+1,scale(10),scale(10));
        if(SetWindowRgn(c.hwnd,region,FALSE)) { c.regionWidth=w; c.regionHeight=h; }
        else DeleteObject(region);
    }
    scrollContent();
}
void SetupWindow::scrollContent() {
    if(!content) return;
    RECT r{}; GetClientRect(window,&r);
    td::SetupLayout metrics{MulDiv(r.right,96,int(dpi)),MulDiv(r.bottom,96,int(dpi)),checked(fields[UsePairing]),selection(fields[Mode])!=0,logHeight};
    SetWindowPos(content,nullptr,0,-scroll.position(),r.right,MulDiv(metrics.contentHeight(),int(dpi),96),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
LRESULT CALLBACK SetupWindow::contentProcedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    auto self=reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE) { self=static_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if(self) {
        if(m==WM_PAINT) { PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); self->paint(dc); EndPaint(h,&ps); return 0; }
        if(m==WM_ERASEBKGND) return 1;
        if(m==WM_COMMAND || m==WM_DRAWITEM || m==WM_MEASUREITEM || m==WM_NOTIFY || m==WM_HSCROLL || m==WM_CTLCOLORLISTBOX || m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN || m==WM_CTLCOLOREDIT || m==WM_MOUSEWHEEL) return self->message(m,w,l);
    }
    return DefWindowProcW(h,m,w,l);
}
void SetupWindow::setLogHeight(int height,bool persist) {
    height=td::SetupLayout::boundedLogHeight(height);
    if(logHeight!=height) { logHeight=height; layout(); }
    if(persist) saveLogHeight();
}
void SetupWindow::saveLogHeight() {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
        putNumber(key,L"LogPanelHeight",DWORD(logHeight)); RegCloseKey(key);
    }
}
void SetupWindow::finishLogResize(bool cancel) {
    if(!resizingLog) return;
    resizingLog=false;
    if(GetCapture()==fields[LogResize]) ReleaseCapture();
    if(cancel) setLogHeight(logResizeStartHeight);
    else if(logHeight!=logResizeStartHeight) saveLogHeight();
    InvalidateRect(fields[LogResize],nullptr,FALSE);
}
LRESULT CALLBACK SetupWindow::logResizeProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
    switch(m) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_SIZENS)); return TRUE;
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        POINT point{}; if(!GetCursorPos(&point)) return 0;
        self->logResizeStartY=point.y; self->logResizeStartHeight=self->logHeight;
        self->logResizeDPI=self->dpi; self->resizingLog=true;
        SetCapture(h); InvalidateRect(h,nullptr,FALSE); return 0;
    }
    case WM_MOUSEMOVE:
        if(self->resizingLog && GetCapture()==h) {
            POINT point{};
            if(GetCursorPos(&point)) self->setLogHeight(self->logResizeStartHeight+MulDiv(point.y-self->logResizeStartY,96,int(self->logResizeDPI)));
            return 0;
        }
        break;
    case WM_LBUTTONUP: self->finishLogResize(); return 0;
    case WM_CAPTURECHANGED: case WM_CANCELMODE: self->finishLogResize(); return 0;
    case WM_LBUTTONDBLCLK:
        self->finishLogResize(true); self->setLogHeight(td::SetupLayout::DefaultLogHeight,true); return 0;
    case WM_GETDLGCODE: {
        auto key=l?reinterpret_cast<MSG*>(l)->wParam:0;
        auto code=DefSubclassProc(h,m,w,l)|DLGC_WANTARROWS;
        if(key==VK_PRIOR || key==VK_NEXT || key==VK_HOME || key==VK_END || (key==VK_ESCAPE && self->resizingLog)) code|=DLGC_WANTMESSAGE;
        return code;
    }
    case WM_KEYDOWN:
        if(w==VK_ESCAPE && self->resizingLog) { self->finishLogResize(true); return 0; }
        if(w==VK_UP || w==VK_DOWN || w==VK_PRIOR || w==VK_NEXT || w==VK_HOME || w==VK_END) {
            int height=self->logHeight;
            if(w==VK_UP) height-=16; else if(w==VK_DOWN) height+=16;
            else if(w==VK_PRIOR) height-=64; else if(w==VK_NEXT) height+=64;
            else height=w==VK_HOME?td::SetupLayout::MinLogHeight:td::SetupLayout::MaxLogHeight;
            self->setLogHeight(height,true); return 0;
        }
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(h,logResizeProcedure,id); break;
    }
    return DefSubclassProc(h,m,w,l);
}
LRESULT CALLBACK SetupWindow::hotkeyProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
    if(m==HKM_GETHOTKEY) return self->saved.fullscreenHotkey;
    if(m==HKM_SETHOTKEY) {
        self->saved.fullscreenHotkey=uint16_t(w);
        SetWindowTextW(h,shortcutText(uint16_t(w)).c_str()); return 0;
    }
    auto modifiers=[] {
        return uint8_t(((GetKeyState(VK_SHIFT)&0x8000)?HOTKEYF_SHIFT:0)|
            ((GetKeyState(VK_CONTROL)&0x8000)?HOTKEYF_CONTROL:0)|((GetKeyState(VK_MENU)&0x8000)?HOTKEYF_ALT:0));
    };
    if(m==WM_GETDLGCODE) {
        auto key=l?reinterpret_cast<MSG*>(l)->wParam:0;
        if(key==VK_TAB && !(modifiers()&(HOTKEYF_CONTROL|HOTKEYF_ALT))) return DLGC_WANTARROWS|DLGC_WANTCHARS;
        return DLGC_WANTALLKEYS;
    }
    if(m==WM_KEYDOWN || m==WM_SYSKEYDOWN) {
        if(w!=VK_CONTROL && w!=VK_MENU && w!=VK_SHIFT && !(w>=VK_LSHIFT && w<=VK_RMENU)) {
            if(w==VK_BACK && modifiers()==0) SendMessageW(h,HKM_SETHOTKEY,td::DefaultFullscreenHotkey,0);
            else {
                auto candidate=uint16_t(MAKEWORD(uint8_t(w),modifiers()|((l&(1u<<24))?HOTKEYF_EXT:0)));
                if(td::validFullscreenHotkey(candidate)) SendMessageW(h,HKM_SETHOTKEY,candidate,0);
                else self->status(tr(L"快捷键需含 Ctrl 或 Alt；F11 和系统保留组合键不可用。",L"Include Ctrl or Alt. F11 and reserved system shortcuts cannot be assigned."));
            }
            SendMessageW(self->content,WM_COMMAND,MAKEWPARAM(Hotkey,EN_CHANGE),reinterpret_cast<LPARAM>(h)); return 0;
        }
        return 0; // Modifier presses alone must not erase the saved combination.
    }
    if(m==WM_KEYUP || m==WM_SYSKEYUP || m==WM_CHAR || m==WM_SYSCHAR) return 0;
    if(m==WM_NCDESTROY) RemoveWindowSubclass(h,hotkeyProcedure,id);
    return DefSubclassProc(h,m,w,l);
}
LRESULT CALLBACK SetupWindow::comboProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
    if(self->theme.dark && !self->theme.highContrast) {
        if(m==WM_PAINT) { PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); self->paintCombo(h,dc); EndPaint(h,&ps); return 0; }
        if(m==WM_PRINTCLIENT) { self->paintCombo(h,reinterpret_cast<HDC>(w)); return 0; }
        if(m==WM_ERASEBKGND) return 1;
    }
    if(m==WM_NCDESTROY) { RemoveWindowSubclass(h,comboProcedure,id); return DefSubclassProc(h,m,w,l); }
    const auto result=DefSubclassProc(h,m,w,l);
    if(m==CB_SETCURSEL || m==CB_SHOWDROPDOWN || m==WM_ENABLE || m==WM_SETFOCUS || m==WM_KILLFOCUS || m==WM_KEYDOWN || m==WM_LBUTTONUP)
        InvalidateRect(h,nullptr,FALSE);
    return result;
}

void SetupWindow::paintCombo(HWND combo,HDC dc) {
    RECT bounds{}; GetClientRect(combo,&bounds);
    const auto enabled=IsWindowEnabled(combo)!=FALSE,focused=GetFocus()==combo;
    const auto scale=[&](int value){return MulDiv(value,int(dpi),96);};
    FillRect(dc,&bounds,GetDlgCtrlID(combo)==Language?background:cardBackground);
    auto brush=CreateSolidBrush(theme.field); auto pen=CreatePen(PS_SOLID,scale(1),focused && enabled?theme.accent:theme.border);
    const auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,0,0,bounds.right,bounds.bottom,scale(10),scale(10));
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
    const auto index=selection(combo);
    std::wstring text;
    if(index>=0) {
        const auto length=SendMessageW(combo,CB_GETLBTEXTLEN,index,0);
        if(length>=0) { text.resize(size_t(length)+1); SendMessageW(combo,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text.data())); text.resize(size_t(length)); }
    }
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,enabled?theme.text:theme.muted);
    auto face=reinterpret_cast<HFONT>(SendMessageW(combo,WM_GETFONT,0,0));
    auto oldFont=SelectObject(dc,face?face:GetStockObject(DEFAULT_GUI_FONT));
    RECT label=bounds; label.left+=scale(10); label.right-=scale(30);
    DrawTextW(dc,text.c_str(),int(text.size()),&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,oldFont);
    const auto x=bounds.right-scale(16),y=bounds.bottom/2;
    auto arrow=CreatePen(PS_SOLID,scale(1),enabled?theme.text:theme.muted); auto arrowOldPen=SelectObject(dc,arrow);
    MoveToEx(dc,x-scale(4),y-scale(2),nullptr); LineTo(dc,x,y+scale(2)); LineTo(dc,x+scale(4),y-scale(2));
    SelectObject(dc,arrowOldPen); DeleteObject(arrow);
}

LRESULT CALLBACK SetupWindow::sliderProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
    if(m==WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); self->paintSlider(h,dc); EndPaint(h,&ps); return 0;
    }
    // Composited parents can request a print instead of an ordinary paint.
    if(m==WM_PRINT || m==WM_PRINTCLIENT) { self->paintSlider(h,reinterpret_cast<HDC>(w)); return 0; }
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_LBUTTONDOWN && IsWindowEnabled(h)) {
        RECT channel{},thumb{}; SendMessageW(h,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
        SendMessageW(h,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(channel.right>channel.left && !PtInRect(&thumb,point)) {
            SetFocus(h);
            const auto position=std::clamp(MulDiv(point.x-channel.left,td::BitrateSliderSteps,channel.right-channel.left),0,td::BitrateSliderSteps);
            SendMessageW(h,TBM_SETPOS,FALSE,position);
            SendMessageW(h,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
            // Start the native drag at its own thumb geometry after seeking.
            DefSubclassProc(h,m,w,MAKELPARAM((thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2));
            SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,position),reinterpret_cast<LPARAM>(h));
            InvalidateRect(h,nullptr,FALSE); return 0;
        }
    }
    if(m==WM_NCDESTROY) { RemoveWindowSubclass(h,sliderProcedure,id); return DefSubclassProc(h,m,w,l); }
    const auto result=DefSubclassProc(h,m,w,l);
    if(m==TBM_SETPOS || m==WM_SIZE || m==WM_ENABLE || m==WM_SETFOCUS || m==WM_KILLFOCUS ||
       m==WM_KEYDOWN || m==WM_LBUTTONDOWN || m==WM_LBUTTONUP || m==WM_CAPTURECHANGED)
        InvalidateRect(h,nullptr,FALSE);
    return result;
}
void SetupWindow::paintSlider(HWND slider,HDC target) {
    RECT area{}; GetClientRect(slider,&area);
    if(!target || area.right<=0 || area.bottom<=0) return;
    const auto scale=[&](int v){return std::max(1,MulDiv(v,int(dpi),96));};
    auto buffer=CreateCompatibleDC(target); auto bitmap=CreateCompatibleBitmap(target,area.right,area.bottom);
    const bool buffered=buffer && bitmap;
    auto dc=buffered?buffer:target;
    auto oldBitmap=buffered?SelectObject(dc,bitmap):nullptr;
    FillRect(dc,&area,cardBackground);
    const bool enabled=IsWindowEnabled(slider)!=FALSE;
    RECT channel{},thumb{};
    SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    // Use the native input geometry, and keep a visible fallback during resize.
    const int left=channel.right>channel.left?int(channel.left):scale(12);
    const int right=std::max(left+1,channel.right>channel.left?int(channel.right):int(area.right)-scale(12));
    const int position=int(SendMessageW(slider,TBM_GETPOS,0,0));
    const int x=std::clamp(thumb.right>thumb.left?int((thumb.left+thumb.right)/2):left+MulDiv(right-left,position,td::BitrateSliderSteps),left,right);
    const int y=thumb.bottom>thumb.top?int((thumb.top+thumb.bottom)/2):int(area.bottom)/2;
    const int half=scale(3),radius=scale(8);
    auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    auto brush=CreateSolidBrush(theme.highContrast?theme.text:theme.border); auto oldBrush=SelectObject(dc,brush);
    RoundRect(dc,left,y-half,right,y+half,half*2,half*2);
    SelectObject(dc,oldBrush); DeleteObject(brush);
    if(position>0) {
        brush=CreateSolidBrush(enabled?theme.accent:theme.muted); SelectObject(dc,brush);
        RoundRect(dc,left,y-half,x,y+half,half*2,half*2);
        SelectObject(dc,oldBrush); DeleteObject(brush);
    }
    if(enabled && GetFocus()==slider) {
        auto pen=CreatePen(PS_SOLID,scale(1),theme.accent); SelectObject(dc,pen); SelectObject(dc,GetStockObject(NULL_BRUSH));
        const int ring=radius+scale(4); Ellipse(dc,x-ring,y-ring,x+ring+1,y+ring+1);
        SelectObject(dc,GetStockObject(NULL_PEN)); SelectObject(dc,oldBrush); DeleteObject(pen);
    }
    const auto fill=enabled?(theme.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)):theme.muted;
    brush=CreateSolidBrush(fill); auto pen=CreatePen(PS_SOLID,scale(GetCapture()==slider?2:1),enabled?theme.accent:theme.border);
    SelectObject(dc,brush); SelectObject(dc,pen);
    Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
    if(buffered) { BitBlt(target,0,0,area.right,area.bottom,dc,0,0,SRCCOPY); SelectObject(dc,oldBitmap); }
    if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
}
LRESULT CALLBACK SetupWindow::bitrateInputProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
    if(m==WM_KEYDOWN && w=='A' && (GetKeyState(VK_CONTROL)&0x8000)) { SendMessageW(h,EM_SETSEL,0,-1); return 0; }
    if(m==WM_GETDLGCODE) {
        const auto code=DefSubclassProc(h,m,w,l);
        const auto key=l?reinterpret_cast<MSG*>(l)->wParam:w;
        return key==VK_RETURN || key==VK_ESCAPE?code|DLGC_WANTALLKEYS:code;
    }
    if(m==WM_KEYDOWN && w==VK_RETURN) { self->commitBitrateInput(); return 0; }
    if(m==WM_KEYDOWN && w==VK_ESCAPE) {
        self->selectedBitrate=self->bitrateBeforeEdit; self->bitrateMaximum=self->bitrateMaximumBeforeEdit;
        self->updateBitrateControls(true); return 0;
    }
    if(m==WM_CHAR && (w==VK_RETURN || w==VK_ESCAPE || w==1)) return 0;
    if(m==WM_NCDESTROY) RemoveWindowSubclass(h,bitrateInputProcedure,id);
    return DefSubclassProc(h,m,w,l);
}

void SetupWindow::setNetworkLink(const td::NetworkLink& link) {
    const auto signature=link.localIPv4+"/"+std::to_string(link.index)+"/"+std::to_string(link.receiveRate)+"/"+std::to_string(link.transmitRate);
    if(lastSessionLink==signature) return;
    lastSessionLink=signature;
    connectedLocalIPv4=link.localIPv4;
    applyNetworkLink(link);
}
void SetupWindow::applyNetworkLink(td::NetworkLink link) {
    if(networkLink.index==link.index && networkLink.name==link.name && networkLink.localIPv4==link.localIPv4 &&
       networkLink.receiveRate==link.receiveRate && networkLink.transmitRate==link.transmitRate) return;
    networkLink=std::move(link); updateBitrateControls();
}
void SetupWindow::pollNetworkLink() {
    const auto host=utf8(value(fields[Host])); const auto local=active?connectedLocalIPv4:std::string{};
    if(linkQuery.valid() && linkQuery.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        try { auto link=linkQuery.get(); if(host==queryHost && local==queryLocal) applyNetworkLink(std::move(link)); }
        catch(...) { applyNetworkLink({}); }
    }
    if(!linkQuery.valid() && micros()>=nextLinkQuery) {
        queryHost=host; queryLocal=local; nextLinkQuery=micros()+2000000;
        // Driver queries run outside the UI, video receive and input threads.
        linkQuery=std::async(std::launch::async,[host,local]{return td::detectNetworkLink(host,local);});
    }
}
void SetupWindow::updateBitrateControls(bool replaceInput) {
    if(updatingBitrateControls) return;
    updatingBitrateControls=true;
    struct Scope { bool& busy; ~Scope(){busy=false;} } scope{updatingBitrateControls};
    const auto limit=networkLink.bitrateLimit();
    if(bitrateMaximum) selectedBitrate=limit;
    else if(selectedBitrate) selectedBitrate=std::clamp(selectedBitrate,td::MinimumBitrate,limit);
    SendMessageW(fields[Rate],TBM_SETPOS,TRUE,td::bitrateSliderPosition(selectedBitrate,limit));
    const auto text=selectedBitrate?std::to_wstring(selectedBitrate/1000000)+L" Mbps":std::wstring(tr(L"自动",L"Auto"));
    if(replaceInput || GetFocus()!=fields[RateValue]) {
        const auto entry=selectedBitrate?std::to_wstring(selectedBitrate/1000000):std::wstring{};
        SetWindowTextW(fields[RateValue],entry.c_str());
    }
    SendMessageW(fields[RateValue],EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(tr(L"自动",L"Auto")));
    SetWindowTextW(fields[Rate],text.c_str());
    SetWindowTextW(fields[RateMax],(std::wstring(tr(L"最大 ",L"Max "))+linkRateText(limit)).c_str());
    std::wstring link;
    if(networkLink.index) {
        link=std::wstring(tr(L"网络接口：",L"Network interface: "))+networkLink.name+L"\r\n"+
            tr(L"接收 ",L"Receive ")+linkRateText(networkLink.receiveRate)+tr(L" · 发送 ",L" · Send ")+linkRateText(networkLink.transmitRate)+
            tr(L"（驱动报告）",L" (driver-reported)");
        if(!td::knownLinkRate(networkLink.receiveRate)) link+=tr(L" · 上限回退 20 Gbps",L" · 20 Gbps fallback limit");
    } else link=tr(L"网桥速率尚未检测到，码率上限暂用 20 Gbps。连接后按实际网卡重新核对。",L"Bridge speed unavailable: using a 20 Gbps limit. The actual connection interface is checked on connect.");
    SetWindowTextW(fields[LinkInfo],link.c_str());
}
bool SetupWindow::commitBitrateInput() {
    const auto parsed=td::parseBitrateMbps(utf8(value(fields[RateValue])),networkLink.bitrateLimit());
    if(!parsed) {
        status(tr(L"请输入有效码率",L"Enter a valid bitrate"),
            std::wstring(tr(L"码率范围：10–",L"Bitrate range: 10–"))+std::to_wstring(networkLink.bitrateLimit()/1000000)+
            tr(L" Mbps；留空或输入 0 使用自动码率。",L" Mbps. Leave empty or enter 0 for automatic bitrate."));
        return false;
    }
    if(selectedBitrate!=*parsed) { selectedBitrate=*parsed; bitrateMaximum=false; }
    updateBitrateControls(true);
    bitrateBeforeEdit=selectedBitrate; bitrateMaximumBeforeEdit=bitrateMaximum;
    return true;
}
ClientOptions SetupWindow::read() {
    if(selection(fields[Codec])!=3 && !commitBitrateInput()) throw std::runtime_error(utf8(tr(L"请修正视频码率后再连接。",L"Correct the video bitrate before connecting.")));
    ClientOptions o; o.host=utf8(value(fields[Host])); o.pairing=checked(fields[UsePairing]);
    o.token=o.pairing?utf8(value(fields[Code])):std::string{};
    for(auto& c:o.token) c=char(std::tolower(static_cast<unsigned char>(c)));
    if(o.pairing && (o.token.size()!=32 || !std::all_of(o.token.begin(),o.token.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})))
        throw std::runtime_error(utf8(tr(L"请粘贴 Mac 设置窗口中的 32 位配对码。",L"Paste the 32-character pairing code from the Mac setup window.")));
    if(!o.host.empty()) { IN_ADDR address{}; if(InetPtonA(AF_INET,o.host.c_str(),&address)!=1 || address.S_un.S_addr==0) throw std::runtime_error(utf8(tr(L"Mac 地址必须为 IPv4，或留空自动查找。",L"Use a Mac IPv4 address, or leave it blank to discover."))); }
    auto number=[&](int id,unsigned lo,unsigned hi) {
        auto s=value(fields[id]); size_t end=0; unsigned long n=0;
        try { n=std::stoul(s,&end); } catch(...) { end=0; }
        if(end==0 || end!=s.size() || n<lo || n>hi) throw std::runtime_error(utf8(tr(L"分辨率或端口无效。宽度 320–4096，高度 240–4096，端口 1–65535。",L"Invalid settings. Width 320–4096, height 240–4096, port 1–65535.")));
        return unsigned(n);
    };
    o.settings.width=uint16_t(number(Width,320,4096)); o.settings.height=uint16_t(number(Height,240,4096));
    if(o.settings.width%2 || o.settings.height%2) throw std::runtime_error(utf8(tr(L"宽度和高度必须是偶数。",L"Width and height must be even.")));
    int index=0;
    o.settings.bitrate=selectedBitrate?selectedBitrate:120000000; o.port=uint16_t(number(Port,1,65535));
    index=selection(fields[Codec]); o.uncompressed=index==3; o.settings.codecMask=o.uncompressed?4:index==0?3:index==1?2:1;
    o.fullscreen=checked(fields[Fullscreen]); o.vsync=checked(fields[Vsync]);
    o.autoQuality=selection(fields[Mode])==0; o.customBitrate=selectedBitrate!=0; o.bitrateMaximum=o.customBitrate && bitrateMaximum; o.localCursor=checked(fields[LocalCursor]); o.pixelExact=false;
    o.nativePixels=checked(fields[NativePixels]); o.clipboard=checked(fields[Clipboard]); o.scalingQuality=1;
    o.debugLogs=checked(fields[DebugLogs]);
    o.colorDepth=selection(fields[Depth])==2?10:selection(fields[Depth])==1?8:0;
    o.fullscreenHotkey=uint16_t(SendMessageW(fields[Hotkey],HKM_GETHOTKEY,0,0));
    if(!td::validFullscreenHotkey(o.fullscreenHotkey)) throw std::runtime_error(utf8(tr(L"全屏快捷键须包含 Ctrl 或 Alt；不可使用 F11、任务管理器、释放输入或系统安全组合键。",L"Fullscreen shortcut must include Ctrl or Alt. F11, Task Manager, release-input and secure-attention shortcuts are reserved.")));
    saved.fullscreenHotkey=o.fullscreenHotkey;
    int screenIndex=selection(fields[Screen]);
    if(screenIndex>=0 && size_t(screenIndex)<displays.size()) {
        auto& screen=displays[size_t(screenIndex)]; o.display=screen.preferred; o.displayBounds=screen.bounds; o.displayBits=screen.bits; selectedDevice=screen.device;
        o.settings.fps=std::min<uint16_t>(screen.preferred.hz,240);
        const auto ceiling=std::min(networkLink.bitrateLimit(),td::LegacyMaxBitrate);
        if(o.autoQuality) o.settings=td::bestQuality(screen.preferred,screen.preferred,o.settings.codecMask,false,o.customBitrate?o.settings.bitrate:0,ceiling);
        else if(!o.customBitrate) o.settings.bitrate=td::automaticBitrate(o.settings.width,o.settings.height,o.settings.fps,o.settings.codecMask,ceiling);
    } else throw std::runtime_error(utf8(tr(L"未检测到显示器，无法协商帧率，请重新检测。",L"No display found for refresh-rate negotiation. Refresh display detection.")));
    if(o.uncompressed) { o.colorDepth=10; o.nativePixels=true; o.settings.codecMask=8; o.customBitrate=false; o.bitrateMaximum=false; }
    remember=o.pairing && checked(fields[Remember]); savePreferences(o); return o;
}
bool SetupWindow::handleDialogMessage(MSG& msg) {
    if(!window) return false;
    if(msg.message==WM_MOUSEWHEEL && (msg.hwnd==window || IsChild(window,msg.hwnd))) {
        // Closed combo boxes and edit controls must not swallow page scrolling
        // or change a setting merely because they still own keyboard focus.
        bool dropdownOpen=false;
        for(auto id:{Language,Screen,Mode,Codec,Depth})
            if(SendMessageW(fields[id],CB_GETDROPPEDSTATE,0,0)) { dropdownOpen=true; break; }
        POINT point{GET_X_LPARAM(msg.lParam),GET_Y_LPARAM(msg.lParam)};
        RECT bounds{}; GetWindowRect(fields[Detail],&bounds);
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
        bool hasScrollInfo=GetScrollInfo(fields[Detail],SB_VERT,&si)!=FALSE;
        int limit=hasScrollInfo && si.nPage?std::max(0,si.nMax-int(si.nPage)+1):0;
        auto target=td::setupWheelTarget(dropdownOpen,PtInRect(&bounds,point)!=FALSE,GET_WHEEL_DELTA_WPARAM(msg.wParam),si.nPos,limit);
        if(target==td::SetupWheelTarget::Diagnostics) {
            SendMessageW(fields[Detail],WM_MOUSEWHEEL,msg.wParam,msg.lParam); return true;
        }
        if(target==td::SetupWheelTarget::Document) {
            message(WM_MOUSEWHEEL,msg.wParam,msg.lParam); return true;
        }
    }
    auto previousFocus=GetFocus();
    if(!IsDialogMessageW(window,&msg)) return false;
    revealFocusedControl(previousFocus!=GetFocus());
    return true;
}
void SetupWindow::revealFocusedControl(bool focusChanged) {
    if(!focusChanged) return;
    auto focused=GetFocus();
    for(auto c:controls) if(c.hwnd==focused || IsChild(c.hwnd,focused)) {
        int top=MulDiv(c.y,int(dpi),96), bottom=top;
        // Combo dropdown heights are larger than their collapsed controls.
        wchar_t kind[32]{}; GetClassNameW(c.hwnd,kind,32);
        bottom=top+MulDiv(wcscmp(kind,L"ComboBox")==0?32:c.h,int(dpi),96);
        int previous=scroll.position();
        scroll.reveal(top,bottom,focusChanged);
        if(scroll.position()!=previous) layout();
        break;
    }
}
void SetupWindow::present() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
void SetupWindow::setActive(bool connected) {
    active=connected;
    if(!active) { connectedLocalIPv4.clear(); lastSessionLink.clear(); nextLinkQuery=0; }
    for(auto id:{Host,UsePairing,Mode,Codec,Width,Height,Rate,RateValue,Port,Fullscreen,Vsync,Hotkey,Depth,NativePixels,Clipboard,LocalCursor,Connect,Screen}) EnableWindow(fields[id],!active);
    updatePairingControls(); updateQualityControls();
    EnableWindow(fields[Disconnect],active); EnableWindow(fields[ShowDisplay],active);
}
void SetupWindow::updatePairingControls() {
    bool enabled=!active && checked(fields[UsePairing]);
    for(auto id:{Code,ShowCode,Remember}) EnableWindow(fields[id],enabled);
}
void SetupWindow::status(const std::wstring& primary,const std::wstring& detail) {
    if(displayedStatus!=primary && SetWindowTextW(fields[Status],primary.c_str())) displayedStatus=primary;
    if(displayedDetail==detail) return;
    const auto log=fields[Detail];
    const auto firstLine=SendMessageW(log,EM_GETFIRSTVISIBLELINE,0,0);
    DWORD selectionStart=0,selectionEnd=0;
    SendMessageW(log,EM_GETSEL,reinterpret_cast<WPARAM>(&selectionStart),reinterpret_cast<LPARAM>(&selectionEnd));
    // WM_SETREDRAW(TRUE) makes a hidden control visible; preserve its own style.
    const bool visible=(GetWindowLongPtrW(log,GWL_STYLE)&WS_VISIBLE)!=0;
    if(visible) SendMessageW(log,WM_SETREDRAW,FALSE,0);
    if(SetWindowTextW(log,detail.c_str())) displayedDetail=detail;
    SendMessageW(log,EM_SETSEL,selectionStart,selectionEnd);
    // Restore the viewport after restoring the selection; never follow new text.
    const auto updatedFirstLine=SendMessageW(log,EM_GETFIRSTVISIBLELINE,0,0);
    SendMessageW(log,EM_LINESCROLL,0,firstLine-updatedFirstLine);
    if(visible) {
        SendMessageW(log,WM_SETREDRAW,TRUE,0);
        RedrawWindow(log,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
    }
}
LRESULT SetupWindow::message(UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CLOSE: ShowWindow(window,SW_HIDE); return 0;
    case WM_DESTROY: window=nullptr; PostQuitMessage(0); return 0;
    case WM_SIZE: layout(); return 0;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: updateTheme(); return 0;
    case WM_DISPLAYCHANGE: refreshDisplays(); updateQualityControls(); layout(); return 0;
    case WM_TIMER: if(w==11) { pollNetworkLink(); return 0; } break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); RECT area{}; GetClientRect(window,&area); FillRect(dc,&area,background); EndPaint(window,&ps); return 0; }
    case WM_DRAWITEM: drawControl(reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_MEASUREITEM: {
        auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);
        if(item && item->CtlType==ODT_COMBOBOX) { item->itemHeight=MulDiv(24,int(dpi),96); return TRUE; } break;
    }
    case WM_HSCROLL: {
        if(reinterpret_cast<HWND>(l)==fields[Rate] && !active) {
            const auto position=int(SendMessageW(fields[Rate],TBM_GETPOS,0,0));
            const auto limit=networkLink.bitrateLimit();
            const auto code=LOWORD(w);
            if(code==TB_LINEUP || code==TB_LINEDOWN) selectedBitrate=td::nextSliderBitrate(selectedBitrate,code==TB_LINEDOWN,limit);
            else selectedBitrate=td::magneticBitrate(td::sliderBitrate(position,limit),limit,selectedBitrate);
            bitrateMaximum=selectedBitrate==limit;
            updateBitrateControls(true); return 0;
        } break;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: {
        auto dc=reinterpret_cast<HDC>(w); int id=GetDlgCtrlID(reinterpret_cast<HWND>(l));
        bool outer=id==Heading||id==Intro||id==Status||id==Shortcuts||id==LanguageLabel;
        SetTextColor(dc,IsWindowEnabled(reinterpret_cast<HWND>(l))?theme.text:theme.muted);
        wchar_t kind[32]{}; GetClassNameW(reinterpret_cast<HWND>(l),kind,32); bool edit=_wcsicmp(kind,L"Edit")==0 || _wcsicmp(kind,L"ComboBox")==0 || m==WM_CTLCOLORLISTBOX;
        SetBkColor(dc,edit?theme.field:outer?theme.background:theme.card); return reinterpret_cast<LRESULT>(edit?fieldBackground:outer?background:cardBackground);
    }
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(l); info->ptMinTrackSize.x=MulDiv(680,int(dpi),96); info->ptMinTrackSize.y=MulDiv(400,int(dpi),96); return 0;
    }
    case WM_DPICHANGED: {
        finishLogResize();
        icons.apply(window);
        dpi=HIWORD(w); layoutWidth=-1; auto* r=reinterpret_cast<RECT*>(l); SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER); layout(); return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_ALL; GetScrollInfo(window,SB_VERT,&si);
        int position=scroll.position();
        switch(LOWORD(w)) { case SB_LINEUP: position-=MulDiv(30,int(dpi),96); break; case SB_LINEDOWN: position+=MulDiv(30,int(dpi),96); break;
        case SB_PAGEUP: position-=int(si.nPage); break; case SB_PAGEDOWN: position+=int(si.nPage); break;
        case SB_TOP: position=0; break; case SB_BOTTOM: position=si.nMax; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position=si.nTrackPos; break; default: return 0; }
        scroll.moveTo(position);
        layout(); return 0;
    }
    case WM_MOUSEWHEEL: scroll.wheel(GET_WHEEL_DELTA_WPARAM(w),MulDiv(60,int(dpi),96)); layout(); return 0;
    case WM_COMMAND: {
        int id=LOWORD(w),notification=HIWORD(w);
        if(id==Connect && notification==BN_CLICKED && !active) {
            try { auto o=read(); if(onConnect) onConnect(std::move(o)); }
            catch(const std::exception& e) { status(tr(L"请检查连接设置",L"Check connection settings"),wide(e.what())); }
        } else if(id==Disconnect && notification==BN_CLICKED) { if(onDisconnect) onDisconnect(); }
        else if(id==ShowDisplay && notification==BN_CLICKED) { if(onShowDisplay) onShowDisplay(); }
        else if(id==DebugLogs && notification==BN_CLICKED) {
            saved.debugLogs=checked(fields[DebugLogs]);
            if(onDebugLogs) onDebugLogs(saved.debugLogs);
            HKEY key=nullptr;
            if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS) { putNumber(key,L"DebugLogs",saved.debugLogs); RegCloseKey(key); }
        }
        else if(id==Hotkey && notification==EN_CHANGE) {
            auto shortcut=uint16_t(SendMessageW(fields[Hotkey],HKM_GETHOTKEY,0,0));
            if(td::validFullscreenHotkey(shortcut)) {
                saved.fullscreenHotkey=shortcut; if(onShortcut) onShortcut(shortcut);
                HKEY key=nullptr;
                if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS) { putNumber(key,L"FullscreenHotkey",shortcut); RegCloseKey(key); }
            }
        }
        else if(id==RateValue && !active && !updatingBitrateControls && fields.count(Rate)) {
            if(notification==EN_SETFOCUS) { bitrateBeforeEdit=selectedBitrate; bitrateMaximumBeforeEdit=bitrateMaximum; }
            else if(notification==EN_CHANGE) {
                if(auto parsed=td::parseBitrateMbps(utf8(value(fields[RateValue])),networkLink.bitrateLimit())) {
                    selectedBitrate=*parsed; bitrateMaximum=false; updateBitrateControls();
                }
            } else if(notification==EN_KILLFOCUS) commitBitrateInput();
        }
        else if(id==UsePairing && notification==BN_CLICKED) { updatePairingControls(); layout(); }
        else if(id==Detect && notification==BN_CLICKED) { nextLinkQuery=0; pollNetworkLink(); refreshDisplays(); updateQualityControls(); layout(); }
        else if(id==Host && notification==EN_CHANGE) { nextLinkQuery=0; }
        else if(id==Screen && notification==CBN_SELCHANGE) { describeDisplay(); updateQualityControls(); }
        else if(id==ShowCode && notification==BN_CLICKED) { SendMessageW(fields[Code],EM_SETPASSWORDCHAR,checked(fields[ShowCode])?0:L'●',0); InvalidateRect(fields[Code],nullptr,TRUE); }
        else if(id==Remember && notification==BN_CLICKED && !checked(fields[Remember])) {
            HKEY key=nullptr;
            if(RegOpenKeyExW(HKEY_CURRENT_USER,registryPath,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS) { RegDeleteValueW(key,L"PairingCode"); RegCloseKey(key); }
        } else if(id==Language && notification==CBN_SELCHANGE) {
            uiLanguage=selection(fields[Language]); translate(); updateQualityControls(); if(onLanguage) onLanguage();
            HKEY key=nullptr;
            if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS) { putNumber(key,L"Language",DWORD(uiLanguage)); RegCloseKey(key); }
        }
        else if(id==Mode && notification==CBN_SELCHANGE) {
            updateQualityControls(); layout();
        } else if(id==Codec && notification==CBN_SELCHANGE) updateQualityControls();
        else if((id==Width || id==Height) && notification==EN_CHANGE && fields.count(Mode) && !applyingPreset) { select(fields[Mode],1); }
        return 0;
    }
    }
    return DefWindowProcW(window,m,w,l);
}
LRESULT CALLBACK SetupWindow::procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    auto* self=reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE) { self=reinterpret_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window=h; SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    return self?self->message(m,w,l):DefWindowProcW(h,m,w,l);
}
void SetupWindow::loadPreferences() {
    // Explicit command-line settings take precedence over saved form settings.
    HKEY key=nullptr; if(RegOpenKeyExW(HKEY_CURRENT_USER,registryPath,0,KEY_READ,&key)!=ERROR_SUCCESS) return;
    logHeight=td::SetupLayout::boundedLogHeight(int(std::min<DWORD>(readNumber(key,L"LogPanelHeight",td::SetupLayout::DefaultLogHeight),td::SetupLayout::MaxLogHeight)));
    if(saved.explicitSettings) { RegCloseKey(key); return; }
    saved.host=utf8(readString(key,L"Host")); selectedDevice=readString(key,L"Display");
    saved.autoQuality=readNumber(key,L"AutoQuality",1)!=0;
    saved.customBitrate=readNumber(key,L"CustomBitrate",0)!=0 || (!saved.autoQuality && !readNumber(key,L"BitrateSliderVersion",0));
    saved.uncompressed=readNumber(key,L"Uncompressed",0)!=0;
    saved.debugLogs=readNumber(key,L"DebugLogs",0)!=0;
    saved.localCursor=readNumber(key,L"LocalCursor",0)!=0; saved.pixelExact=false;
    saved.nativePixels=readNumber(key,L"NativePixels",1)!=0; saved.clipboard=readNumber(key,L"Clipboard",1)!=0;
    // Migrate the previous sharpening default to plain resampling.
    saved.scalingQuality=1;
    auto hotkey=uint16_t(readNumber(key,L"FullscreenHotkey",td::DefaultFullscreenHotkey));
    saved.fullscreenHotkey=td::validFullscreenHotkey(hotkey)?hotkey:td::DefaultFullscreenHotkey;
    auto depth=readNumber(key,L"ColorDepth",0); saved.colorDepth=depth==8?8:depth==10?10:0;
    auto bounded=[&](const wchar_t* name,DWORD fallback,DWORD lo,DWORD hi) { return std::clamp(readNumber(key,name,fallback),lo,hi); };
    saved.settings.width=uint16_t(bounded(L"Width",2560,320,4096)); saved.settings.height=uint16_t(bounded(L"Height",1600,240,4096));
    saved.settings.fps=uint16_t(bounded(L"FPS",120,1,240));
    saved.settings.bitrate=uint64_t(bounded(L"Bitrate",120,10,DWORD(td::MaxBitrate/1000000)))*1000000;
    saved.bitrateMaximum=readNumber(key,L"BitrateAtMaximum",0)!=0; saved.port=uint16_t(bounded(L"Port",47990,1,65535));
    saved.settings.codecMask=uint8_t(bounded(L"Codec",3,1,3)); saved.fullscreen=readNumber(key,L"Fullscreen",0)!=0; saved.vsync=readNumber(key,L"Vsync",0)!=0;
    uiLanguage=int(bounded(L"Language",0,0,2));
    saved.pairing=readNumber(key,L"PairingEnabled",0)!=0;
    DWORD size=0,type=0;
    if(saved.pairing && RegQueryValueExW(key,L"PairingCode",nullptr,&type,nullptr,&size)==ERROR_SUCCESS && type==REG_BINARY && size<8192 && size>0) {
        std::vector<BYTE> encrypted(size);
        if(RegQueryValueExW(key,L"PairingCode",nullptr,nullptr,encrypted.data(),&size)==ERROR_SUCCESS) {
            DATA_BLOB input{size,encrypted.data()},out{};
            if(CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) {
                if(out.cbData==32) { saved.token.assign(reinterpret_cast<char*>(out.pbData),out.cbData); remember=true; }
                SecureZeroMemory(out.pbData,out.cbData); LocalFree(out.pbData);
            }
        }
    }
    RegCloseKey(key);
}
void SetupWindow::savePreferences(const ClientOptions& o) {
    HKEY key=nullptr; if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return;
    auto host=wide(o.host); RegSetValueExW(key,L"Host",0,REG_SZ,reinterpret_cast<const BYTE*>(host.c_str()),DWORD((host.size()+1)*sizeof(wchar_t)));
    putNumber(key,L"Width",o.settings.width); putNumber(key,L"Height",o.settings.height); putNumber(key,L"FPS",o.settings.fps);
    putNumber(key,L"Bitrate",o.settings.bitrate/1000000); putNumber(key,L"Port",o.port); putNumber(key,L"Codec",o.settings.codecMask);
    putNumber(key,L"PairingEnabled",o.pairing); putNumber(key,L"AutoQuality",o.autoQuality); putNumber(key,L"CustomBitrate",o.customBitrate); putNumber(key,L"LocalCursor",o.localCursor);
    putNumber(key,L"Uncompressed",o.uncompressed);
    putNumber(key,L"DebugLogs",o.debugLogs);
    putNumber(key,L"LogPanelHeight",DWORD(logHeight));
    putNumber(key,L"BitrateAtMaximum",o.bitrateMaximum);
    putNumber(key,L"BitrateSliderVersion",1);
    RegSetValueExW(key,L"Display",0,REG_SZ,reinterpret_cast<const BYTE*>(selectedDevice.c_str()),DWORD((selectedDevice.size()+1)*sizeof(wchar_t)));
    putNumber(key,L"FullscreenHotkey",o.fullscreenHotkey); putNumber(key,L"PixelExact",o.pixelExact); putNumber(key,L"ColorDepth",o.colorDepth);
    putNumber(key,L"NativePixels",o.nativePixels); putNumber(key,L"Clipboard",o.clipboard); putNumber(key,L"ScalingQuality",o.scalingQuality);
    putNumber(key,L"Fullscreen",o.fullscreen); putNumber(key,L"Vsync",o.vsync); putNumber(key,L"Language",DWORD(uiLanguage));
    RegDeleteValueW(key,L"PairingCode");
    if(remember) {
        DATA_BLOB in{DWORD(o.token.size()),reinterpret_cast<BYTE*>(const_cast<char*>(o.token.data()))},out{};
        if(CryptProtectData(&in,L"ThunderDisplay pairing code",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) {
            RegSetValueExW(key,L"PairingCode",0,REG_BINARY,out.pbData,out.cbData); SecureZeroMemory(out.pbData,out.cbData); LocalFree(out.pbData);
        }
    }
    RegCloseKey(key);
}

void SetupWindow::refreshDisplays() {
    int old=selection(fields[Screen]);
    if(old>=0 && size_t(old)<displays.size()) selectedDevice=displays[size_t(old)].device;
    displays=detectDisplays(); SendMessageW(fields[Screen],CB_RESETCONTENT,0,0);
    int selected=0;
    for(size_t i=0;i<displays.size();++i) {
        auto text=displays[i].name+L" · "+std::to_wstring(displays[i].preferred.width)+L" × "+std::to_wstring(displays[i].preferred.height);
        SendMessageW(fields[Screen],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        if(displays[i].device==selectedDevice) selected=int(i);
    }
    select(fields[Screen],selected); describeDisplay();
}
void SetupWindow::describeDisplay() {
    auto dimensions=[](td::DisplayLimits limits) {
        return std::to_wstring(limits.width)+L" × "+std::to_wstring(limits.height)+L" @ "+std::to_wstring(limits.hz)+L" Hz";
    };
    int selected=selection(fields[Screen]);
    if(selected<0 || size_t(selected)>=displays.size()) {
        SetWindowTextW(fields[DisplayInfo],tr(L"未检测到显示器。请重新检测或使用手动模式。",L"No display detected. Refresh or use manual settings.")); return;
    }
    auto& screen=displays[size_t(selected)];
    auto hdr=std::wstring(screen.hdrKnown?(screen.hdrSupported?(screen.hdrEnabled?tr(L"HDR 已开启",L"HDR on"):tr(L"HDR 可用",L"HDR available")):L"SDR"):
        (screen.advancedColor?tr(L"高级色彩可用 · HDR 未知",L"Advanced color · HDR unknown"):tr(L"HDR 未知",L"HDR unknown")));
    auto precision=screen.bits?std::to_wstring(screen.bits)+tr(L"-bit 输出",L"-bit output"):tr(L"输出色深未知",L"Output precision unknown");
    auto text=std::wstring(tr(L"当前 ",L"Current "))+dimensions(screen.current)+tr(L"  ·  推荐 ",L"  ·  Preferred ")+dimensions(screen.preferred)+
        L"\r\n"+tr(L"最大模式 ",L"Largest mode ")+dimensions(screen.maximum)+L"  ·  "+hdr+L"  ·  "+precision;
    SetWindowTextW(fields[DisplayInfo],text.c_str());
    SetWindowTextW(fields[StreamInfo],tr(L"串流：sRGB SDR · 8 / 10-bit · 4:2:0  ｜ 旧端使用兼容色彩",L"Stream: sRGB SDR · 8 / 10-bit · 4:2:0  |  Legacy color with older hosts"));
}
void SetupWindow::updateQualityControls() {
    bool automatic=selection(fields[Mode])==0;
    for(auto id:{Width,Height}) EnableWindow(fields[id],!active && !automatic);
    const bool raw=selection(fields[Codec])==3;
    EnableWindow(fields[Rate],!active && !raw); EnableWindow(fields[RateValue],!active && !raw);
    EnableWindow(fields[Depth],!active && !raw);
    if(raw) { select(fields[Depth],2); checkBox(fields[NativePixels],true); }
    EnableWindow(fields[NativePixels],!active && automatic && !raw);
    std::wstring hint=tr(L"最左侧自动协商码率；向右调节压缩强度，最右侧使用当前链路上限。",L"Far left selects automatic bitrate; move right for less compression, up to the link limit.");
    int selected=selection(fields[Screen]);
    if(automatic && selected>=0 && size_t(selected)<displays.size() && !active) {
        try {
            auto limits=displays[size_t(selected)].preferred; int codec=selection(fields[Codec]);
            auto settings=td::bestQuality(limits,limits,codec==2?1:codec==1?2:3);
            applyingPreset=true;
            SetWindowTextW(fields[Width],std::to_wstring(settings.width).c_str()); SetWindowTextW(fields[Height],std::to_wstring(settings.height).c_str());
            applyingPreset=false;
        } catch(const std::exception& e) { hint=wide(e.what()); }
    }
    if(raw) hint=tr(L"无压缩保留 10 位，支持仅传变化区域。帧率随两端屏幕协商；全画面持续变化仍需要足够带宽，码率滑块不适用。",L"Uncompressed 10-bit with changed-region updates. Frame rate follows both displays; continuous full-screen changes still require sufficient bandwidth. The bitrate slider is unused.");
    SetWindowTextW(fields[AutoHint],hint.c_str());
    updateBitrateControls();
}
void SetupWindow::paint(HDC dc) {
    RECT area{}; GetClientRect(content,&area); RECT viewport{}; GetClientRect(window,&viewport);
    auto buffer=CreateCompatibleDC(dc); auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,area.right),std::max<LONG>(1,area.bottom)); auto previous=SelectObject(buffer,bitmap);
    FillRect(buffer,&area,background);
    auto scale=[&](int value){return MulDiv(value,int(dpi),96);};
    td::SetupLayout layout{MulDiv(area.right,96,int(dpi)),MulDiv(viewport.bottom,96,int(dpi)),checked(fields[UsePairing]),selection(fields[Mode])!=0,logHeight};
    auto pen=CreatePen(PS_SOLID,1,theme.border); auto oldPen=SelectObject(buffer,pen); auto oldBrush=SelectObject(buffer,cardBackground);
    for(auto card:{std::pair<int,int>{218,176},{layout.networkTop(),layout.networkHeight()},{layout.qualityTop(),layout.qualityHeight()},
        {layout.diagnosticsTop(),layout.diagnosticsHeight()}}) {
        RoundRect(buffer,scale(28),scale(card.first),area.right-scale(28),scale(card.first+card.second),scale(24),scale(24));
    }
    SelectObject(buffer,oldPen); DeleteObject(pen); SelectObject(buffer,oldBrush);
    BitBlt(dc,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,previous); DeleteObject(bitmap); DeleteDC(buffer);
}
void SetupWindow::drawControl(DRAWITEMSTRUCT* item) {
    if(!item) return;
    if(item->CtlID==LogResize && item->CtlType==ODT_BUTTON) {
        auto dc=item->hDC; auto rect=item->rcItem;
        auto scale=[&](int value) { return MulDiv(value,int(dpi),96); };
        FillRect(dc,&rect,cardBackground);
        const auto color=resizingLog || (item->itemState&ODS_FOCUS)?theme.accent:theme.muted;
        const auto oldFont=SelectObject(dc,hintFont);
        auto text=value(item->hwndItem); SIZE textSize{}; GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&textSize);
        int left=std::max(scale(8),(int(rect.right)-scale(44+12)-int(textSize.cx))/2);
        auto brush=CreateSolidBrush(color); auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        const int middle=(rect.top+rect.bottom)/2;
        RoundRect(dc,left,middle-scale(2),left+scale(44),middle+scale(2),scale(4),scale(4));
        SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(brush);
        rect.left=left+scale(56); rect.right-=scale(8);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
        DrawTextW(dc,text.c_str(),-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,oldFont);
        if((item->itemState&ODS_FOCUS) && !(item->itemState&ODS_NOFOCUSRECT)) {
            rect=item->rcItem; InflateRect(&rect,-scale(4),-scale(2)); DrawFocusRect(dc,&rect);
        }
        return;
    }
    if(item->CtlType==ODT_COMBOBOX) {
        const bool selected=(item->itemState&ODS_SELECTED)!=0 && !(item->itemState&ODS_COMBOBOXEDIT);
        const bool enabled=IsWindowEnabled(item->hwndItem)!=FALSE;
        auto dc=item->hDC; auto rect=item->rcItem;
        auto brush=CreateSolidBrush(selected?theme.accent:theme.field); FillRect(dc,&rect,brush); DeleteObject(brush);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,!enabled?theme.muted:selected?(theme.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)):theme.text);
        auto face=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));
        auto oldFont=SelectObject(dc,face?face:GetStockObject(DEFAULT_GUI_FONT));
        if(item->itemID!=UINT(-1)) {
            const auto length=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,item->itemID,0);
            if(length>=0) {
                std::wstring text(size_t(length)+1,L'\0'); SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(text.data()));
                rect.left+=MulDiv(10,int(dpi),96); rect.right-=MulDiv(6,int(dpi),96);
                DrawTextW(dc,text.c_str(),int(length),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
            }
        }
        if((item->itemState&ODS_FOCUS) && !(item->itemState&ODS_NOFOCUSRECT)) DrawFocusRect(dc,&rect);
        SelectObject(dc,oldFont); return;
    }
    if(item->CtlType!=ODT_BUTTON) return;
    auto dc=item->hDC; auto rect=item->rcItem; bool enabled=IsWindowEnabled(item->hwndItem)!=FALSE,primary=item->CtlID==Connect;
    COLORREF fill=enabled?(primary?theme.accent:theme.button):theme.field;
    if(item->itemState&ODS_SELECTED) fill=primary?theme.accent:theme.pressed;
    FillRect(dc,&rect,item->CtlID==Detect?cardBackground:background);
    auto brush=CreateSolidBrush(fill); auto pen=CreatePen(PS_SOLID,1,primary?fill:theme.border); auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    auto radius=MulDiv(18,int(dpi),96); RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,!enabled?theme.muted:primary?RGB(255,255,255):theme.text);
    auto oldFont=SelectObject(dc,font); auto text=value(item->hwndItem); DrawTextW(dc,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(item->itemState&ODS_FOCUS) { InflateRect(&rect,-5,-5); DrawFocusRect(dc,&rect); }
    SelectObject(dc,oldFont); SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
}

void SetupWindow::updateTheme() {
    if(updatingTheme) return;
    updatingTheme=true;
    if(window) icons.apply(window);
    theme=td::UITheme::system();
    for(auto brush:{background,cardBackground,fieldBackground}) if(brush) DeleteObject(brush);
    background=CreateSolidBrush(theme.background); cardBackground=CreateSolidBrush(theme.card); fieldBackground=CreateSolidBrush(theme.field);
    theme.apply(window); if(content) theme.apply(content);
    for(auto& control:controls) {
        theme.apply(control.hwnd);
        COMBOBOXINFO info{}; info.cbSize=sizeof(info);
        if(GetComboBoxInfo(control.hwnd,&info)) { theme.apply(info.hwndList); theme.apply(info.hwndItem); }
    }
    if(window) RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    updatingTheme=false;
}
