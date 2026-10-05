#pragma once
#include "common.hpp"
#include "quality.hpp"
struct ClientDisplay {
    std::wstring device,name;
    RECT bounds{};
    td::DisplayLimits current,preferred,maximum;
    unsigned bits=0;
    bool advancedColor=false,advancedEnabled=false,primary=false;
    bool colorKnown=false;
    bool hdrKnown=false,hdrSupported=false,hdrEnabled=false;
};
std::vector<ClientDisplay> detectDisplays();
