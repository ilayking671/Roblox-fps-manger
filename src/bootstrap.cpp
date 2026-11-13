#include "bootstrap.h"
#include "net.h"
#include "util.h"
#include "resource.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include "WebView2.h"

namespace boot {

namespace {

constexpr wchar_t kLoaderUrl[] = L"https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4314.35";
constexpr wchar_t kRuntimeUrl[] = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";
#if defined(_M_ARM64)
constexpr wchar_t kEntry[] = L"runtimes/win-arm64/native/WebView2Loader.dll";
#elif defined(_M_X64)
constexpr wchar_t kEntry[] = L"runtimes/win-x64/native/WebView2Loader.dll";
#else
constexpr wchar_t kEntry[] = L"runtimes/win-x86/native/WebView2Loader.dll";
#endif
constexpr wchar_t kClass[] = L"RobloxFpsManager.Boot";
constexpr UINT WM_BOOT_UPDATE = WM_APP + 40;
constexpr UINT WM_BOOT_DONE = WM_APP + 41;
constexpr UINT_PTR kAnimTimer = 7;

struct Context {
    HWND hwnd = nullptr;
    std::wstring dir;
    std::wstring deps;
    std::wstring loaderPath;
    HMODULE loader = nullptr;
    std::wstring error;
    std::mutex lock;
    std::wstring status = L"Preparing";
    std::wstring detail;
    std::atomic<int> permille{-1};
    int lastPosted = -2;
    int anim = 0;
    UINT dpi = 96;
};

HMODULE LoadLoader(const std::wstring& path)
{
    if (!util::FileExists(path)) return nullptr;
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

bool RuntimeAvailable(HMODULE loader)
{
    if (!loader) return false;
    auto query = reinterpret_cast<decltype(&GetAvailableCoreWebView2BrowserVersionString)>(
        GetProcAddress(loader, "GetAvailableCoreWebView2BrowserVersionString"));
    if (!query) return false;
    LPWSTR version = nullptr;
    HRESULT hr = query(nullptr, &version);
    bool ok = SUCCEEDED(hr) && version && *version;
    if (version) CoTaskMemFree(version);
    return ok;
}

bool IsMicrosoftSigned(const std::wstring& file)
{
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    bool ok = false;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
        if (sg && sg->csCertChain > 0 && sg->pasCertChain[0].pCert) {
            wchar_t name[256]{};
            CertGetNameStringW(sg->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
            ok = wcscmp(name, L"Microsoft Corporation") == 0;
        }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    return ok;
}

DWORD RunHidden(std::wstring cmd, DWORD timeout)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return static_cast<DWORD>(-1);
    }
    DWORD code = static_cast<DWORD>(-1);
    if (WaitForSingleObject(pi.hProcess, timeout) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

std::wstring SystemDir()
{
    wchar_t b[MAX_PATH]{};
    GetSystemDirectoryW(b, MAX_PATH);
    return b;
}

bool ExtractEntry(const std::wstring& zip, const std::wstring& outDir, const std::wstring& dest)
{
    std::wstring staged = outDir + L"\\WebView2Loader.dll";
    DeleteFileW(staged.c_str());
    std::wstring tar = SystemDir() + L"\\tar.exe";
    if (util::FileExists(tar)) {
        std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + outDir + L"\" --strip-components 3 \"" +
                           kEntry + L"\"";
        if (RunHidden(cmd, 60000) == 0 && util::FileExists(staged) &&
            MoveFileExW(staged.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            return true;
        }
    }
    std::wstring ps = SystemDir() + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring script =
        L"$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        L"$z=[IO.Compression.ZipFile]::OpenRead('" + zip + L"');try{$e=$z.GetEntry('" + kEntry +
        L"');[IO.Compression.ZipFileExtensions]::ExtractToFile($e,'" + dest + L"',$true)}finally{$z.Dispose()}";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + L"\"";
    return RunHidden(cmd, 120000) == 0 && util::FileExists(dest);
}

std::wstring FormatMb(uint64_t bytes)
{
    wchar_t b[32];
    std::swprintf(b, 32, L"%.1f MB", bytes / 1048576.0);
    return b;
}

void SetStatus(Context& c, const std::wstring& status, const std::wstring& detail, int permille)
{
    {
        std::lock_guard<std::mutex> g(c.lock);
        c.status = status;
        c.detail = detail;
    }
    c.permille = permille;
    PostMessageW(c.hwnd, WM_BOOT_UPDATE, 0, 0);
}

bool DownloadStep(Context& c, const wchar_t* url, const std::wstring& dest, const wchar_t* label)
{
    std::wstring err;
    bool ok = net::Download(url, dest, [&](uint64_t done, uint64_t total) {
        int pm = total ? static_cast<int>(done * 1000 / total) : -1;
        if (pm == c.lastPosted) return;
        c.lastPosted = pm;
        std::wstring detail = total ? FormatMb(done) + L" / " + FormatMb(total) : FormatMb(done);
        SetStatus(c, label, detail, pm);
    }, err);
    if (!ok) c.error = std::wstring(label) + L" failed: " + err;
    return ok;
}

void Worker(Context* pc)
{
    Context& c = *pc;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = util::EnsureDir(c.deps);
    if (!ok) c.error = L"Cannot create folder: " + c.deps;

    if (ok && !c.loader) {
        std::wstring pkg = c.deps + L"\\webview2.nupkg";
        ok = DownloadStep(c, kLoaderUrl, pkg, L"Downloading WebView2 loader");
        if (ok) {
            SetStatus(c, L"Extracting WebView2Loader.dll", L"", -1);
            std::wstring tmp = c.loaderPath + L".new";
            ok = ExtractEntry(pkg, c.deps, tmp);
            if (!ok) c.error = L"Could not extract WebView2Loader.dll from the package.";
            if (ok) {
                SetStatus(c, L"Verifying signature", L"", -1);
                ok = IsMicrosoftSigned(tmp);
                if (!ok) c.error = L"WebView2Loader.dll signature verification failed.";
            }
            if (ok) {
                ok = MoveFileExW(tmp.c_str(), c.loaderPath.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
                if (!ok) c.error = L"Could not place WebView2Loader.dll.";
            }
            DeleteFileW(tmp.c_str());
        }
        DeleteFileW(pkg.c_str());
        if (ok) {
            c.loader = LoadLoader(c.loaderPath);
            if (!c.loader) {
                ok = false;
                c.error = L"WebView2Loader.dll could not be loaded (error " + std::to_wstring(GetLastError()) + L").";
            }
        }
    }

    if (ok && !RuntimeAvailable(c.loader)) {
        std::wstring setup = c.deps + L"\\MicrosoftEdgeWebview2Setup.exe";
        ok = DownloadStep(c, kRuntimeUrl, setup, L"Downloading WebView2 runtime");
        if (ok) {
            SetStatus(c, L"Verifying signature", L"", -1);
            ok = IsMicrosoftSigned(setup);
            if (!ok) c.error = L"WebView2 setup signature verification failed.";
        }
        if (ok) {
            SetStatus(c, L"Installing WebView2 runtime", L"This can take a minute", -1);
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
            sei.lpVerb = L"open";
            sei.lpFile = setup.c_str();
            sei.lpParameters = L"/silent /install";
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 15 * 60 * 1000);
                CloseHandle(sei.hProcess);
            }
            ok = RuntimeAvailable(c.loader);
            if (!ok) c.error = L"WebView2 runtime installation did not complete.";
        }
        DeleteFileW(setup.c_str());
    }

    if (ok) SetStatus(c, L"Starting", L"", 1000);
    CoUninitialize();
    PostMessageW(c.hwnd, WM_BOOT_DONE, ok ? 1 : 0, 0);
}

int Px(const Context& c, int v)
{
    return MulDiv(v, static_cast<int>(c.dpi), 96);
}

void Paint(Context& c, HDC dc, const RECT& rc)
{
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(14, 15, 17));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    std::wstring status, detail;
    {
        std::lock_guard<std::mutex> g(c.lock);
        status = c.status;
        detail = c.detail;
    }
    int pm = c.permille;

    SetBkMode(mem, TRANSPARENT);
    HFONT fSmall = CreateFontW(-Px(c, 11), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                              L"Segoe UI");
    HFONT big = CreateFontW(-Px(c, 15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                            L"Segoe UI");
    HFONT mono = CreateFontW(-Px(c, 11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             L"Consolas");
    int pad = Px(c, 22);
    RECT r{pad, Px(c, 20), rc.right - pad, Px(c, 36)};
    HGDIOBJ oldFont = SelectObject(mem, fSmall);
    SetTextColor(mem, RGB(110, 116, 125));
    DrawTextW(mem, L"ROBLOX FPS MANAGER", -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(mem, big);
    SetTextColor(mem, RGB(231, 232, 234));
    r = {pad, Px(c, 42), rc.right - pad, Px(c, 64)};
    DrawTextW(mem, status.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    SelectObject(mem, mono);
    SetTextColor(mem, RGB(139, 144, 153));
    r = {pad, Px(c, 66), rc.right - pad, Px(c, 84)};
    DrawTextW(mem, detail.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (pm >= 0) {
        wchar_t pct[16];
        std::swprintf(pct, 16, L"%d%%", pm / 10);
        DrawTextW(mem, pct, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(mem, oldFont);

    RECT track{pad, rc.bottom - Px(c, 26), rc.right - pad, rc.bottom - Px(c, 22)};
    HBRUSH tb = CreateSolidBrush(RGB(37, 39, 44));
    FillRect(mem, &track, tb);
    DeleteObject(tb);
    HBRUSH fb = CreateSolidBrush(RGB(61, 139, 253));
    int w = track.right - track.left;
    RECT fill = track;
    if (pm >= 0) {
        fill.right = track.left + w * pm / 1000;
    } else {
        int seg = w / 4;
        int pos = (c.anim % (w + seg)) - seg;
        fill.left = track.left + (pos < 0 ? 0 : pos);
        fill.right = track.left + ((pos + seg) > w ? w : pos + seg);
    }
    if (fill.right > fill.left) FillRect(mem, &fill, fb);
    DeleteObject(fb);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    DeleteObject(fSmall);
    DeleteObject(big);
    DeleteObject(mono);
}

LRESULT CALLBACK BootProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    auto* c = reinterpret_cast<Context*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (c) Paint(*c, dc, rc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (c && w == kAnimTimer && c->permille < 0) {
            c->anim += Px(*c, 5);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_BOOT_UPDATE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_BOOT_DONE:
        KillTimer(h, kAnimTimer);
        PostQuitMessage(static_cast<int>(w));
        return 0;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        return r == HTCLIENT ? HTCAPTION : r;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}

HMODULE EnsureRuntime(HINSTANCE instance, std::wstring& error)
{
    Context c;
    c.dir = util::ExeDir();
    c.deps = c.dir + L"\\deps";
    c.loaderPath = c.deps + L"\\WebView2Loader.dll";
    c.loader = LoadLoader(c.loaderPath);
    if (c.loader && RuntimeAvailable(c.loader)) return c.loader;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BootProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    c.dpi = GetDpiForSystem();
    int ww = Px(c, 420), wh = Px(c, 128);
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;
    c.hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Roblox FPS Manager", WS_POPUP, x, y, ww, wh, nullptr, nullptr,
                             instance, &c);
    if (!c.hwnd) {
        error = L"Could not create setup window.";
        return nullptr;
    }
    UINT dpi = GetDpiForWindow(c.hwnd);
    if (dpi && dpi != c.dpi) {
        c.dpi = dpi;
        ww = Px(c, 420);
        wh = Px(c, 128);
        SetWindowPos(c.hwnd, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2, ww, wh, SWP_NOZORDER);
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(c.hwnd, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(c.hwnd, 33, &corner, sizeof(corner));
    COLORREF border = RGB(37, 39, 44);
    DwmSetWindowAttribute(c.hwnd, 34, &border, sizeof(border));
    ShowWindow(c.hwnd, SW_SHOW);
    UpdateWindow(c.hwnd);
    SetTimer(c.hwnd, kAnimTimer, 16, nullptr);

    std::thread worker(Worker, &c);
    MSG msg;
    int result = 0;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    result = static_cast<int>(msg.wParam);
    worker.join();
    DestroyWindow(c.hwnd);
    UnregisterClassW(kClass, instance);
    if (!result) {
        error = c.error.empty() ? L"Setup failed." : c.error;
        if (c.loader) FreeLibrary(c.loader);
        return nullptr;
    }
    return c.loader;
}

}
