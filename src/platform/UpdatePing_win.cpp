#include "platform/UpdatePingInternal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#include <cstring>
#include <string>
#include <vector>

namespace galaxy {
namespace {

struct RtlOsVersion {
    unsigned long size;
    unsigned long major;
    unsigned long minor;
    unsigned long build;
    unsigned long platform;
    wchar_t servicePack[128];
};

using RtlGetVersionFn = long(__stdcall*)(RtlOsVersion*);

}  // namespace

void postUpdateCheck(std::string_view body) {
    std::string payload(body);
    HINTERNET session = WinHttpOpen(L"Galaxy Editor", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        return;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);
    HINTERNET connection = WinHttpConnect(session, L"starnexa.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        return;
    }
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        L"/galaxy_editor_services/check_for_update/",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return;
    }
    const wchar_t* headers = L"Content-Type: application/json";
    const BOOL sent = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1),
        payload.data(),
        static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()),
        0);
    if (sent && WinHttpReceiveResponse(request, nullptr)) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> discarded(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, discarded.data(), available, &read) || read == 0) {
                break;
            }
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
}

std::string hostPlatformName() {
    return "Windows";
}

std::string hostOperatingSystem() {
    FARPROC proc = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    RtlGetVersionFn rtlGetVersion = nullptr;
    std::memcpy(&rtlGetVersion, &proc, sizeof(rtlGetVersion));
    RtlOsVersion info{};
    info.size = sizeof(info);
    if (rtlGetVersion == nullptr || rtlGetVersion(&info) != 0) {
        return "Windows";
    }
    return "Windows " + std::to_string(info.major) + "." + std::to_string(info.minor) + "." + std::to_string(info.build);
}

}  // namespace galaxy
