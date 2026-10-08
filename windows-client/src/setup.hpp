#pragma once
#include "network.hpp"
#include "displays.hpp"
#include "setup_scroll.hpp"
#include "setup_layout.hpp"
#include "ui_theme.hpp"
#include "window_icons.hpp"
#include "network_link.hpp"
#include <functional>
#include <future>
#include <commctrl.h>
#include <map>

// 0 = Windows UI language, 1 = Chinese, 2 = English.
extern int uiLanguage;
const wchar_t* tr(const wchar_t* chinese,const wchar_t* english);
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);

class SetupWindow {
    WindowIcons icons;
    struct Control { HWND hwnd; int x,y,w,h; HFONT appliedFont=nullptr; int regionWidth=0,regionHeight=0; bool rounded=false; };
    std::vector<Control> controls;
    std::map<int,HWND> fields;
    HFONT font=nullptr, headingFont=nullptr, sectionFont=nullptr, hintFont=nullptr;
    HBRUSH background=nullptr,cardBackground=nullptr, fieldBackground=nullptr;
    td::UITheme theme=td::UITheme::system();
    td::NetworkLink networkLink;
    std::future<td::NetworkLink> linkQuery;
    std::string connectedLocalIPv4,queryHost,queryLocal,lastSessionLink;
    uint64_t nextLinkQuery=0,selectedBitrate=0,bitrateBeforeEdit=0;
    bool bitrateMaximum=false,updatingBitrateControls=false;
    bool bitrateMaximumBeforeEdit=false;
    void pollNetworkLink();
    void applyNetworkLink(td::NetworkLink link);
    void updateBitrateControls(bool replaceInput=false);
    bool commitBitrateInput();
    void paintCombo(HWND combo,HDC dc);
    void paintSlider(HWND slider,HDC dc);
    void updateTheme();
    UINT dpi=96, fontDPI=0;
    HWND content=nullptr;
    td::SetupScroll scroll;
    int layoutWidth=-1,layoutHeight=-1;
    int logHeight=td::SetupLayout::DefaultLogHeight,layoutLogHeight=-1;
    int logResizeStartY=0,logResizeStartHeight=0;
    UINT logResizeDPI=96;
    bool resizingLog=false;
    bool layoutPaired=false,layoutManual=false;
    bool active=false, remember=false, applyingPreset=false, layingOut=false,updatingTheme=false;
    ClientOptions saved;
    std::wstring displayedStatus,displayedDetail;
    std::vector<ClientDisplay> displays;
    std::wstring selectedDevice;
    void refreshDisplays();
    void describeDisplay();
    void updateQualityControls();
    void paint(HDC dc);
    void drawControl(DRAWITEMSTRUCT* item);
    void build();
    void updatePairingControls();
    void translate();
    void layout();
    void scrollContent();
    void revealFocusedControl(bool focusChanged);
    void loadPreferences();
    void savePreferences(const ClientOptions& options);
    void setLogHeight(int height,bool persist=false);
    void saveLogHeight();
    void finishLogResize(bool cancel=false);
    HWND add(int id,const wchar_t* kind,DWORD style,int x,int y,int w,int h,const wchar_t* text=L"");
    LRESULT message(UINT message,WPARAM w,LPARAM l);
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK contentProcedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK hotkeyProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK comboProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK sliderProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK bitrateInputProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK logResizeProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
public:
    HWND window=nullptr;
    std::function<void(ClientOptions)> onConnect;
    std::function<void()> onDisconnect,onShowDisplay,onLanguage;
    std::function<void(uint16_t)> onShortcut;
    std::function<void(bool)> onDebugLogs;
    explicit SetupWindow(ClientOptions initial);
    ~SetupWindow();
    ClientOptions read();
    uint16_t fullscreenShortcut() const { return saved.fullscreenHotkey; }
    bool debugLogsEnabled() const { return saved.debugLogs; }
    void present();
    bool handleDialogMessage(MSG& message);
    void setActive(bool connected);
    void setNetworkLink(const td::NetworkLink& link);
    void status(const std::wstring& primary,const std::wstring& detail=L"");
};
