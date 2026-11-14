#include "tweaks.h"
#include "util.h"
#include <dxgi1_6.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wrl.h>
#include <algorithm>
#include <cwctype>
#include <set>

namespace tweaks {

namespace {

constexpr wchar_t kLayersKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
constexpr wchar_t kGpuKey[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
constexpr wchar_t kFsoToken[] = L"DISABLEDXMAXIMIZEDWINDOWEDMODE";
constexpr wchar_t kPlayer[] = L"RobloxPlayerBeta.exe";

std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

bool ReadString(const wchar_t* key, const std::wstring& name, std::wstring& out)
{
    out.clear();
    DWORD type = 0, size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name.c_str(), RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key, name.c_str(), RRF_RT_REG_SZ, &type, buf.data(), &size) != ERROR_SUCCESS) {
        return false;
    }
    buf.resize(wcslen(buf.c_str()));
    out = buf;
    return true;
}

bool WriteString(const wchar_t* key, const std::wstring& name, const std::wstring& value)
{
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG r = RegSetValueExW(h, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool DeleteValue(const wchar_t* key, const std::wstring& name)
{
    LONG r = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, name.c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep)
{
    std::vector<std::wstring> out;
    size_t b = 0;
    while (b <= s.size()) {
        size_t e = s.find(sep, b);
        if (e == std::wstring::npos) e = s.size();
        std::wstring t = s.substr(b, e - b);
        while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
        while (!t.empty() && iswspace(t.back())) t.pop_back();
        if (!t.empty()) out.push_back(t);
        b = e + 1;
    }
    return out;
}

void AddVersionDirs(const std::wstring& root, std::vector<std::wstring>& out)
{
    if (root.empty() || !util::DirExists(root)) return;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::wstring dir = root + L"\\" + fd.cFileName;
        if (util::FileExists(dir + L"\\" + kPlayer)) out.push_back(dir);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

}

std::wstring Lower(const std::wstring& s)
{
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return r;
}

std::vector<DWORD> RobloxPids()
{
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, kPlayer) == 0) pids.push_back(pe.th32ProcessID);
    }
    CloseHandle(snap);
    return pids;
}

std::vector<std::wstring> VersionDirs()
{
    std::vector<std::wstring> dirs;
    AddVersionDirs(KnownFolder(FOLDERID_LocalAppData) + L"\\Roblox\\Versions", dirs);
    AddVersionDirs(KnownFolder(FOLDERID_ProgramFilesX86) + L"\\Roblox\\Versions", dirs);
    AddVersionDirs(KnownFolder(FOLDERID_ProgramFilesX64) + L"\\Roblox\\Versions", dirs);
    return dirs;
}

std::vector<std::wstring> PlayerExes(const std::vector<DWORD>& pids)
{
    std::vector<std::wstring> exes;
    std::set<std::wstring> seen;
    auto add = [&](const std::wstring& p) {
        if (p.empty()) return;
        if (seen.insert(Lower(p)).second) exes.push_back(p);
    };
    for (const auto& d : VersionDirs()) add(d + L"\\" + kPlayer);
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        wchar_t buf[MAX_PATH * 2];
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) add(std::wstring(buf, n));
        CloseHandle(h);
    }
    return exes;
}

int ApplyPriority(const std::vector<DWORD>& pids, int level)
{
    DWORD cls = level == 2 ? HIGH_PRIORITY_CLASS : level == 1 ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    int changed = 0;
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        if (GetPriorityClass(h) != cls && SetPriorityClass(h, cls)) ++changed;
        CloseHandle(h);
    }
    return changed;
}

bool FsoDisabled(const std::wstring& exe)
{
    std::wstring v;
    if (!ReadString(kLayersKey, exe, v)) return false;
    for (const auto& t : Split(v, L' ')) {
        if (_wcsicmp(t.c_str(), kFsoToken) == 0) return true;
    }
    return false;
}

bool SetFsoDisabled(const std::wstring& exe, bool disabled)
{
    std::wstring v;
    ReadString(kLayersKey, exe, v);
    std::vector<std::wstring> keep;
    for (const auto& t : Split(v, L' ')) {
        if (t == L"~" || _wcsicmp(t.c_str(), kFsoToken) == 0) continue;
        keep.push_back(t);
    }
    if (disabled) keep.push_back(kFsoToken);
    if (keep.empty()) return DeleteValue(kLayersKey, exe);
    std::wstring out = L"~";
    for (const auto& t : keep) out += L" " + t;
    return WriteString(kLayersKey, exe, out);
}

bool HighPerfGpu(const std::wstring& exe)
{
    std::wstring v;
    if (!ReadString(kGpuKey, exe, v)) return false;
    for (const auto& kv : Split(v, L';')) {
        if (_wcsicmp(kv.c_str(), L"GpuPreference=2") == 0) return true;
    }
    return false;
}

bool SetHighPerfGpu(const std::wstring& exe, bool on)
{
    std::wstring v;
    ReadString(kGpuKey, exe, v);
    std::wstring out;
    for (const auto& kv : Split(v, L';')) {
        if (_wcsnicmp(kv.c_str(), L"GpuPreference=", 14) == 0) continue;
        out += kv + L";";
    }
    if (on) out += L"GpuPreference=2;";
    if (out.empty()) return DeleteValue(kGpuKey, exe);
    return WriteString(kGpuKey, exe, out);
}

GpuInfo QueryGpus()
{
    GpuInfo info;
    Microsoft::WRL::ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return info;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f1->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.VendorId != 0x1414) ++info.count;
        a.Reset();
    }
    Microsoft::WRL::ComPtr<IDXGIFactory6> f6;
    if (SUCCEEDED(f1.As(&f6))) {
        auto name = [&](DXGI_GPU_PREFERENCE pref) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> ad;
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(f6->EnumAdapterByGpuPreference(0, pref, IID_PPV_ARGS(&ad))) && SUCCEEDED(ad->GetDesc1(&d))) {
                return std::wstring(d.Description);
            }
            return std::wstring();
        };
        info.highPerf = name(DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);
        info.powerSaving = name(DXGI_GPU_PREFERENCE_MINIMUM_POWER);
    }
    return info;
}

bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &n);
    CloseHandle(token);
    return ok && e.TokenIsElevated;
}

}
