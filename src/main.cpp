#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "settings.h"
#include "tunnel.h"

namespace {

enum : int { ID_HOST = 101, ID_PORT, ID_USER, ID_PASS, ID_DNS, ID_MTU, ID_UDP, ID_BTN, ID_STATUS };
constexpr UINT      WM_TUNNEL_STARTED = WM_APP + 1;
constexpr UINT_PTR  TIMER_WATCHDOG    = 1;
enum class State { Idle, Connecting, Connected };

HINSTANCE g_inst  = nullptr;
HWND      g_hwnd  = nullptr;
State     g_state = State::Idle;
Tunnel    g_tunnel;

HWND ctl(int id) { return GetDlgItem(g_hwnd, id); }

std::wstring getText(int id) {
    int n = GetWindowTextLengthW(ctl(id));
    std::wstring s(static_cast<size_t>(n), L'\0');
    GetWindowTextW(ctl(id), s.data(), n + 1);
    return s;
}

BOOL CALLBACK setFontProc(HWND c, LPARAM font) {
    SendMessageW(c, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

void createControls(HWND h) {
    int y = 14;
    auto row = [&](const wchar_t* label, int id, DWORD extra = 0) {
        CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE, 14, y + 3, 110, 20, h,
                        nullptr, g_inst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra, 130, y, 200,
                        24, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
        y += 32;
    };
    row(L"Proxy host", ID_HOST);
    row(L"Proxy port", ID_PORT, ES_NUMBER);
    row(L"Username", ID_USER);
    row(L"Password", ID_PASS, ES_PASSWORD);
    row(L"DNS server", ID_DNS);
    row(L"MTU", ID_MTU, ES_NUMBER);

    CreateWindowExW(0, L"BUTTON", L"Relay UDP through the proxy", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    14, y, 316, 22, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UDP)), g_inst, nullptr);
    y += 32;
    CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    14, y, 316, 34, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN)), g_inst, nullptr);
    y += 44;
    CreateWindowExW(0, L"STATIC", L"Disconnected", WS_CHILD | WS_VISIBLE | SS_CENTER, 14, y, 316, 20, h,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), g_inst, nullptr);

    EnumChildWindows(h, setFontProc, reinterpret_cast<LPARAM>(GetStockObject(DEFAULT_GUI_FONT)));
}

void loadSettingsToUi() {
    Settings s;
    s.load();
    SetWindowTextW(ctl(ID_HOST), s.proxyHost.c_str());
    SetWindowTextW(ctl(ID_PORT), std::to_wstring(s.proxyPort).c_str());
    SetWindowTextW(ctl(ID_USER), s.proxyUser.c_str());
    SetWindowTextW(ctl(ID_PASS), s.proxyPass.c_str());
    SetWindowTextW(ctl(ID_DNS),  s.dns.c_str());
    SetWindowTextW(ctl(ID_MTU),  std::to_wstring(s.mtu).c_str());
    CheckDlgButton(g_hwnd, ID_UDP, s.udp ? BST_CHECKED : BST_UNCHECKED);
}

bool readSettings(Settings& s) {
    s.proxyHost = getText(ID_HOST);
    s.proxyUser = getText(ID_USER);
    s.proxyPass = getText(ID_PASS);
    s.dns       = getText(ID_DNS);
    s.proxyPort = _wtoi(getText(ID_PORT).c_str());
    s.mtu       = _wtoi(getText(ID_MTU).c_str());
    s.udp       = IsDlgButtonChecked(g_hwnd, ID_UDP) == BST_CHECKED;

    auto bad = [](const wchar_t* m) {
        MessageBoxW(g_hwnd, m, L"MSoxyVPN", MB_OK | MB_ICONWARNING);
        return false;
    };
    if (s.proxyHost.empty())                  return bad(L"Enter the proxy host.");
    if (s.proxyPort < 1 || s.proxyPort > 65535) return bad(L"Proxy port must be 1-65535.");
    if (s.mtu < 576 || s.mtu > 9000)          return bad(L"MTU must be between 576 and 9000.");
    return true;
}

void refreshUi(const wchar_t* statusOverride = nullptr) {
    const bool idle = g_state == State::Idle;
    for (int id : {ID_HOST, ID_PORT, ID_USER, ID_PASS, ID_DNS, ID_MTU, ID_UDP})
        EnableWindow(ctl(id), idle);
    EnableWindow(ctl(ID_BTN), g_state != State::Connecting);
    SetWindowTextW(ctl(ID_BTN), g_state == State::Connected ? L"Disconnect" : L"Connect");

    const wchar_t* text = L"Disconnected";
    if (g_state == State::Connecting) text = L"Connecting...";
    if (g_state == State::Connected)  text = L"Connected";
    SetWindowTextW(ctl(ID_STATUS), statusOverride ? statusOverride : text);
}

void onButton() {
    if (g_state == State::Connected) {
        g_tunnel.stop();
        g_state = State::Idle;
        refreshUi();
        return;
    }
    Settings s;
    if (!readSettings(s)) return;
    s.save();
    g_state = State::Connecting;
    refreshUi();
    std::thread([s] {
        std::wstring err;
        bool ok = g_tunnel.start(s, err);
        PostMessageW(g_hwnd, WM_TUNNEL_STARTED, ok ? 1 : 0,
                     reinterpret_cast<LPARAM>(new std::wstring(std::move(err))));
    }).detach();
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;
        createControls(h);
        loadSettingsToUi();
        refreshUi();
        SetTimer(h, TIMER_WATCHDOG, 2000, nullptr);
        return 0;

    case WM_COMMAND:
        if (LOWORD(w) == ID_BTN && HIWORD(w) == BN_CLICKED) onButton();
        return 0;

    case WM_TUNNEL_STARTED: {
        std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(l));
        if (w) {
            g_state = State::Connected;
            refreshUi();
        } else {
            g_state = State::Idle;
            refreshUi();
            MessageBoxW(h, err->c_str(), L"MSoxyVPN - connection failed", MB_OK | MB_ICONERROR);
        }
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_WATCHDOG && g_state == State::Connected && !g_tunnel.running()) {
            g_tunnel.stop();
            g_state = State::Idle;
            refreshUi(L"Connection lost");
        }
        return 0;

    case WM_CLOSE:
        if (g_state == State::Connecting) return 0;   // wait for the worker thread
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, TIMER_WATCHDOG);
        g_tunnel.stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MSoxyVPNWindow";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, 344, 330};
    AdjustWindowRect(&rc, style, FALSE);

    HWND h = CreateWindowExW(0, wc.lpszClassName, L"MSoxyVPN", style, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    ShowWindow(h, show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
