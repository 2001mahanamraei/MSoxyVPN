#include "settings.h"
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

std::wstring Settings::dir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, static_cast<DWORD>(std::size(buf)));
    fs::path p = (n > 0 && n < std::size(buf)) ? fs::path(buf) : fs::temp_directory_path();
    p /= L"MSoxyVPN";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.wstring();
}

static std::wstring iniPath() { return (fs::path(Settings::dir()) / L"settings.ini").wstring(); }

static std::wstring readStr(const wchar_t* key, const wchar_t* def, const std::wstring& file) {
    wchar_t buf[512];
    GetPrivateProfileStringW(L"proxy", key, def, buf, static_cast<DWORD>(std::size(buf)), file.c_str());
    return buf;
}

void Settings::load() {
    const auto f = iniPath();
    proxyHost = readStr(L"host", proxyHost.c_str(), f);
    proxyPort = static_cast<int>(GetPrivateProfileIntW(L"proxy", L"port", proxyPort, f.c_str()));
    proxyUser = readStr(L"user", L"", f);
    proxyPass = readStr(L"pass", L"", f);
    dns       = readStr(L"dns", dns.c_str(), f);
    mtu       = static_cast<int>(GetPrivateProfileIntW(L"proxy", L"mtu", mtu, f.c_str()));
    udp       = GetPrivateProfileIntW(L"proxy", L"udp", udp ? 1 : 0, f.c_str()) != 0;
}

void Settings::save() const {
    const auto f = iniPath();
    WritePrivateProfileStringW(L"proxy", L"host", proxyHost.c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"port", std::to_wstring(proxyPort).c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"user", proxyUser.c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"pass", proxyPass.c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"dns",  dns.c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"mtu",  std::to_wstring(mtu).c_str(), f.c_str());
    WritePrivateProfileStringW(L"proxy", L"udp",  udp ? L"1" : L"0", f.c_str());
}
