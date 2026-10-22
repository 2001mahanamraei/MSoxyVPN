#include "tunnel.h"
#include "util.h"

#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kAdapterName[] = L"MSoxyVPN";
constexpr wchar_t kTunAddress[]  = L"198.18.0.1";

DWORD runHidden(std::wstring cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return static_cast<DWORD>(-1);
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = static_cast<DWORD>(-1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

fs::path moduleDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

bool resolveIPv4(const std::wstring& host, in_addr& out) {
    ADDRINFOW hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ADDRINFOW* res = nullptr;
    if (GetAddrInfoW(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    FreeAddrInfoW(res);
    return true;
}

std::wstring ipToString(in_addr a) {
    wchar_t buf[INET_ADDRSTRLEN] = {};
    InetNtopW(AF_INET, &a, buf, INET_ADDRSTRLEN);
    return buf;
}

DWORD findAdapter(const wchar_t* name) {
    ULONG size = 16 * 1024;
    std::vector<BYTE> buf;
    for (int i = 0; i < 3; ++i) {
        buf.resize(size);
        auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        ULONG r = GetAdaptersAddresses(AF_UNSPEC,
                                       GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                           GAA_FLAG_SKIP_DNS_SERVER,
                                       nullptr, a, &size);
        if (r == ERROR_BUFFER_OVERFLOW) continue;
        if (r != NO_ERROR) return 0;
        for (; a; a = a->Next)
            if (a->FriendlyName && _wcsicmp(a->FriendlyName, name) == 0) return a->IfIndex;
        return 0;
    }
    return 0;
}

// YAML single-quoted scalar
std::string q(const std::wstring& w) {
    std::string out = "'";
    for (char c : toUtf8(w)) {
        if (c == '\'') out += "''"; else out += c;
    }
    return out + "'";
}

} // namespace

bool Tunnel::fail(std::wstring& err, std::wstring msg) {
    err = std::move(msg);
    stop();
    return false;
}

bool Tunnel::running() const {
    return up_ && proc_ && WaitForSingleObject(proc_, 0) == WAIT_TIMEOUT;
}

bool Tunnel::start(const Settings& s, std::wstring& err) {
    if (up_ || proc_) { err = L"Already running."; return false; }

    static std::once_flag wsaOnce;
    std::call_once(wsaOnce, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });

    const fs::path exeDir = moduleDir();
    const fs::path hev    = exeDir / L"hev-socks5-tunnel.exe";
    if (!fs::exists(hev))
        return fail(err, L"hev-socks5-tunnel.exe was not found next to MSoxyVPN.exe.");
    if (!fs::exists(exeDir / L"wintun.dll"))
        return fail(err, L"wintun.dll was not found next to MSoxyVPN.exe.");

    in_addr proxyIp{};
    if (!resolveIPv4(s.proxyHost, proxyIp))
        return fail(err, L"Cannot resolve proxy host: " + s.proxyHost);
    const std::wstring proxyIpStr = ipToString(proxyIp);
    const bool loopback = (ntohl(proxyIp.s_addr) >> 24) == 127;

    // 1) Pin the route to the proxy on the current physical path (before touching routes)
    if (!loopback) {
        MIB_IPFORWARDROW row{};
        if (GetBestRoute(proxyIp.s_addr, 0, &row) != NO_ERROR)
            return fail(err, L"Cannot determine the current route to the proxy.");
        in_addr nh{};
        nh.s_addr = row.dwForwardNextHop;
        const std::wstring gw  = ipToString(nh);
        const std::wstring idx = std::to_wstring(row.dwForwardIfIndex);
        const std::wstring common =
            L" prefix=" + proxyIpStr + L"/32 interface=" + idx + L" nexthop=" + gw;
        runHidden(L"netsh interface ipv4 add route" + common + L" metric=1");
        cleanup_.push_back(L"netsh interface ipv4 delete route" + common);
    }

    // 2) hev-socks5-tunnel config
    const fs::path dir = Settings::dir();
    configPath_ = (dir / L"hev.yml").wstring();
    logPath_    = (dir / L"hev.log").wstring();
    {
        std::ofstream f(configPath_, std::ios::binary | std::ios::trunc);
        if (!f) return fail(err, L"Cannot write " + configPath_);
        f << "tunnel:\n"
          << "  name: " << toUtf8(kAdapterName) << "\n"
          << "  mtu: " << s.mtu << "\n"
          << "  ipv4: " << toUtf8(kTunAddress) << "\n"
          << "socks5:\n"
          << "  port: " << s.proxyPort << "\n"
          << "  address: " << q(proxyIpStr) << "\n";
        if (s.udp) f << "  udp: 'udp'\n";
        if (!s.proxyUser.empty()) {
            f << "  username: " << q(s.proxyUser) << "\n"
              << "  password: " << q(s.proxyPass) << "\n";
        }
        f << "misc:\n"
          << "  log-file: " << q(logPath_) << "\n"
          << "  log-level: warn\n";
    }

    // 3) Launch tun2socks inside a job object (dies with us)
    {
        std::wstring cmd = L"\"" + hev.wstring() + L"\" \"" + configPath_ + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, exeDir.c_str(), &si, &pi))
            return fail(err, L"Cannot start hev-socks5-tunnel.exe (error " +
                                 std::to_wstring(GetLastError()) + L").");
        CloseHandle(pi.hThread);
        proc_ = pi.hProcess;

        job_ = CreateJobObjectW(nullptr, nullptr);
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
            AssignProcessToJobObject(job_, proc_);
        }
    }

    // 4) Wait for the Wintun adapter to show up
    DWORD tunIdx = 0;
    for (int i = 0; i < 60 && !tunIdx; ++i) {
        if (WaitForSingleObject(proc_, 0) != WAIT_TIMEOUT)
            return fail(err, L"hev-socks5-tunnel exited immediately. See log:\n" + logPath_);
        tunIdx = findAdapter(kAdapterName);
        if (!tunIdx) Sleep(250);
    }
    if (!tunIdx) return fail(err, L"The Wintun adapter did not appear. See log:\n" + logPath_);
    const std::wstring ti = std::to_wstring(tunIdx);

    // 5) Address, DNS and routes (best effort; hev may already have set the address)
    runHidden(L"netsh interface ipv4 set address name=" + ti + L" source=static address=" +
              kTunAddress + L" mask=255.255.0.0");
    runHidden(L"netsh interface ipv4 set interface interface=" + ti + L" metric=1");
    if (!s.dns.empty())
        runHidden(L"netsh interface ipv4 set dnsservers name=" + ti +
                  L" source=static address=" + s.dns + L" validate=no");

    for (const wchar_t* prefix : {L"0.0.0.0/1", L"128.0.0.0/1"}) {
        const std::wstring common =
            std::wstring(L" prefix=") + prefix + L" interface=" + ti + L" nexthop=0.0.0.0";
        if (runHidden(L"netsh interface ipv4 add route" + common + L" metric=1") != 0)
            return fail(err, std::wstring(L"Failed to add route ") + prefix);
        cleanup_.push_back(L"netsh interface ipv4 delete route" + common);
    }

    up_ = true;
    return true;
}

void Tunnel::stop() {
    up_ = false;
    for (auto it = cleanup_.rbegin(); it != cleanup_.rend(); ++it) runHidden(*it);
    cleanup_.clear();

    if (proc_) {
        TerminateProcess(proc_, 0);
        WaitForSingleObject(proc_, 3000);
        CloseHandle(proc_);
        proc_ = nullptr;
    }
    if (job_) { CloseHandle(job_); job_ = nullptr; }

    if (!configPath_.empty()) {   // config may contain the proxy password
        std::error_code ec;
        fs::remove(configPath_, ec);
        configPath_.clear();
    }
}
