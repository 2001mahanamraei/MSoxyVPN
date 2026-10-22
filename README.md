# MSoxyVPN

A small Windows (C++20 / Win32) client that sends **all IPv4 traffic** of the machine through a
**SOCKS5 proxy that you provide**, using [Wintun](https://www.wintun.net/) and
[hev-socks5-tunnel](https://github.com/heiher/hev-socks5-tunnel) (tun2socks).

## Download the exe
1. Push this folder to a GitHub repository (branch `main`).
2. Open **Actions → Build MSoxyVPN → Run workflow** (it also runs on every push).
3. Download the `MSoxyVPN-win64` artifact. For a public release push a tag: `git tag v1.0.0 && git push --tags`.

The zip contains `MSoxyVPN.exe`, `hev-socks5-tunnel.exe` and `wintun.dll`. Keep them in the same folder.
The exe asks for Administrator rights (needed to create the adapter and routes).

## Build locally
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Then copy `hev-socks5-tunnel.exe` and `wintun.dll` next to `build\Release\MSoxyVPN.exe`.

## How it works
1. Resolves the proxy and pins a /32 route to it on the physical interface (no routing loop).
2. Writes a config and starts `hev-socks5-tunnel.exe` (creates the Wintun adapter `MSoxyVPN`).
3. Sets DNS and adds `0.0.0.0/1` + `128.0.0.0/1` routes into the adapter.
4. On disconnect/exit the routes are removed and the process is killed.

## Known limitations
- IPv4 only: IPv6 traffic is **not** tunneled (possible leak). No kill switch.
- Settings (including the proxy password) are stored in plain text in `%APPDATA%\MSoxyVPN\settings.ini`.
- DNS goes through the tunnel only if the proxy supports UDP associate (keep "Relay UDP" enabled).
- Not included: login to any VPN provider. You must supply your own SOCKS5 proxy.
- Untested on real hardware by the author; the hev config keys follow hev-socks5-tunnel's documented format -
  check `%APPDATA%\MSoxyVPN\hev.log` if it fails.
