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
    Status,Detail,Shortcuts,LanguageLabel,Language,ScreenLabel,Screen,Detect,DisplayInfo,StreamInfo,DisplaySection,ConnectionSection,QualitySection,DiagnosticSection,AutoHint,HotkeyLabel,Hotkey,PixelExact,DepthLabel,Depth,NativePixels,ScalingLabel,Scaling,Clipboard,RateOverride,LocalCursor };
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
}
SetupWindow::SetupWindow(ClientOptions initial): saved(std::move(initial)) {
    loadPreferences();
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
}
SetupWindow::~SetupWindow() {
    if(window && IsWindow(window)) DestroyWindow(window);
    if(font) DeleteObject(font); if(headingFont) DeleteObject(headingFont); if(sectionFont) DeleteObject(sectionFont); if(hintFont) DeleteObject(hintFont);
    if(background) DeleteObject(background); if(cardBackground) DeleteObject(cardBackground); if(fieldBackground) DeleteObject(fieldBackground);
}
HWND SetupWindow::add(int id,const wchar_t* kind,DWORD style,int x,int y,int w,int h,const wchar_t* text) {
    auto child=CreateWindowExW(0,kind,text,
        WS_CHILD|WS_VISIBLE|style,0,0,1,1,content,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    if(!child) throw std::runtime_error("Cannot create setup control");
    theme.apply(child);
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
    add(RateOverride,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,140,30); checkBox(fields[RateOverride],saved.customBitrate);
    add(RateLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Rate,L"EDIT",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,188,0,202,32);
    add(PortLabel,L"STATIC",SS_LEFT,418,0,72,24); add(Port,L"EDIT",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,498,0,266,32);
    add(Fullscreen,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,350,26); add(Vsync,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,414,0,350,26);
    add(DepthLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Depth,L"COMBOBOX",CBS_DROPDOWNLIST|WS_TABSTOP,200,0,564,180);
    add(HotkeyLabel,L"STATIC",SS_LEFT,48,0,140,24); add(Hotkey,HOTKEY_CLASSW,WS_TABSTOP,200,0,564,32);
    SendMessageW(fields[Hotkey],HKM_SETHOTKEY,saved.fullscreenHotkey,0);
    SetWindowSubclass(fields[Hotkey],hotkeyProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    add(NativePixels,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[NativePixels],saved.nativePixels);
    add(LocalCursor,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[LocalCursor],saved.localCursor);
    add(Clipboard,L"BUTTON",BS_AUTOCHECKBOX|WS_TABSTOP,48,0,716,26); checkBox(fields[Clipboard],saved.clipboard);
    add(DiagnosticSection,L"STATIC",SS_LEFT,48,0,400,28);
    add(Detail,L"EDIT",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,48,0,716,88);
    add(Shortcuts,L"STATIC",SS_LEFT,28,0,756,60);
    SetWindowTextW(fields[Host],wide(saved.host).c_str()); SetWindowTextW(fields[Code],wide(saved.token).c_str());
    SetWindowTextW(fields[Width],std::to_wstring(saved.settings.width).c_str()); SetWindowTextW(fields[Height],std::to_wstring(saved.settings.height).c_str());
    SetWindowTextW(fields[Rate],std::to_wstring(saved.settings.bitrate/1000000).c_str()); SetWindowTextW(fields[Port],std::to_wstring(saved.port).c_str());
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
    label(ModeLabel,L"显示模式",L"Display mode"); label(CodecLabel,L"视频编码",L"Video codec"); label(WidthLabel,L"宽度",L"Width"); label(HeightLabel,L"高度",L"Height"); label(RateOverride,L"自定义码率",L"Custom Mbps");
    label(RateLabel,L"码率（Mbps）",L"Bitrate (Mbps)"); label(PortLabel,L"端口",L"Port");
    label(Fullscreen,L"连接后全屏",L"Fullscreen after connecting"); label(Vsync,L"垂直同步",L"VSync");
    label(Connect,L"连接 Mac",L"Connect to Mac"); label(Disconnect,L"断开",L"Disconnect"); label(ShowDisplay,L"打开远程画面",L"Show remote display");
    label(HotkeyLabel,L"全屏快捷键",L"Fullscreen shortcut"); label(DepthLabel,L"串流色深",L"Stream precision");
    label(NativePixels,L"清晰度优先：保留 Mac HiDPI 像素（自动模式）",L"Preserve Mac HiDPI pixels (Auto mode, best detail)");
    label(LocalCursor,L"本地指针 · macOS 原生（同步系统形状与点击热点）",L"Local native macOS cursor (system shape and click hotspot)");
    label(Clipboard,L"双向剪贴板：文字与图片（连接后复制）",L"Sync copied text and images (new copies only)");
    label(Shortcuts,L"在全屏快捷键框中直接按组合键（需含 Ctrl 或 Alt）。\r\nCtrl+Alt+Shift+Esc 释放输入 · Ctrl+Shift+Esc 任务管理器",L"Press a key combination in the shortcut field (include Ctrl or Alt).\r\nCtrl+Alt+Shift+Esc releases input · Ctrl+Shift+Esc opens Task Manager");
    auto combo=[&](int id,std::initializer_list<const wchar_t*> items,int fallback) {
        int index=selection(fields[id]); if(index<0) index=fallback;
        SendMessageW(fields[id],CB_RESETCONTENT,0,0);
        for(auto* item:items) SendMessageW(fields[id],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item)); select(fields[id],index);
    };
    combo(Language,{tr(L"跟随系统",L"System default"),L"中文",L"English"},uiLanguage);
    combo(Mode,{tr(L"自动",L"Auto"),tr(L"自定义",L"Custom")},saved.autoQuality?0:1);
    combo(Codec,{tr(L"自动（优先 HEVC）",L"Auto (prefer HEVC)"),L"HEVC / H.265",L"H.264"},saved.settings.codecMask==3?0:saved.settings.codecMask==2?1:2);
    combo(Depth,{tr(L"自动（支持时使用 10-bit SDR）",L"Auto (10-bit SDR when supported)"),L"8-bit SDR",L"10-bit SDR / HEVC Main10"},saved.colorDepth==10?2:saved.colorDepth==8?1:0);
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
    td::SetupLayout metrics{logicalWidth,logicalHeight,checked(fields[UsePairing]),selection(fields[Mode])!=0};
    SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS; si.nMax=scale(metrics.contentHeight())-1; si.nPage=UINT(std::max<LONG>(1,r.bottom));
    scroll.resize(si.nMax+1,int(si.nPage)); si.nPos=scroll.position(); SetScrollInfo(window,SB_VERT,&si,TRUE);
    GetClientRect(window,&r); logicalWidth=MulDiv(r.right,96,int(dpi)); metrics.width=logicalWidth;
    if(layoutWidth==r.right && layoutHeight==r.bottom && layoutPaired==metrics.paired && layoutManual==metrics.manual) { scrollContent(); return; }
    layoutWidth=r.right; layoutHeight=r.bottom; layoutPaired=metrics.paired; layoutManual=metrics.manual;
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
        case DisplayInfo: case StreamInfo: case AutoHint: w=logicalWidth-96; break;
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
        case RateLabel: x=48; w=140; y=metrics.qualityTop()+244; visible=metrics.manual; break;
        case RateOverride: x=48; w=140; y=metrics.qualityTop()+238; visible=!metrics.manual; break;
        case Rate: x=200; w=metrics.fieldWidth(); y=metrics.qualityTop()+238; break;
        case Fullscreen: case Vsync: {
            auto cell=metrics.column(id==Vsync?1:0,2,18); x=cell.x; w=cell.width; y=metrics.qualityTop()+298; break;
        }
        case Detail: x=48; w=logicalWidth-96; y=metrics.diagnosticsTop()+58; h=88+metrics.extraHeight(); break;
        }
        if(id==QualitySection) y=metrics.qualityTop()+18;
        if(id==ModeLabel) y=metrics.qualityTop()+66;
        if(id==Mode) y=metrics.qualityTop()+60;
        if(id==CodecLabel) y=metrics.qualityTop()+110;
        if(id==Codec) y=metrics.qualityTop()+104;
        if(id==DepthLabel) y=metrics.qualityTop()+154;
        if(id==Depth) y=metrics.qualityTop()+148;
        if(id==HotkeyLabel) y=metrics.qualityTop()+350;
        if(id==Hotkey) y=metrics.qualityTop()+344;
        if(id==NativePixels) { y=metrics.qualityTop()+394; w=logicalWidth-96; }
        if(id==LocalCursor) { y=metrics.qualityTop()+470; w=logicalWidth-96; }
        if(id==Clipboard) { y=metrics.qualityTop()+432; w=logicalWidth-96; }
        if(id==AutoHint) { y=metrics.qualityTop()+192; h=40; visible=!metrics.manual; }
        if(id==DiagnosticSection) y=metrics.diagnosticsTop()+18;
        if(id==Shortcuts) y=metrics.diagnosticsTop()+174+metrics.extraHeight();
        if(id==Code || id==ShowCode || id==Remember) visible=metrics.paired;
        c.x=x; c.y=y;
        HFONT face=id==Heading?headingFont:(id==DisplaySection||id==ConnectionSection||id==QualitySection||id==DiagnosticSection)?sectionFont:font;
        if(id==DisplayInfo||id==StreamInfo||id==HostHint||id==AutoHint||id==Shortcuts||id==Intro) face=hintFont;
        if(c.appliedFont!=face) { SendMessageW(c.hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(face),FALSE); c.appliedFont=face; }
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
    td::SetupLayout metrics{MulDiv(r.right,96,int(dpi)),MulDiv(r.bottom,96,int(dpi)),checked(fields[UsePairing]),selection(fields[Mode])!=0};
    SetWindowPos(content,nullptr,0,-scroll.position(),r.right,MulDiv(metrics.contentHeight(),int(dpi),96),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
LRESULT CALLBACK SetupWindow::contentProcedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    auto self=reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE) { self=static_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if(self) {
        if(m==WM_PAINT) { PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); self->paint(dc); EndPaint(h,&ps); return 0; }
        if(m==WM_ERASEBKGND) return 1;
        if(m==WM_COMMAND || m==WM_DRAWITEM || m==WM_CTLCOLORLISTBOX || m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN || m==WM_CTLCOLOREDIT || m==WM_MOUSEWHEEL) return self->message(m,w,l);
    }
    return DefWindowProcW(h,m,w,l);
}
LRESULT CALLBACK SetupWindow::hotkeyProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto self=reinterpret_cast<SetupWindow*>(data);
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
ClientOptions SetupWindow::read() {
    ClientOptions o; o.host=utf8(value(fields[Host])); o.pairing=checked(fields[UsePairing]);
    o.token=o.pairing?utf8(value(fields[Code])):std::string{};
    for(auto& c:o.token) c=char(std::tolower(static_cast<unsigned char>(c)));
    if(o.pairing && (o.token.size()!=32 || !std::all_of(o.token.begin(),o.token.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})))
        throw std::runtime_error(utf8(tr(L"请粘贴 Mac 设置窗口中的 32 位配对码。",L"Paste the 32-character pairing code from the Mac setup window.")));
    if(!o.host.empty()) { IN_ADDR address{}; if(InetPtonA(AF_INET,o.host.c_str(),&address)!=1 || address.S_un.S_addr==0) throw std::runtime_error(utf8(tr(L"Mac 地址必须为 IPv4，或留空自动查找。",L"Use a Mac IPv4 address, or leave it blank to discover."))); }
    auto number=[&](int id,unsigned lo,unsigned hi) {
        auto s=value(fields[id]); size_t end=0; unsigned long n=0;
        try { n=std::stoul(s,&end); } catch(...) { end=0; }
        if(end==0 || end!=s.size() || n<lo || n>hi) throw std::runtime_error(utf8(tr(L"分辨率、码率或端口无效。宽度 320–4096，高度 240–4096，码率 10–20000 Mbps，端口 1–65535。",L"Invalid settings. Width 320–4096, height 240–4096, bitrate 10–20000 Mbps, port 1–65535.")));
        return unsigned(n);
    };
    o.settings.width=uint16_t(number(Width,320,4096)); o.settings.height=uint16_t(number(Height,240,4096));
    if(o.settings.width%2 || o.settings.height%2) throw std::runtime_error(utf8(tr(L"宽度和高度必须是偶数。",L"Width and height must be even.")));
    int index=0;
    o.settings.bitrate=uint64_t(number(Rate,10,20000))*1000000; o.port=uint16_t(number(Port,1,65535));
    index=selection(fields[Codec]); o.settings.codecMask=index==0?3:index==1?2:1;
    o.fullscreen=checked(fields[Fullscreen]); o.vsync=checked(fields[Vsync]);
    o.autoQuality=selection(fields[Mode])==0; o.customBitrate=checked(fields[RateOverride]); o.localCursor=checked(fields[LocalCursor]); o.pixelExact=false;
    o.nativePixels=checked(fields[NativePixels]); o.clipboard=checked(fields[Clipboard]); o.scalingQuality=1;
    o.colorDepth=selection(fields[Depth])==2?10:selection(fields[Depth])==1?8:0;
    o.fullscreenHotkey=uint16_t(SendMessageW(fields[Hotkey],HKM_GETHOTKEY,0,0));
    if(!td::validFullscreenHotkey(o.fullscreenHotkey)) throw std::runtime_error(utf8(tr(L"全屏快捷键须包含 Ctrl 或 Alt；不可使用 F11、任务管理器、释放输入或系统安全组合键。",L"Fullscreen shortcut must include Ctrl or Alt. F11, Task Manager, release-input and secure-attention shortcuts are reserved.")));
    saved.fullscreenHotkey=o.fullscreenHotkey;
    int screenIndex=selection(fields[Screen]);
    if(screenIndex>=0 && size_t(screenIndex)<displays.size()) {
        auto& screen=displays[size_t(screenIndex)]; o.display=screen.preferred; o.displayBounds=screen.bounds; o.displayBits=screen.bits; selectedDevice=screen.device;
        o.settings.fps=std::min<uint16_t>(screen.preferred.hz,240);
        if(o.autoQuality) o.settings=td::bestQuality(screen.preferred,screen.preferred,o.settings.codecMask,false,o.customBitrate?o.settings.bitrate:0);
    } else throw std::runtime_error(utf8(tr(L"未检测到显示器，无法协商帧率，请重新检测。",L"No display found for refresh-rate negotiation. Refresh display detection.")));
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
    for(auto id:{Host,UsePairing,Mode,Codec,Width,Height,Rate,RateOverride,Port,Fullscreen,Vsync,Hotkey,Depth,NativePixels,Clipboard,LocalCursor,Connect,Screen}) EnableWindow(fields[id],!active);
    updatePairingControls(); updateQualityControls();
    EnableWindow(fields[Disconnect],active); EnableWindow(fields[ShowDisplay],active);
}
void SetupWindow::updatePairingControls() {
    bool enabled=!active && checked(fields[UsePairing]);
    for(auto id:{Code,ShowCode,Remember}) EnableWindow(fields[id],enabled);
}
void SetupWindow::status(const std::wstring& primary,const std::wstring& detail) {
    SetWindowTextW(fields[Status],primary.c_str()); SetWindowTextW(fields[Detail],detail.c_str());
}
LRESULT SetupWindow::message(UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CLOSE: if(onDisconnect) onDisconnect(); DestroyWindow(window); return 0;
    case WM_DESTROY: window=nullptr; PostQuitMessage(0); return 0;
    case WM_SIZE: layout(); return 0;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: updateTheme(); return 0;
    case WM_DISPLAYCHANGE: refreshDisplays(); updateQualityControls(); layout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); RECT area{}; GetClientRect(window,&area); FillRect(dc,&area,background); EndPaint(window,&ps); return 0; }
    case WM_DRAWITEM: drawControl(reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
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
        else if(id==Hotkey && notification==EN_CHANGE) {
            auto shortcut=uint16_t(SendMessageW(fields[Hotkey],HKM_GETHOTKEY,0,0));
            if(td::validFullscreenHotkey(shortcut)) {
                saved.fullscreenHotkey=shortcut; if(onShortcut) onShortcut(shortcut);
                HKEY key=nullptr;
                if(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS) { putNumber(key,L"FullscreenHotkey",shortcut); RegCloseKey(key); }
            }
        }
        else if(id==UsePairing && notification==BN_CLICKED) { updatePairingControls(); layout(); }
        else if(id==RateOverride && notification==BN_CLICKED) updateQualityControls();
        else if(id==Detect && notification==BN_CLICKED) { refreshDisplays(); updateQualityControls(); layout(); }
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
    if(saved.explicitSettings) return;
    HKEY key=nullptr; if(RegOpenKeyExW(HKEY_CURRENT_USER,registryPath,0,KEY_READ,&key)!=ERROR_SUCCESS) return;
    saved.host=utf8(readString(key,L"Host")); selectedDevice=readString(key,L"Display");
    saved.autoQuality=readNumber(key,L"AutoQuality",1)!=0; saved.customBitrate=readNumber(key,L"CustomBitrate",0)!=0; saved.localCursor=readNumber(key,L"LocalCursor",0)!=0; saved.pixelExact=false;
    saved.nativePixels=readNumber(key,L"NativePixels",1)!=0; saved.clipboard=readNumber(key,L"Clipboard",1)!=0;
    // Migrate the previous sharpening default to plain resampling.
    saved.scalingQuality=1;
    auto hotkey=uint16_t(readNumber(key,L"FullscreenHotkey",td::DefaultFullscreenHotkey));
    saved.fullscreenHotkey=td::validFullscreenHotkey(hotkey)?hotkey:td::DefaultFullscreenHotkey;
    auto depth=readNumber(key,L"ColorDepth",0); saved.colorDepth=depth==8?8:depth==10?10:0;
    auto bounded=[&](const wchar_t* name,DWORD fallback,DWORD lo,DWORD hi) { return std::clamp(readNumber(key,name,fallback),lo,hi); };
    saved.settings.width=uint16_t(bounded(L"Width",2560,320,4096)); saved.settings.height=uint16_t(bounded(L"Height",1600,240,4096));
    saved.settings.fps=uint16_t(bounded(L"FPS",120,1,240));
    saved.settings.bitrate=uint64_t(bounded(L"Bitrate",120,10,20000))*1000000; saved.port=uint16_t(bounded(L"Port",47990,1,65535));
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
    EnableWindow(fields[Rate],!active && (!automatic || checked(fields[RateOverride])));
    EnableWindow(fields[RateOverride],!active && automatic);
    EnableWindow(fields[NativePixels],!active && automatic);
    std::wstring hint=tr(L"自动采用较低压缩：160–1000 Mbps，实际值连接后协商。自定义码率 10–20000 Mbps。",L"Auto uses lighter compression: 160–1000 Mbps, negotiated on connect. Custom bitrate: 10–20000 Mbps.");
    int selected=selection(fields[Screen]);
    if(automatic && selected>=0 && size_t(selected)<displays.size() && !active) {
        try {
            auto limits=displays[size_t(selected)].preferred; int codec=selection(fields[Codec]);
            auto settings=td::bestQuality(limits,limits,codec==2?1:codec==1?2:3);
            applyingPreset=true;
            SetWindowTextW(fields[Width],std::to_wstring(settings.width).c_str()); SetWindowTextW(fields[Height],std::to_wstring(settings.height).c_str());
            if(!checked(fields[RateOverride])) SetWindowTextW(fields[Rate],std::to_wstring(settings.bitrate/1000000).c_str()); applyingPreset=false;
        } catch(const std::exception& e) { hint=wide(e.what()); }
    }
    SetWindowTextW(fields[AutoHint],hint.c_str());
}
void SetupWindow::paint(HDC dc) {
    RECT area{}; GetClientRect(content,&area); RECT viewport{}; GetClientRect(window,&viewport);
    auto buffer=CreateCompatibleDC(dc); auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,area.right),std::max<LONG>(1,area.bottom)); auto previous=SelectObject(buffer,bitmap);
    FillRect(buffer,&area,background);
    auto scale=[&](int value){return MulDiv(value,int(dpi),96);};
    td::SetupLayout layout{MulDiv(area.right,96,int(dpi)),MulDiv(viewport.bottom,96,int(dpi)),checked(fields[UsePairing]),selection(fields[Mode])!=0};
    auto pen=CreatePen(PS_SOLID,1,theme.border); auto oldPen=SelectObject(buffer,pen); auto oldBrush=SelectObject(buffer,cardBackground);
    for(auto card:{std::pair<int,int>{218,176},{layout.networkTop(),layout.networkHeight()},{layout.qualityTop(),layout.qualityHeight()},
        {layout.diagnosticsTop(),158+layout.extraHeight()}}) {
        RoundRect(buffer,scale(28),scale(card.first),area.right-scale(28),scale(card.first+card.second),scale(24),scale(24));
    }
    SelectObject(buffer,oldPen); DeleteObject(pen); SelectObject(buffer,oldBrush);
    BitBlt(dc,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,previous); DeleteObject(bitmap); DeleteDC(buffer);
}
void SetupWindow::drawControl(DRAWITEMSTRUCT* item) {
    if(!item || item->CtlType!=ODT_BUTTON) return;
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
