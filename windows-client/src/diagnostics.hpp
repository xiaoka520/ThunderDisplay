#pragma once
#include "common.hpp"
#include "client_diagnostics.hpp"
#include "../version.h"
#include <shlobj.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>

// Disk I/O stays off the input, decoder and presentation threads. Keep only
// two bounded logs; never record input contents, pairing codes or clipboard data.
class ClientDiagnostics {
    std::filesystem::path file;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> pending;
    std::thread writer;
    bool stopped=false;
    unsigned dropped=0;
    td::RemoteDiagnostics remote;
    std::atomic<bool> sharing{false};
    static constexpr uintmax_t MaxBytes=2*1024*1024;
    void run() {
        for(;;) {
            std::deque<std::string> batch;
            unsigned skipped=0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return stopped || !pending.empty();});
                if(stopped && pending.empty()) return;
                batch.swap(pending); skipped=dropped; dropped=0;
            }
            std::error_code error;
            auto size=std::filesystem::file_size(file,error);
            if(!error && size>=MaxBytes) {
                auto previous=file.parent_path()/L"client.previous.log";
                std::filesystem::remove(previous,error);
                error.clear(); std::filesystem::rename(file,previous,error);
                // If rotation fails, truncate rather than grow indefinitely.
                if(error) { std::ofstream truncate(file,std::ios::trunc); }
            }
            std::ofstream output(file,std::ios::app|std::ios::binary);
            if(!output) continue;
            if(skipped) output<<"diagnostics: dropped "<<skipped<<" queued entries\n";
            for(const auto& line:batch) output<<line<<'\n';
        }
    }
    ClientDiagnostics() {
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,local))) return;
        auto directory=std::filesystem::path(local)/L"ThunderDisplay"/L"Logs";
        std::error_code error; std::filesystem::create_directories(directory,error);
        if(error) return;
        file=directory/L"client.log";
        writer=std::thread([this]{run();});
        write("client.start","version=" TD_VERSION_TEXT " pid="+std::to_string(GetCurrentProcessId()));
    }
public:
    static ClientDiagnostics& instance() { static ClientDiagnostics instance; return instance; }
    ~ClientDiagnostics() {
        { std::lock_guard<std::mutex> lock(mutex); stopped=true; }
        wake.notify_one(); if(writer.joinable()) writer.join();
    }
    std::wstring path() const { return file.wstring(); }
    void setRemoteEnabled(bool value) {
        remote.setEnabled(value); sharing=value;
        write("client.debug",std::string("share_with_mac=")+(value?"1":"0")+" version=" TD_VERSION_TEXT);
    }
    bool remoteEnabled() const { return sharing.load(); }
    std::string takeRemote() { return remote.take(); }
    void write(const char* event,std::string details={}) {
        if(details.size()>2048) details.resize(2048);
        for(auto& c:details) if(static_cast<unsigned char>(c)<32) c=' ';
        SYSTEMTIME now{}; GetLocalTime(&now);
        std::ostringstream line;
        line<<std::setfill('0')<<std::setw(4)<<now.wYear<<'-'<<std::setw(2)<<now.wMonth<<'-'<<std::setw(2)<<now.wDay
            <<' '<<std::setw(2)<<now.wHour<<':'<<std::setw(2)<<now.wMinute<<':'<<std::setw(2)<<now.wSecond<<'.'<<std::setw(3)<<now.wMilliseconds
            <<" tick_us="<<micros()<<' '<<event<<' '<<details;
        remote.push(event,line.str());
        if(file.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(pending.size()>=256) { ++dropped; return; }
            pending.push_back(line.str());
        }
        wake.notify_one();
    }
};
inline void diagnosticLog(const char* event,std::string details={}) {
    ClientDiagnostics::instance().write(event,std::move(details));
}
