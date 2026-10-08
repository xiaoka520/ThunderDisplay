#pragma once
#include "common.hpp"
#include "cursor.hpp"

// A small, input-transparent child surface can move without decoding or
// presenting another video frame. Windows clips it to the remote window.
class CursorOverlay {
    HWND window=nullptr;
    int hotX=0,hotY=0;
public:
    ~CursorOverlay() { if(IsWindow(window)) DestroyWindow(window); }
    void hide() { if(window) ShowWindow(window,SW_HIDE); }
    bool image(HWND parent,const td::CursorRaster& raster) {
        if(!window) window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
            L"STATIC",L"",WS_CHILD|WS_DISABLED,0,0,raster.size,raster.size,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=raster.size; info.bmiHeader.biHeight=-raster.size;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        bool ok=false;
        if(dc && bitmap && pixels) {
            std::memcpy(pixels,raster.pixels.data(),raster.pixels.size()*sizeof(uint32_t));
            auto old=SelectObject(dc,bitmap);
            POINT source{}; SIZE size{raster.size,raster.size}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            ok=UpdateLayeredWindow(window,screen,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
            SelectObject(dc,old);
        }
        if(bitmap) DeleteObject(bitmap); if(dc) DeleteDC(dc); if(screen) ReleaseDC(nullptr,screen);
        if(ok) { hotX=raster.hotX; hotY=raster.hotY; }
        return ok;
    }
    void move(int x,int y) {
        if(window) SetWindowPos(window,HWND_TOP,x-hotX,y-hotY,0,0,SWP_NOACTIVATE|SWP_NOSIZE|SWP_SHOWWINDOW);
    }
};
