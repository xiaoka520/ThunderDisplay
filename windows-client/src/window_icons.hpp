#pragma once
#include <windows.h>
#include "../resource.h"

inline int applicationIconResource(bool taskbar=false) {
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        taskbar?L"SystemUsesLightTheme":L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    return light?IDI_THUNDERDISPLAY:IDI_THUNDERDISPLAY_DARK;
}

// Each top-level window owns its scaled icons. Reload on DPI changes instead
// of stretching a shared low-resolution icon on high-density displays.
class WindowIcons {
    HICON large=nullptr, small=nullptr;
public:
    WindowIcons()=default;
    WindowIcons(const WindowIcons&)=delete;
    WindowIcons& operator=(const WindowIcons&)=delete;
    ~WindowIcons() { if(large) DestroyIcon(large); if(small) DestroyIcon(small); }
    void apply(HWND window) {
        const auto dpi=GetDpiForWindow(window);
        const auto module=GetModuleHandleW(nullptr);
        const auto resource=MAKEINTRESOURCEW(applicationIconResource());
        auto big=static_cast<HICON>(LoadImageW(module,resource,IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXICON,dpi),GetSystemMetricsForDpi(SM_CYICON,dpi),0));
        auto little=static_cast<HICON>(LoadImageW(module,resource,IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXSMICON,dpi),GetSystemMetricsForDpi(SM_CYSMICON,dpi),0));
        if(!big || !little) { if(big) DestroyIcon(big); if(little) DestroyIcon(little); return; }
        SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(big));
        SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(little));
        if(large) DestroyIcon(large); if(small) DestroyIcon(small);
        large=big; small=little;
    }
};
