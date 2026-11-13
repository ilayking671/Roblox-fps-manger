#include "settings.h"
#include "util.h"
#include <cctype>
#include <charconv>
#include <shlobj.h>

namespace {

constexpr char kKey[] = "name=\"FramerateCap\"";

bool FindCap(const std::string& s, size_t& begin, size_t& end)
{
    size_t from = 0;
    for (;;) {
        size_t k = s.find(kKey, from);
        if (k == std::string::npos) return false;
        from = k + 1;
        size_t open = s.rfind('<', k);
        if (open == std::string::npos || s.compare(open, 5, "<int ") != 0) continue;
        size_t gt = s.find('>', k);
        if (gt == std::string::npos) return false;
        size_t lt = s.find('<', gt);
        if (lt == std::string::npos) return false;
        begin = gt + 1;
        end = lt;
        return true;
    }
}

bool ParseInt(const std::string& s, size_t b, size_t e, int& out)
{
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b == e) return false;
    auto r = std::from_chars(s.data() + b, s.data() + e, out);
    return r.ec == std::errc() && r.ptr == s.data() + e;
}

std::string LastError(const char* what)
{
    DWORD e = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   e, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::string text = msg ? util::Narrow(msg) : std::string();
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    return std::string(what) + (text.empty() ? "" : ": " + text);
}

}

void RobloxSettings::Resolve()
{
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dir_ = std::wstring(local) + L"\\Roblox";
    }
    if (local) CoTaskMemFree(local);
    path_ = dir_ + L"\\GlobalBasicSettings_13.xml";
    if (util::FileExists(path_)) return;

    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir_ + L"\\GlobalBasicSettings_*.xml").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    int best = -1;
    do {
        std::wstring n = fd.cFileName;
        size_t us = n.rfind(L'_');
        size_t dot = n.rfind(L'.');
        if (us == std::wstring::npos || dot == std::wstring::npos || dot <= us + 1) continue;
        int v = _wtoi(n.substr(us + 1, dot - us - 1).c_str());
        if (v > best) {
            best = v;
            path_ = dir_ + L"\\" + n;
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

SettingsState RobloxSettings::Query() const
{
    SettingsState s;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (path_.empty() || !GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &a)) return s;
    if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return s;
    s.found = true;
    s.readOnly = (a.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    s.size = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    s.modified = util::FormatTime(a.ftLastWriteTime);
    std::string data;
    if (!util::ReadAll(path_, data)) return s;
    size_t b, e;
    if (FindCap(data, b, e)) s.hasKey = ParseInt(data, b, e, s.fps);
    return s;
}

bool RobloxSettings::WriteContent(const std::string& data, std::string& error)
{
    DWORD attrs = GetFileAttributesW(path_.c_str());
    bool ro = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY);
    if (ro) SetFileAttributesW(path_.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    bool ok = util::WriteAtomic(path_, data);
    if (!ok) error = LastError("Write failed");
    if (ro) {
        DWORD now = GetFileAttributesW(path_.c_str());
        if (now != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(path_.c_str(), now | FILE_ATTRIBUTE_READONLY);
    }
    return ok;
}

bool RobloxSettings::SetFps(int fps, std::string& error)
{
    std::string data;
    if (!util::ReadAll(path_, data)) {
        error = LastError("Read failed");
        return false;
    }
    std::string value = std::to_string(fps);
    size_t b, e;
    if (FindCap(data, b, e)) {
        int current = 0;
        if (ParseInt(data, b, e, current) && current == fps) return true;
        data.replace(b, e - b, value);
    } else {
        size_t props = data.find("<Properties>");
        if (props == std::string::npos) {
            error = "Settings file has no <Properties> block";
            return false;
        }
        size_t after = props + 12;
        std::string eol = data.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::string indent = "\t\t\t";
        size_t line = data.find('\n', after);
        if (line != std::string::npos) {
            size_t i = line + 1;
            size_t j = i;
            while (j < data.size() && (data[j] == '\t' || data[j] == ' ')) ++j;
            if (j > i) indent = data.substr(i, j - i);
        }
        data.insert(after, eol + indent + "<int name=\"FramerateCap\">" + value + "</int>");
    }
    return WriteContent(data, error);
}

bool RobloxSettings::SetReadOnly(bool on, std::string& error)
{
    DWORD attrs = GetFileAttributesW(path_.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        error = LastError("File unavailable");
        return false;
    }
    DWORD next = on ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (next == attrs) return true;
    if (!SetFileAttributesW(path_.c_str(), next)) {
        error = LastError("Could not change attributes");
        return false;
    }
    return true;
}

bool RobloxSettings::SaveBackup(const std::wstring& backupPath, std::string& error) const
{
    std::string data;
    if (!util::ReadAll(path_, data)) {
        error = LastError("Read failed");
        return false;
    }
    util::EnsureDir(util::ParentDir(backupPath));
    if (!util::WriteAtomic(backupPath, data)) {
        error = LastError("Backup write failed");
        return false;
    }
    return true;
}

bool RobloxSettings::RestoreBackup(const std::wstring& backupPath, std::string& error)
{
    std::string data;
    if (!util::ReadAll(backupPath, data)) {
        error = "No backup available";
        return false;
    }
    if (data.find("<roblox") == std::string::npos) {
        error = "Backup file is not a Roblox settings file";
        return false;
    }
    return WriteContent(data, error);
}
