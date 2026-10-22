#pragma once
#include <string>

struct Settings {
    std::wstring proxyHost = L"127.0.0.1";
    int          proxyPort = 1080;
    std::wstring proxyUser;
    std::wstring proxyPass;
    std::wstring dns       = L"1.1.1.1";
    int          mtu       = 1400;
    bool         udp       = true;

    // %APPDATA%\MSoxyVPN (created if missing)
    static std::wstring dir();
    void load();
    void save() const;
};
