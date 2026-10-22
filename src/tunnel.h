#pragma once
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>
#include "settings.h"

// Starts hev-socks5-tunnel (tun2socks over Wintun), then routes all IPv4
// traffic into the Wintun adapter. The route to the upstream proxy itself
// is pinned to the physical interface so the tunnel does not loop.
class Tunnel {
public:
    ~Tunnel() { stop(); }
    bool start(const Settings& s, std::wstring& err);
    void stop();
    bool running() const;

private:
    bool fail(std::wstring& err, std::wstring msg);

    HANDLE proc_ = nullptr;
    HANDLE job_  = nullptr;
    std::vector<std::wstring> cleanup_;   // commands executed in reverse on stop()
    std::wstring configPath_;
    std::wstring logPath_;
    std::atomic<bool> up_{false};
};
