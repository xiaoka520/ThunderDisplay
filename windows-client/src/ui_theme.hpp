#pragma once
#include "common.hpp"
#include <dwmapi.h>
#include <uxtheme.h>
namespace td {
struct UITheme {
    bool dark=false,highContrast=false;
    COLORREF background,card,field,text,muted,border,button,pressed,accent;
    static DWORD windowsBuild() {
        OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
        using Version=LONG(WINAPI*)(OSVERSIONINFOW*);
        auto function=reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        return function && function(&version)==0?version.dwBuildNumber:0;
    }
    static HMODULE themeLibrary() { static auto library=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);return library; }
    static void enableNativeAppTheme() {
        // Optional native theming exports; only use their known Windows build
        // signatures. Older systems retain the documented DWM/color fallback.
        if(windowsBuild()<18362 || !themeLibrary()) return;
        using Preferred=int(WINAPI*)(int);
        auto preferred=reinterpret_cast<Preferred>(GetProcAddress(themeLibrary(),MAKEINTRESOURCEA(135)));
        if(preferred) preferred(1); // AllowDark: follow the user's app color mode.
    }
    static UITheme system() {
        HIGHCONTRASTW contrast{sizeof(contrast),0,nullptr};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        if(contrast.dwFlags&HCF_HIGHCONTRASTON) return {false,true,GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_GRAYTEXT),GetSysColor(COLOR_WINDOWFRAME),GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_HIGHLIGHT)};
        DWORD light=1,size=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
        if(!light) return {true,false,RGB(24,24,24),RGB(32,32,32),RGB(43,43,43),RGB(242,242,242),RGB(166,166,166),RGB(65,65,65),RGB(43,43,43),RGB(58,58,58),RGB(111,105,245)};
        return {false,false,RGB(243,246,251),RGB(255,255,255),RGB(247,249,253),RGB(24,36,58),RGB(126,138,156),RGB(224,231,242),RGB(255,255,255),RGB(229,233,249),RGB(79,71,230)};
    }
    void apply(HWND window) const {
        if(!window) return;
        if(windowsBuild()>=17763 && themeLibrary()) {
            using Allow=BOOL(WINAPI*)(HWND,BOOL);
            auto allow=reinterpret_cast<Allow>(GetProcAddress(themeLibrary(),MAKEINTRESOURCEA(133)));
            if(allow) allow(window,dark && !highContrast);
        }
        BOOL enabled=dark;
        if(FAILED(DwmSetWindowAttribute(window,20,&enabled,sizeof(enabled)))) DwmSetWindowAttribute(window,19,&enabled,sizeof(enabled));
        SetWindowTheme(window,highContrast?L"":dark?L"DarkMode_Explorer":L"Explorer",nullptr);
    }
};
}
