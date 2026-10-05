#pragma once
#include "decoder.hpp"
#include "clipboard.hpp"
#include "blob.hpp"
#include "recovery.hpp"

class ClientSession {
    ClientOptions options;
    Renderer& renderer;
    HWND window;
    uint8_t requestedCodecMask;
    std::atomic<bool> stopFlag{false}, online{false}, wantIDR{false}, overflow{false};
    std::thread worker;
    std::mutex mutex;
    std::deque<td::Bytes> outgoing;
    std::deque<td::Bytes> clipboardOutgoing;
    std::optional<std::string> clipboardIncoming;
    std::optional<td::Bytes> imageIncoming,cursorIncoming;
    std::atomic<bool> richClipboard{false};
    uint32_t clipboardID=0;
    std::atomic<bool> clipboardOnline{false};
    std::atomic<bool> localCursorActive{false};
    std::string status;
    std::string fallbackDescription;
    td::RetryBudget retryBudget;
    std::atomic<unsigned> attemptNumber{0};
    std::atomic<bool> recoveryStopped{false}, videoInterrupted{false};
    void checkRecoveryDeadline();
    void run();
    void connectAndStream(const std::string& host);
    std::string discover();
    void setStatus(std::string value);
public:
    ClientSession(ClientOptions options,Renderer& renderer,HWND hwnd): options(std::move(options)),renderer(renderer),window(hwnd),requestedCodecMask(this->options.settings.codecMask) {}
    ~ClientSession() { stop(); }
    void start() { worker=std::thread([this]{run();}); }
    void stop() { stopFlag=true; if(worker.joinable()) worker.join(); }
    bool connected() const { return online; }
    bool retryStopped() const { return recoveryStopped; }
    bool interrupted() const { return videoInterrupted; }
    unsigned attempts() const { return attemptNumber; }
    static constexpr unsigned totalAttempts() { return td::RetryBudget::Total; }
    void send(td::Bytes data);
    void sendClipboard(const std::string& text);
    void sendClipboardImage(const td::Bytes& png);
    bool imageClipboardEnabled() const { return clipboardOnline && richClipboard; }
    std::optional<td::Bytes> takeImage() { std::lock_guard<std::mutex> lock(mutex);auto image=std::move(imageIncoming);imageIncoming.reset();return image; }
    std::optional<td::Bytes> takeCursor() { std::lock_guard<std::mutex> lock(mutex);auto image=std::move(cursorIncoming);cursorIncoming.reset();return image; }
    std::optional<std::string> takeClipboard() { std::lock_guard<std::mutex> lock(mutex); auto text=std::move(clipboardIncoming); clipboardIncoming.reset(); return text; }
    bool clipboardEnabled() const { return clipboardOnline; }
    bool localCursorEnabled() const { return localCursorActive; }
    void requestIDR() { wantIDR=true; }
    std::string currentStatus() { std::lock_guard<std::mutex> lock(mutex); return status; }
};
