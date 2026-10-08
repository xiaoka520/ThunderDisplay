#pragma once
#include "common.hpp"
#include "bitrate.hpp"
#include <iphlpapi.h>
#include <cwctype>

namespace td {
struct NetworkLink {
    ULONG index=0;
    std::wstring name,description;
    std::string localIPv4;
    uint64_t receiveRate=0,transmitRate=0;
    uint64_t bitrateLimit() const { return linkBitrateLimit(receiveRate); }
};
inline NetworkLink detectNetworkLink(const std::string& host={},const std::string& localIPv4={}) {
    ULONG size=16384;
    std::vector<uint8_t> storage(size);
    ULONG result=ERROR_BUFFER_OVERFLOW;
    for(unsigned attempt=0;attempt<3 && result==ERROR_BUFFER_OVERFLOW;++attempt) {
        storage.resize(size);
        result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()),&size);
    }
    if(result!=NO_ERROR) return {};
    ULONG routeIndex=0;
    if(localIPv4.empty() && !host.empty()) {
        SOCKADDR_INET destination{},source{}; MIB_IPFORWARD_ROW2 route{};
        destination.Ipv4.sin_family=AF_INET;
        if(InetPtonA(AF_INET,host.c_str(),&destination.Ipv4.sin_addr)!=1 ||
           GetBestRoute2(nullptr,0,nullptr,&destination,0,&route,&source)!=NO_ERROR) return {};
        routeIndex=route.InterfaceIndex;
    }
    std::vector<NetworkLink> candidates;
    for(auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());adapter;adapter=adapter->Next) {
        if(adapter->OperStatus!=IfOperStatusUp || adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if(routeIndex && adapter->IfIndex!=routeIndex) continue;
        NetworkLink link; link.index=adapter->IfIndex;
        if(adapter->FriendlyName) link.name=adapter->FriendlyName;
        if(adapter->Description) link.description=adapter->Description;
        bool matchesAddress=localIPv4.empty();
        for(auto* address=adapter->FirstUnicastAddress;address;address=address->Next) {
            if(!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family!=AF_INET) continue;
            char text[INET_ADDRSTRLEN]{};
            auto* ip=&reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr)->sin_addr;
            if(!InetNtopA(AF_INET,ip,text,INET_ADDRSTRLEN)) continue;
            if(link.localIPv4.empty()) link.localIPv4=text;
            if(localIPv4==text) { matchesAddress=true; link.localIPv4=text; break; }
        }
        if(!matchesAddress || link.localIPv4.empty()) continue;
        if(!routeIndex && localIPv4.empty()) {
            auto name=link.name+L" "+link.description;
            std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
            if(name.find(L"thunderbolt")==std::wstring::npos && name.find(L"usb4")==std::wstring::npos && name.find(L"雷雳")==std::wstring::npos && name.find(L"雷电")==std::wstring::npos) continue;
        }
        MIB_IF_ROW2 row{}; row.InterfaceLuid=adapter->Luid;
        if(GetIfEntry2(&row)==NO_ERROR) {
            if(row.OperStatus!=IfOperStatusUp || row.MediaConnectState==MediaConnectStateDisconnected) continue;
            link.receiveRate=row.ReceiveLinkSpeed; link.transmitRate=row.TransmitLinkSpeed;
        } else { link.receiveRate=adapter->ReceiveLinkSpeed; link.transmitRate=adapter->TransmitLinkSpeed; }
        candidates.push_back(std::move(link));
    }
    // Never guess the link from the first Ethernet adapter or the fastest NIC.
    return candidates.size()==1?candidates.front():NetworkLink{};
}
}
