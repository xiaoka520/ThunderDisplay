#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000F
#endif
#include "displays.hpp"

std::vector<ClientDisplay> detectDisplays() {
    std::vector<ClientDisplay> result;
    // Enumerate only modes compatible with the attached monitor (no EDS_RAWMODE).
    for(DWORD i=0;;++i) {
        DISPLAY_DEVICEW device{}; device.cb=sizeof(device);
        if(!EnumDisplayDevicesW(nullptr,i,&device,0)) break;
        if(!(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) || (device.StateFlags&DISPLAY_DEVICE_MIRRORING_DRIVER)) continue;
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        if(!EnumDisplaySettingsExW(device.DeviceName,ENUM_CURRENT_SETTINGS,&mode,0)) continue;
        ClientDisplay screen; screen.device=device.DeviceName; screen.name=device.DeviceString;
        DISPLAY_DEVICEW monitor{}; monitor.cb=sizeof(monitor);
        if(EnumDisplayDevicesW(device.DeviceName,0,&monitor,0) && monitor.DeviceString[0]) screen.name=monitor.DeviceString;
        screen.current={mode.dmPelsWidth,mode.dmPelsHeight,uint16_t(std::clamp<DWORD>(mode.dmDisplayFrequency,1,1000))};
        screen.preferred=screen.maximum=screen.current;
        screen.primary=(device.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)!=0;
        screen.bounds={mode.dmPosition.x,mode.dmPosition.y,mode.dmPosition.x+LONG(mode.dmPelsWidth),mode.dmPosition.y+LONG(mode.dmPelsHeight)};
        result.push_back(std::move(screen));
    }
    // CCD distinguishes preferred panel dimensions and per-channel precision from
    // dmBitsPerPel, which describes the desktop buffer rather than panel bit depth.
    for(unsigned retry=0;retry<3;++retry) {
        UINT paths=0,modes=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)!=ERROR_SUCCESS) break;
        std::vector<DISPLAYCONFIG_PATH_INFO> path(paths); std::vector<DISPLAYCONFIG_MODE_INFO> mode(modes);
        auto hr=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,path.data(),&modes,mode.data(),nullptr);
        if(hr==ERROR_INSUFFICIENT_BUFFER) continue;
        if(hr!=ERROR_SUCCESS) break;
        for(UINT i=0;i<paths;++i) {
            auto& p=path[i]; DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),p.sourceInfo.adapterId,p.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS) continue;
            for(auto& screen:result) if(screen.device==source.viewGdiDeviceName) {
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),p.targetInfo.adapterId,p.targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS && target.monitorFriendlyDeviceName[0]) screen.name=target.monitorFriendlyDeviceName;
                DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
                preferred.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,sizeof(preferred),p.targetInfo.adapterId,p.targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&preferred.header)==ERROR_SUCCESS && preferred.width && preferred.height) {
                    screen.preferred.width=preferred.width; screen.preferred.height=preferred.height;
                    if(screen.current.width<screen.current.height && screen.preferred.width>screen.preferred.height) std::swap(screen.preferred.width,screen.preferred.height);
                    auto refresh=preferred.targetMode.targetVideoSignalInfo.vSyncFreq;
                    if(refresh.Denominator) screen.preferred.hz=uint16_t(std::clamp<unsigned>(unsigned(std::lround(double(refresh.Numerator)/refresh.Denominator)),1,1000));
                }
                if(p.targetInfo.refreshRate.Denominator) screen.current.hz=uint16_t(std::clamp<unsigned>(unsigned(std::lround(double(p.targetInfo.refreshRate.Numerator)/p.targetInfo.refreshRate.Denominator)),1,1000));
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
                color.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(color),p.targetInfo.adapterId,p.targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&color.header)==ERROR_SUCCESS) {
                    screen.colorKnown=true; screen.advancedColor=color.advancedColorSupported!=0;
                    screen.advancedEnabled=color.advancedColorEnabled!=0; screen.bits=color.bitsPerColorChannel;
                }
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 hdr{};
                hdr.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2,sizeof(hdr),p.targetInfo.adapterId,p.targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&hdr.header)==ERROR_SUCCESS) {
                    screen.hdrKnown=true; screen.hdrSupported=hdr.highDynamicRangeSupported!=0;
                    screen.hdrEnabled=hdr.highDynamicRangeUserEnabled!=0; screen.bits=hdr.bitsPerColorChannel;
                }
            }
        }
        break;
    }
    for(auto& screen:result) {
        uint16_t preferredHz=0;
        for(DWORD i=0;;++i) {
            DEVMODEW mode{}; mode.dmSize=sizeof(mode);
            if(!EnumDisplaySettingsExW(screen.device.c_str(),i,&mode,0)) break;
            if(mode.dmDisplayFlags&DM_INTERLACED) continue;
            auto hz=uint16_t(std::clamp<DWORD>(mode.dmDisplayFrequency,1,1000));
            uint64_t area=uint64_t(mode.dmPelsWidth)*mode.dmPelsHeight,currentArea=uint64_t(screen.maximum.width)*screen.maximum.height;
            if(area>currentArea || (area==currentArea && hz>screen.maximum.hz)) screen.maximum={mode.dmPelsWidth,mode.dmPelsHeight,hz};
            if(mode.dmPelsWidth==screen.preferred.width && mode.dmPelsHeight==screen.preferred.height) preferredHz=std::max(preferredHz,hz);
        }
        if(preferredHz) screen.preferred.hz=preferredHz;
    }
    std::stable_sort(result.begin(),result.end(),[](auto& a,auto& b){return a.primary && !b.primary;});
    return result;
}
