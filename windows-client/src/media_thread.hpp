#pragma once
#include "diagnostics.hpp"
#include <avrt.h>

// Register only dedicated video threads. MMCSS supplies bounded multimedia
// scheduling without changing global timer resolution or process priority.
class MediaThreadPriority {
    HANDLE task=nullptr;
public:
    explicit MediaThreadPriority(const char* role) {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        DWORD index=0;
        task=AvSetMmThreadCharacteristicsW(L"Playback",&index);
        if(task && !AvSetMmThreadPriority(task,AVRT_PRIORITY_HIGH)) {
            AvRevertMmThreadCharacteristics(task); task=nullptr;
        }
        diagnosticLog("video.scheduling",std::string(role)+(task?" MMCSS Playback/high":" above-normal fallback"));
    }
    ~MediaThreadPriority() { if(task) AvRevertMmThreadCharacteristics(task); }
    MediaThreadPriority(const MediaThreadPriority&)=delete;
    MediaThreadPriority& operator=(const MediaThreadPriority&)=delete;
};
