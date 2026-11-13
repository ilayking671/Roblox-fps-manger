#include "util.h"
#include <cstdio>
#include <vector>

namespace util {

std::wstring ExeDir()
{
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return L".";
        if (n < buf.size()) {
            std::wstring p(buf.data(), n);
            return ParentDir(p);
        }
        buf.resize(buf.size() * 2);
    }
}

std::wstring Widen(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), a.data(), n, nullptr, nullptr);
    return a;
}

std::string JsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                o += b;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o;
}

bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::wstring& path)
{
    if (DirExists(path)) return true;
    std::wstring parent = ParentDir(path);
    if (!parent.empty() && parent != path && !DirExists(parent)) EnsureDir(parent);
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadAll(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 6 && h == INVALID_HANDLE_VALUE; ++i) {
        h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION) return false;
            Sleep(15);
        }
    }
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (64ll << 20)) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(got);
    return true;
}

bool WriteAtomic(const std::wstring& path, const std::string& data)
{
    std::wstring tmp = path + L".fpsm.tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    BOOL ok = data.empty() ? TRUE : WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &put, nullptr);
    ok = ok && put == data.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DWORD e = GetLastError();
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION && e != ERROR_ACCESS_DENIED) break;
        Sleep(20);
    }
    DWORD e = GetLastError();
    DeleteFileW(tmp.c_str());
    SetLastError(e);
    return false;
}

std::string FormatTime(const FILETIME& ft)
{
    FILETIME lt;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ft, &lt) || !FileTimeToSystemTime(&lt, &st)) return {};
    char b[32];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
    return b;
}

std::wstring FileName(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring ParentDir(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

}
