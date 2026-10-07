#pragma once
#include "common.hpp"
#include "diagnostics.hpp"
#include "window_icons.hpp"
#include <shellapi.h>

constexpr UINT TrayMessage=WM_APP+20;
inline UINT showSettingsMessage() { static const UINT message=RegisterWindowMessageW(L"ThunderDisplay.ShowSettings.v1"); return message; }
inline UINT exitForInstallerMessage() { static const UINT message=RegisterWindowMessageW(L"ThunderDisplay.ExitForInstaller.v1"); return message; }

// Load Icon/Icon.png at native taskbar size, with a contrasting template color.
// Shell and taskbar restarts retain the same notification identity.
class TrayIcon {
    HWND window=nullptr;
    HICON icon=nullptr;
    NOTIFYICONDATAW data{};
    bool registered=false;
    uint64_t retryAfter=0;
    HICON makeIcon() {
        auto taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
        const auto dpi=GetDpiForWindow(taskbar?taskbar:window);
        const auto resource=applicationIconResource(true)==IDI_THUNDERDISPLAY?IDI_THUNDERDISPLAY_STATUS_LIGHT:IDI_THUNDERDISPLAY_STATUS;
        return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(resource),
            IMAGE_ICON,GetSystemMetricsForDpi(SM_CXSMICON,dpi),GetSystemMetricsForDpi(SM_CYSMICON,dpi),0));
    }

public:
    void attach(HWND owner) {
        window=owner; data.cbSize=sizeof(data); data.hWnd=owner; data.uID=1;
        data.guidItem={0xb53b9ab0,0x1609,0x4407,{0x89,0xe3,0x5a,0x73,0x02,0x53,0xba,0x61}};
        data.uCallbackMessage=TrayMessage; refresh();
    }
    void refresh(bool taskbarRestart=false) {
        if(!window) return;
        if(taskbarRestart) registered=false;
        retryAfter=micros()+2000000;
        auto replacement=makeIcon(); if(!replacement) { diagnosticLog("tray.icon.failed"); return; }
        auto previous=icon; icon=replacement; data.hIcon=icon;
        data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_GUID|NIF_SHOWTIP;
        lstrcpynW(data.szTip,L"ThunderDisplay",int(std::size(data.szTip)));
        registered=Shell_NotifyIconW(registered?NIM_MODIFY:NIM_ADD,&data)!=FALSE;
        if(registered) { data.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); }
        if(previous) DestroyIcon(previous);
    }
    void status(const std::wstring& state) {
        if(!window) return;
        lstrcpynW(data.szTip,(L"ThunderDisplay · "+state).c_str(),int(std::size(data.szTip)));
        data.uFlags=NIF_TIP|NIF_GUID|NIF_SHOWTIP;
        if(registered) Shell_NotifyIconW(NIM_MODIFY,&data);
    }
    void retry() { if(window && !registered && micros()>=retryAfter) refresh(); }
    void remove() {
        if(registered) Shell_NotifyIconW(NIM_DELETE,&data);
        registered=false; window=nullptr;
        if(icon) DestroyIcon(icon); icon=nullptr;
    }
    ~TrayIcon() { remove(); }
};
