#include "net.h"
#include <winhttp.h>
#include <vector>

namespace net {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    ~Handle()
    {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

std::wstring Fail(const wchar_t* what)
{
    return std::wstring(what) + L" (error " + std::to_wstring(GetLastError()) + L")";
}

}

bool Download(const std::wstring& url, const std::wstring& dest, const Progress& progress, std::wstring& error)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = Fail(L"Invalid URL");
        return false;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring object(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    Handle session(WinHttpOpen(L"RobloxFpsManager/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session.h = WinHttpOpen(L"RobloxFpsManager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        error = Fail(L"Network unavailable");
        return false;
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    DWORD both = protocols | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &both, sizeof(both)))
#endif
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    WinHttpSetTimeouts(session.h, 10000, 15000, 30000, 60000);

    Handle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!connect) {
        error = Fail(L"Could not connect");
        return false;
    }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        error = Fail(L"Could not create request");
        return false;
    }
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = Fail(L"Request failed");
        return false;
    }

    DWORD status = 0;
    DWORD len = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = L"Server responded with HTTP " + std::to_wstring(status);
        return false;
    }

    uint64_t total = 0;
    wchar_t lenBuf[32]{};
    len = sizeof(lenBuf);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &len,
                            WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(lenBuf, nullptr, 10);
    }

    std::wstring part = dest + L".part";
    HANDLE file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = Fail(L"Could not create file");
        return false;
    }

    std::vector<char> buf(1 << 18);
    uint64_t done = 0;
    bool ok = true;
    if (progress) progress(0, total);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            error = Fail(L"Download interrupted");
            ok = false;
            break;
        }
        if (got == 0) break;
        DWORD put = 0;
        if (!WriteFile(file, buf.data(), got, &put, nullptr) || put != got) {
            error = Fail(L"Disk write failed");
            ok = false;
            break;
        }
        done += got;
        if (progress) progress(done, total);
    }
    CloseHandle(file);
    if (ok && total && done != total) {
        error = L"Download incomplete";
        ok = false;
    }
    if (ok && !MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = Fail(L"Could not finalize file");
        ok = false;
    }
    if (!ok) DeleteFileW(part.c_str());
    return ok;
}

}
