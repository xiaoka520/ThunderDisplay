#pragma once
#include "network.hpp"
#include "displays.hpp"
#include "setup_scroll.hpp"
#include "ui_theme.hpp"
#include <functional>
#include <map>

// 0 = Windows UI language, 1 = Chinese, 2 = English.
extern int uiLanguage;
const wchar_t* tr(const wchar_t* chinese,const wchar_t* english);
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);

class SetupWindow {
    struct Control { HWND hwnd; int x,y,w,h; HFONT appliedFont=nullptr; int regionWidth=0,regionHeight=0; bool rounded=false; };
    std::vector<Control> controls;
    std::map<int,HWND> fields;
    HFONT font=nullptr, headingFont=nullptr, sectionFont=nullptr, hintFont=nullptr;
    HBRUSH background=nullptr,cardBackground=nullptr, fieldBackground=nullptr;
    td::UITheme theme=td::UITheme::system();
    void updateTheme();
    UINT dpi=96, fontDPI=0;
    HWND content=nullptr;
    td::SetupScroll scroll;
    int layoutWidth=-1,layoutHeight=-1;
    bool layoutPaired=false,layoutManual=false;
    bool active=false, remember=false, applyingPreset=false, layingOut=false,updatingTheme=false;
    ClientOptions saved;
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
    HWND add(int id,const wchar_t* kind,DWORD style,int x,int y,int w,int h,const wchar_t* text=L"");
    LRESULT message(UINT message,WPARAM w,LPARAM l);
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK contentProcedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK hotkeyProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
public:
    HWND window=nullptr;
    std::function<void(ClientOptions)> onConnect;
    std::function<void()> onDisconnect,onShowDisplay,onLanguage;
    std::function<void(uint16_t)> onShortcut;
    explicit SetupWindow(ClientOptions initial);
    ~SetupWindow();
    ClientOptions read();
    uint16_t fullscreenShortcut() const { return saved.fullscreenHotkey; }
    void present();
    bool handleDialogMessage(MSG& message);
    void setActive(bool connected);
    void status(const std::wstring& primary,const std::wstring& detail=L"");
};
