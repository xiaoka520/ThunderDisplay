#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "protocol.hpp"
#include "quality.hpp"
#include "shortcuts.hpp"

using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr, const char* where) {
    if(FAILED(hr)) { std::ostringstream s; s<<where<<" (HRESULT 0x"<<std::hex<<uint32_t(hr)<<")"; throw std::runtime_error(s.str()); }
}
inline uint64_t micros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr UINT StatusMessage=WM_APP+1, DisconnectedMessage=WM_APP+2, FramePresentedMessage=WM_APP+3, KeyboardMessage=WM_APP+4, ReleaseInputMessage=WM_APP+5, ClipboardMessage=WM_APP+6,CursorMessage=WM_APP+7;
struct ClientOptions {
    std::string host, token;
    uint16_t port=td::Port;
    td::Settings settings;
    bool fullscreen=false, vsync=false, explicitSettings=false, pairing=false, autoConnect=false;
    bool autoQuality=true, customBitrate=false, pixelExact=false, nativePixels=true, clipboard=true;
    bool autoFrameRate=true;
    bool localCursor=false;
    uint8_t scalingQuality=1; // 0 compatibility, 1 Lanczos (no sharpening).
    uint16_t fullscreenHotkey=td::DefaultFullscreenHotkey;
    unsigned displayBits=0;
    uint8_t colorDepth=0;
    uint8_t capabilityVersion=7;
    td::DisplayLimits display;
    RECT displayBounds{};
};
