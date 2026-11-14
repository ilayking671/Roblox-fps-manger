#include "app.h"
#include "resource.h"
#include "util.h"
#include "fflags.h"
#include "power.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdio>
#include "WebView2EnvironmentOptions.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_FS_CHANGED = WM_APP + 1;
constexpr UINT_PTR kTimerFs = 1;
constexpr UINT_PTR kTimerPoll = 2;
constexpr UINT_PTR kTimerFrames = 3;
constexpr int kWidth = 920;
constexpr int kHeight = 640;
constexpr int kMinWidth = 820;
constexpr int kMinHeight = 600;
constexpr int kMinFps = 1;
constexpr int kMaxFps = 9999;
constexpr COLORREF kBg = RGB(14, 15, 17);
constexpr wchar_t kHost[] = L"https://fps.local/";

struct Asset {
    const wchar_t* path;
    int id;
    const wchar_t* type;
};

constexpr Asset kAssets[] = {
    {L"index.html", IDR_HTML, L"text/html; charset=utf-8"},
    {L"style.css", IDR_CSS, L"text/css; charset=utf-8"},
    {L"app.js", IDR_JS, L"text/javascript; charset=utf-8"},
};

std::string Bool(bool b)
{
    return b ? "true" : "false";
}

}

App::App(HINSTANCE instance, HMODULE loader) : instance_(instance), loader_(loader)
{
    dataDir_ = util::ExeDir() + L"\\data";
    util::EnsureDir(dataDir_);
    configPath_ = dataDir_ + L"\\config.ini";
    LoadConfig();
    settings_.Resolve();
    elevated_ = tweaks::IsElevated();
    gpus_ = tweaks::QueryGpus();
    flagsManaged_ = util::ReadAll(dataDir_ + L"\\fflags.json", flags_) && fflags::LooksLikeObject(flags_);
    if (!flagsManaged_) {
        flags_ = fflags::ReadNewest(tweaks::VersionDirs());
        if (!fflags::LooksLikeObject(flags_)) flags_ = "{}";
    }
    if (!util::ReadAll(dataDir_ + L"\\profiles.json", profiles_)) profiles_.clear();
}

App::~App()
{
    live_.Stop();
    StopWatcher();
}

void App::LoadConfig()
{
    const wchar_t* f = configPath_.c_str();
    target_ = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"fps", L"target", 240, f)), kMinFps, kMaxFps);
    enforce_ = GetPrivateProfileIntW(L"fps", L"enforce", 0, f) != 0;
    priority_ = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"tweaks", L"priority", 0, f)), 0, 2);
    fso_ = GetPrivateProfileIntW(L"tweaks", L"fso", 0, f) != 0;
    gpu_ = GetPrivateProfileIntW(L"tweaks", L"gpu", 0, f) != 0;
    liveEnabled_ = GetPrivateProfileIntW(L"live", L"enabled", 1, f) != 0;
}

void App::SaveConfig()
{
    const wchar_t* f = configPath_.c_str();
    WritePrivateProfileStringW(L"fps", L"target", std::to_wstring(target_).c_str(), f);
    WritePrivateProfileStringW(L"fps", L"enforce", enforce_ ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"tweaks", L"priority", std::to_wstring(priority_).c_str(), f);
    WritePrivateProfileStringW(L"tweaks", L"fso", fso_ ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"tweaks", L"gpu", gpu_ ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"live", L"enabled", liveEnabled_ ? L"1" : L"0", f);
}

std::wstring App::BackupPath() const
{
    return dataDir_ + L"\\backup\\" + util::FileName(settings_.Path());
}

int App::Run(int show)
{
    if (!CreateMainWindow(show)) return 1;
    InitWebView();
    StartWatcher();
    hz_ = RefreshRate();
    Poll();
    SetTimer(hwnd_, kTimerPoll, 1500, nullptr);
    SetTimer(hwnd_, kTimerFrames, 100, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool App::CreateMainWindow(int show)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!RegisterClassExW(&wc)) return false;

    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"))) {
            fn(mon, 0, &dpiX, &dpiY);
        }
    }
    dpi_ = dpiX ? dpiX : 96;
    int w = MulDiv(kWidth, dpi_, 96) + 2 * FrameX();
    int h = MulDiv(kHeight, dpi_, 96) + FrameY();
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;

    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Roblox FPS Manager", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y,
                            w, h, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    ApplyFrame();

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    bool restored = GetPrivateProfileStructW(L"window", L"placement", &wp, sizeof(wp), configPath_.c_str()) &&
                    wp.length == sizeof(wp) && MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL);
    if (restored) {
        bool maximized = wp.showCmd == SW_SHOWMAXIMIZED;
        wp.showCmd = SW_HIDE;
        wp.flags = 0;
        SetWindowPlacement(hwnd_, &wp);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(hwnd_, maximized ? SW_SHOWMAXIMIZED : (show == SW_HIDE ? SW_SHOWNORMAL : show));
    } else {
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(hwnd_, show == SW_HIDE ? SW_SHOWNORMAL : show);
    }
    UpdateWindow(hwnd_);
    return true;
}

int App::FrameX() const
{
    return GetSystemMetricsForDpi(SM_CXFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
}

int App::FrameY() const
{
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
}

int App::TopInset() const
{
    return IsZoomed(hwnd_) ? 0 : MulDiv(5, dpi_, 96);
}

void App::SavePlacement()
{
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd_, &wp)) {
        if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = (wp.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        WritePrivateProfileStructW(L"window", L"placement", &wp, sizeof(wp), configPath_.c_str());
    }
}

void App::ApplyFrame()
{
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof(corner));
    COLORREF border = RGB(37, 39, 44);
    DwmSetWindowAttribute(hwnd_, 34, &border, sizeof(border));
    MARGINS m{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd_, &m);
}

void App::InitWebView()
{
    auto create = reinterpret_cast<decltype(&CreateCoreWebView2EnvironmentWithOptions)>(
        GetProcAddress(loader_, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create) {
        MessageBoxW(hwnd_, L"WebView2Loader.dll is invalid.", L"Roblox FPS Manager", MB_ICONERROR);
        DestroyWindow(hwnd_);
        return;
    }
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--disable-background-networking --disable-component-update --disable-sync --no-first-run "
        L"--disable-features=msSmartScreenProtection,Translate,msEdgeTranslate,AutofillServerCommunication");
    std::wstring udf = dataDir_ + (elevated_ ? L"\\webview-admin" : L"\\webview");
    HRESULT hr = create(
        nullptr, udf.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    MessageBoxW(hwnd_, L"Failed to start WebView2.", L"Roblox FPS Manager", MB_ICONERROR);
                    DestroyWindow(hwnd_);
                    return S_OK;
                }
                env_ = env;
                env_->CreateCoreWebView2Controller(
                    hwnd_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                               [this](HRESULT r, ICoreWebView2Controller* c) -> HRESULT {
                                   if (FAILED(r) || !c) {
                                       MessageBoxW(hwnd_, L"Failed to create WebView2 controller.",
                                                   L"Roblox FPS Manager", MB_ICONERROR);
                                       DestroyWindow(hwnd_);
                                       return S_OK;
                                   }
                                   OnControllerReady(c);
                                   return S_OK;
                               })
                               .Get());
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) {
        MessageBoxW(hwnd_, L"Failed to initialize WebView2.", L"Roblox FPS Manager", MB_ICONERROR);
        DestroyWindow(hwnd_);
    }
}

void App::OnControllerReady(ICoreWebView2Controller* controller)
{
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);

    ComPtr<ICoreWebView2Controller2> c2;
    if (SUCCEEDED(controller_.As(&c2))) {
        COREWEBVIEW2_COLOR bg{255, 14, 15, 17};
        c2->put_DefaultBackgroundColor(bg);
    }

    ComPtr<ICoreWebView2Settings> s;
    webview_->get_Settings(&s);
    s->put_AreDefaultContextMenusEnabled(FALSE);
    s->put_IsStatusBarEnabled(FALSE);
    s->put_IsZoomControlEnabled(FALSE);
    s->put_AreDefaultScriptDialogsEnabled(FALSE);
    s->put_IsBuiltInErrorPageEnabled(FALSE);
#ifdef _DEBUG
    s->put_AreDevToolsEnabled(TRUE);
#else
    s->put_AreDevToolsEnabled(FALSE);
#endif
    ComPtr<ICoreWebView2Settings3> s3;
    if (SUCCEEDED(s.As(&s3))) {
#ifndef _DEBUG
        s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
#endif
    }
    ComPtr<ICoreWebView2Settings4> s4;
    if (SUCCEEDED(s.As(&s4))) {
        s4->put_IsPasswordAutosaveEnabled(FALSE);
        s4->put_IsGeneralAutofillEnabled(FALSE);
    }
    ComPtr<ICoreWebView2Settings5> s5;
    if (SUCCEEDED(s.As(&s5))) s5->put_IsPinchZoomEnabled(FALSE);
    ComPtr<ICoreWebView2Settings6> s6;
    if (SUCCEEDED(s.As(&s6))) s6->put_IsSwipeNavigationEnabled(FALSE);
    ComPtr<ICoreWebView2Settings9> s9;
    if (SUCCEEDED(s.As(&s9))) s9->put_IsNonClientRegionSupportEnabled(TRUE);

    EventRegistrationToken tok;
    webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                LPWSTR uri = nullptr;
                a->get_Uri(&uri);
                bool allowed = uri && wcsncmp(uri, kHost, wcslen(kHost)) == 0;
                if (uri) CoTaskMemFree(uri);
                if (!allowed) a->put_Cancel(TRUE);
                return S_OK;
            })
            .Get(),
        &tok);
    webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                a->put_Handled(TRUE);
                return S_OK;
            })
            .Get(),
        &tok);
    webview_->AddWebResourceRequestedFilter((std::wstring(kHost) + L"*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT {
                OnResource(a);
                return S_OK;
            })
            .Get(),
        &tok);
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                LPWSTR msg = nullptr;
                if (SUCCEEDED(a->TryGetWebMessageAsString(&msg)) && msg) {
                    std::wstring m(msg);
                    CoTaskMemFree(msg);
                    OnMessage(m);
                }
                return S_OK;
            })
            .Get(),
        &tok);

    Resize();
    controller_->put_IsVisible(!IsIconic(hwnd_));
    webview_->Navigate((std::wstring(kHost) + L"index.html").c_str());
}

void App::OnResource(ICoreWebView2WebResourceRequestedEventArgs* args)
{
    ComPtr<ICoreWebView2WebResourceRequest> req;
    args->get_Request(&req);
    LPWSTR raw = nullptr;
    req->get_Uri(&raw);
    std::wstring uri = raw ? raw : L"";
    if (raw) CoTaskMemFree(raw);
    std::wstring path = uri.size() > wcslen(kHost) ? uri.substr(wcslen(kHost)) : L"";
    size_t q = path.find_first_of(L"?#");
    if (q != std::wstring::npos) path.resize(q);
    if (path.empty()) path = L"index.html";

    ComPtr<ICoreWebView2WebResourceResponse> resp;
    for (const auto& a : kAssets) {
        if (path != a.path) continue;
        HRSRC r = FindResourceW(instance_, MAKEINTRESOURCEW(a.id), MAKEINTRESOURCEW(10));
        HGLOBAL g = r ? LoadResource(instance_, r) : nullptr;
        const BYTE* p = g ? static_cast<const BYTE*>(LockResource(g)) : nullptr;
        DWORD n = r ? SizeofResource(instance_, r) : 0;
        if (!p) break;
        IStream* stream = SHCreateMemStream(p, n);
        std::wstring headers = std::wstring(L"Content-Type: ") + a.type +
                               L"\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'self'; "
                               L"img-src 'self' data:; style-src 'self'; script-src 'self'";
        env_->CreateWebResourceResponse(stream, 200, L"OK", headers.c_str(), &resp);
        if (stream) stream->Release();
        break;
    }
    if (!resp) env_->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &resp);
    args->put_Response(resp.Get());
}

void App::Resize()
{
    if (!controller_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    rc.top += TopInset();
    controller_->put_Bounds(rc);
}

void App::OnMessage(const std::wstring& raw)
{
    std::wstring cmd = raw, arg;
    size_t c = raw.find(L':');
    if (c != std::wstring::npos) {
        cmd = raw.substr(0, c);
        arg = raw.substr(c + 1);
    }
    std::string err;
    if (cmd == L"ready") {
        ready_ = true;
        Post(std::string("{\"t\":\"win\",\"max\":") + Bool(IsZoomed(hwnd_) != FALSE) + "}");
        Post("{\"t\":\"fflags\",\"managed\":" + Bool(flagsManaged_) + ",\"data\":\"" + util::JsonEscape(flags_) + "\"}");
        Post("{\"t\":\"profiles\",\"data\":\"" + util::JsonEscape(profiles_) + "\"}");
        PushState(true);
        for (auto& p : pending_) Post(p);
        pending_.clear();
    } else if (cmd == L"max") {
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
    } else if (cmd == L"min") {
        ShowWindow(hwnd_, SW_MINIMIZE);
    } else if (cmd == L"close") {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    } else if (cmd == L"apply") {
        int v = std::clamp(_wtoi(arg.c_str()), kMinFps, kMaxFps);
        target_ = v;
        SaveConfig();
        SettingsState st = settings_.Query();
        if (!st.found) {
            Log("err", "Settings file not found");
        } else {
            if (!util::FileExists(BackupPath())) {
                std::string be;
                if (settings_.SaveBackup(BackupPath(), be)) Log("info", "Original settings backed up");
            }
            if (settings_.SetFps(v, err)) {
                Log("ok", "FramerateCap set to " + std::to_string(v) + (roblox_ ? " (applies on next Roblox launch)" : ""));
            } else {
                Log("err", err);
            }
        }
        Refresh();
    } else if (cmd == L"lock") {
        bool on = arg == L"1";
        if (settings_.SetReadOnly(on, err)) {
            Log("ok", on ? "File locked (read-only)" : "File unlocked");
        } else {
            Log("err", err);
        }
        PushState(true);
    } else if (cmd == L"enforce") {
        enforce_ = arg == L"1";
        SaveConfig();
        Log("ok", enforce_ ? "Enforce enabled at " + std::to_string(target_) + " FPS" : "Enforce disabled");
        Refresh();
        PushState(true);
    } else if (cmd == L"open") {
        if (util::FileExists(settings_.Path())) {
            std::wstring a = L"/select,\"" + settings_.Path() + L"\"";
            ShellExecuteW(hwnd_, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
        } else if (util::DirExists(settings_.Dir())) {
            ShellExecuteW(hwnd_, L"open", settings_.Dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (cmd == L"reload") {
        settings_.Resolve();
        StartWatcher();
        Refresh();
        PushState(true);
        Log("info", "Reloaded from disk");
    } else if (cmd == L"backup") {
        if (settings_.SaveBackup(BackupPath(), err)) {
            Log("ok", "Backup saved");
        } else {
            Log("err", err);
        }
        PushState(true);
    } else if (cmd == L"restore") {
        if (enforce_) {
            enforce_ = false;
            SaveConfig();
            Log("info", "Enforce disabled for restore");
        }
        if (settings_.RestoreBackup(BackupPath(), err)) {
            Log("ok", "Backup restored");
        } else {
            Log("err", err);
        }
        Refresh();
        PushState(true);
    } else if (cmd == L"priority") {
        priority_ = std::clamp(_wtoi(arg.c_str()), 0, 2);
        SaveConfig();
        tweaks::ApplyPriority(pids_, priority_);
        static const char* names[] = {"Normal", "Above normal", "High"};
        Log("ok", std::string("Roblox priority: ") + names[priority_]);
        PushState(true);
    } else if (cmd == L"fso") {
        fso_ = arg == L"1";
        SaveConfig();
        ApplyExeTweaks(exes_, true, false);
        Log("ok", std::string(fso_ ? "Fullscreen optimizations disabled" : "Fullscreen optimizations restored") +
                      " for " + std::to_string(exes_.size()) + " Roblox build(s)");
        PushState(true);
    } else if (cmd == L"gpu") {
        gpu_ = arg == L"1";
        SaveConfig();
        ApplyExeTweaks(exes_, false, true);
        Log("ok", std::string(gpu_ ? "High-performance GPU set" : "GPU preference reset") + " for " +
                      std::to_string(exes_.size()) + " Roblox build(s)");
        PushState(true);
    } else if (cmd == L"plan") {
        if (power::SetActivePlan(arg)) {
            Log("ok", "Power plan changed");
        } else {
            Log("err", "Could not change power plan");
        }
        PushState(true);
    } else if (cmd == L"overlay") {
        if (power::SetOverlay(util::Narrow(arg))) {
            Log("ok", "Power mode changed");
        } else {
            Log("err", "Could not change power mode");
        }
        PushState(true);
    } else if (cmd == L"fflags") {
        std::string json = util::Narrow(arg);
        if (!fflags::LooksLikeObject(json)) {
            Log("err", "Invalid FFlag data");
        } else {
            flags_ = json;
            flagsManaged_ = true;
            util::WriteAtomic(dataDir_ + L"\\fflags.json", flags_);
            WriteFlags(true);
        }
        PushState(true);
    } else if (cmd == L"profiles") {
        profiles_ = util::Narrow(arg);
        if (!util::WriteAtomic(dataDir_ + L"\\profiles.json", profiles_)) Log("err", "Could not save profiles");
    } else if (cmd == L"live") {
        liveEnabled_ = arg == L"1";
        liveTried_ = false;
        SaveConfig();
        UpdateLive();
        PushState(true);
    } else if (cmd == L"elevate") {
        Elevate();
    }
}

void App::ApplyExeTweaks(const std::vector<std::wstring>& exes, bool fso, bool gpu)
{
    for (const auto& exe : exes) {
        if (fso && tweaks::FsoDisabled(exe) != fso_) tweaks::SetFsoDisabled(exe, fso_);
        if (gpu && tweaks::HighPerfGpu(exe) != gpu_) tweaks::SetHighPerfGpu(exe, gpu_);
    }
}

void App::WriteFlags(bool announce)
{
    if (!flagsManaged_) return;
    std::string err;
    auto dirs = tweaks::VersionDirs();
    int n = fflags::Write(dirs, flags_, err);
    flagDirs_ = n < 0 ? 0 : n;
    if (!err.empty()) {
        Log("err", err);
    } else if (announce) {
        Log("ok", "FFlags written to " + std::to_string(flagDirs_) + " Roblox build(s)" +
                      (roblox_ ? " (applies on next launch)" : ""));
    }
}

void App::UpdateLive()
{
    bool want = liveEnabled_ && roblox_;
    if (want && !live_.Active() && !liveTried_) {
        liveTried_ = true;
        live_.SetPids(pids_);
        if (live_.Start()) {
            Log("info", "Live frame monitor started");
        } else if (live_.GetStatus() == LiveMonitor::Status::Denied) {
            Log("warn", "Live monitor needs administrator rights");
        } else {
            Log("err", "Live monitor could not start");
        }
    } else if (!want && live_.Active()) {
        live_.Stop();
        livePid_ = 0;
    }
    if (!roblox_) liveTried_ = false;
    if (live_.Active()) live_.SetPids(pids_);
}

void App::PumpFrames()
{
    if (!live_.Active() || !ready_ || IsIconic(hwnd_)) return;
    live_.Flush();
    std::unordered_map<DWORD, std::vector<float>> batch;
    live_.Collect(batch);
    if (batch.empty()) return;
    DWORD fg = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fg);
    DWORD pick = 0;
    if (batch.count(fg)) {
        pick = fg;
    } else if (batch.count(livePid_)) {
        pick = livePid_;
    } else {
        size_t best = 0;
        for (const auto& kv : batch) {
            if (kv.second.size() > best) {
                best = kv.second.size();
                pick = kv.first;
            }
        }
    }
    livePid_ = pick;
    const auto& v = batch[pick];
    std::string j = "{\"t\":\"frames\",\"pid\":" + std::to_string(pick) + ",\"ft\":[";
    j.reserve(j.size() + v.size() * 7);
    char b[24];
    for (size_t i = 0; i < v.size(); ++i) {
        std::snprintf(b, sizeof(b), i ? ",%.3f" : "%.3f", v[i]);
        j += b;
    }
    j += "]}";
    Post(j);
}

void App::Elevate()
{
    wchar_t exe[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring params = L"--after " + std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd_;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    } else {
        Log("warn", "Administrator restart was cancelled");
    }
}

std::string App::PowerJson() const
{
    std::wstring active = power::ActivePlan();
    std::string j = "\"plan\":\"" + util::JsonEscape(util::Narrow(active)) + "\",\"planKind\":\"" +
                    power::PlanKind(active) + "\",\"overlay\":\"" + power::Overlay() + "\",\"plans\":[";
    bool first = true;
    for (const auto& p : power::Plans()) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":\"" + util::JsonEscape(util::Narrow(p.id)) + "\",\"name\":\"" +
             util::JsonEscape(util::Narrow(p.name)) + "\",\"kind\":\"" + power::PlanKind(p.id) + "\"}";
    }
    return j + "]";
}

void App::Refresh()
{
    SettingsState s = settings_.Query();
    if (!s.found) {
        settings_.Resolve();
        s = settings_.Query();
        if (s.found) StartWatcher();
    }
    if (enforce_ && s.found && (!s.hasKey || s.fps != target_)) {
        std::string err;
        int before = s.fps;
        bool had = s.hasKey;
        if (settings_.SetFps(target_, err)) {
            Log("ok", "Enforced " + std::to_string(target_) + " FPS" +
                          (had ? " (file had " + std::to_string(before) + ")" : ""));
        } else {
            Log("err", "Enforce failed: " + err);
        }
    }
    PushState();
}

void App::Poll()
{
    pids_ = tweaks::RobloxPids();
    bool running = !pids_.empty();
    int hz = RefreshRate();
    if (running != roblox_) {
        roblox_ = running;
        Log("info", running ? "Roblox started" : "Roblox closed");
        if (!running) Refresh();
    }
    hz_ = hz;
    if (priority_ > 0 && running) {
        int n = tweaks::ApplyPriority(pids_, priority_);
        if (n > 0) Log("ok", "Priority raised for " + std::to_string(n) + " Roblox process(es)");
    }

    exes_ = tweaks::PlayerExes(pids_);
    std::string key;
    for (const auto& e : exes_) key += util::Narrow(tweaks::Lower(e)) + "|";
    if (key != exeKey_) {
        bool firstScan = exeKey_.empty();
        exeKey_ = key;
        if (fso_ || gpu_) ApplyExeTweaks(exes_, fso_, gpu_);
        if (flagsManaged_) WriteFlags(false);
        if (!firstScan && (fso_ || gpu_ || flagsManaged_)) Log("info", "New Roblox build detected, tweaks re-applied");
    }
    UpdateLive();
    if (watchDir_.empty()) {
        SettingsState s = settings_.Query();
        if (!s.found) Refresh();
    }
    PushState();
}

void App::PushState(bool force)
{
    if (!ready_) return;
    SettingsState s = settings_.Query();
    std::string j = "{\"t\":\"state\",\"found\":" + Bool(s.found) + ",\"hasKey\":" + Bool(s.hasKey) +
                    ",\"fps\":" + std::to_string(s.fps) + ",\"readonly\":" + Bool(s.readOnly) +
                    ",\"size\":" + std::to_string(s.size) + ",\"modified\":\"" + util::JsonEscape(s.modified) +
                    "\",\"path\":\"" + util::JsonEscape(util::Narrow(settings_.Path())) + "\",\"file\":\"" +
                    util::JsonEscape(util::Narrow(util::FileName(settings_.Path()))) +
                    "\",\"roblox\":" + Bool(roblox_) + ",\"hz\":" + std::to_string(hz_) +
                    ",\"enforce\":" + Bool(enforce_) + ",\"target\":" + std::to_string(target_) +
                    ",\"backup\":" + Bool(util::FileExists(BackupPath()));
    int fsoCount = 0, gpuCount = 0;
    for (const auto& e : exes_) {
        if (tweaks::FsoDisabled(e)) ++fsoCount;
        if (tweaks::HighPerfGpu(e)) ++gpuCount;
    }
    static const char* liveNames[] = {"off", "running", "denied", "failed"};
    j += ",\"instances\":" + std::to_string(pids_.size()) + ",\"exes\":" + std::to_string(exes_.size()) +
         ",\"priority\":" + std::to_string(priority_) + ",\"fso\":" + Bool(fso_) +
         ",\"fsoCount\":" + std::to_string(fsoCount) + ",\"gpu\":" + Bool(gpu_) +
         ",\"gpuCount\":" + std::to_string(gpuCount) + ",\"gpuHigh\":\"" +
         util::JsonEscape(util::Narrow(gpus_.highPerf)) + "\",\"gpuLow\":\"" +
         util::JsonEscape(util::Narrow(gpus_.powerSaving)) + "\",\"gpus\":" + std::to_string(gpus_.count) +
         ",\"elevated\":" + Bool(elevated_) + ",\"liveEnabled\":" + Bool(liveEnabled_) + ",\"live\":\"" +
         liveNames[static_cast<int>(live_.GetStatus())] + "\",\"flagDirs\":" + std::to_string(flagDirs_) +
         ",\"versions\":" + std::to_string(tweaks::VersionDirs().size()) + "," + PowerJson() + "}";
    if (!force && j == lastState_) return;
    lastState_ = j;
    Post(j);
}

void App::Log(const char* kind, const std::string& text)
{
    std::string j = std::string("{\"t\":\"log\",\"k\":\"") + kind + "\",\"m\":\"" + util::JsonEscape(text) + "\"}";
    if (!ready_) {
        pending_.push_back(j);
        return;
    }
    Post(j);
}

void App::Post(const std::string& json)
{
    if (webview_) webview_->PostWebMessageAsJson(util::Widen(json).c_str());
}

int App::RefreshRate() const
{
    HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
    return dm.dmDisplayFrequency > 1 ? static_cast<int>(dm.dmDisplayFrequency) : 0;
}

void App::StartWatcher()
{
    std::wstring dir = settings_.Dir();
    if (!util::DirExists(dir)) {
        StopWatcher();
        return;
    }
    if (watchThread_ && dir == watchDir_) return;
    StopWatcher();
    watchDir_ = dir;
    watchStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watchThread_ = CreateThread(nullptr, 0, WatchThread, this, 0, nullptr);
}

void App::StopWatcher()
{
    if (watchThread_) {
        SetEvent(watchStop_);
        WaitForSingleObject(watchThread_, 2000);
        CloseHandle(watchThread_);
        watchThread_ = nullptr;
    }
    if (watchStop_) {
        CloseHandle(watchStop_);
        watchStop_ = nullptr;
    }
    watchDir_.clear();
}

DWORD WINAPI App::WatchThread(LPVOID p)
{
    auto* self = static_cast<App*>(p);
    HANDLE change = FindFirstChangeNotificationW(
        self->watchDir_.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SIZE);
    if (change == INVALID_HANDLE_VALUE) return 1;
    HANDLE waits[2] = {self->watchStop_, change};
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0 + 1) break;
        PostMessageW(self->hwnd_, WM_FS_CHANGED, 0, 0);
        if (!FindNextChangeNotification(change)) break;
    }
    FindCloseChangeNotification(change);
    return 0;
}

LRESULT CALLBACK App::WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE) {
        auto* self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return self ? self->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::Handle(UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NCCALCSIZE:
        if (w) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
            LONG top = p->rgrc[0].top;
            LRESULT r = DefWindowProcW(hwnd_, m, w, l);
            if (r != 0) return r;
            p->rgrc[0].top = top + (IsZoomed(hwnd_) ? FrameY() : 0);
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd_, m, w, l);
        if (hit != HTCLIENT || IsZoomed(hwnd_)) return hit;
        POINT pt{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        ScreenToClient(hwnd_, &pt);
        if (pt.y < FrameY()) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            int corner = FrameX() * 2;
            if (pt.x < corner) return HTTOPLEFT;
            if (pt.x >= rc.right - corner) return HTTOPRIGHT;
            if (pt.y < TopInset()) return HTTOP;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(l);
        mm->ptMinTrackSize.x = MulDiv(kMinWidth, dpi_, 96) + 2 * FrameX();
        mm->ptMinTrackSize.y = MulDiv(kMinHeight, dpi_, 96) + FrameY();
        return 0;
    }
    case WM_NCACTIVATE:
        if (ready_) Post(std::string("{\"t\":\"focus\",\"v\":") + Bool(w != FALSE) + "}");
        return DefWindowProcW(hwnd_, m, w, -1);
    case WM_SIZE:
        if (controller_) {
            controller_->put_IsVisible(w != SIZE_MINIMIZED);
            Resize();
        }
        if (ready_ && w != SIZE_MINIMIZED) Post(std::string("{\"t\":\"win\",\"max\":") + Bool(w == SIZE_MAXIMIZED) + "}");
        return 0;
    case WM_MOVE:
    case WM_MOVING:
        if (controller_) controller_->NotifyParentWindowPositionChanged();
        break;
    case WM_EXITSIZEMOVE:
        Poll();
        break;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        const RECT* r = reinterpret_cast<const RECT*>(l);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        Poll();
        break;
    case WM_FS_CHANGED:
        SetTimer(hwnd_, kTimerFs, 120, nullptr);
        return 0;
    case WM_TIMER:
        if (w == kTimerFs) {
            KillTimer(hwnd_, kTimerFs);
            Refresh();
        } else if (w == kTimerPoll) {
            Poll();
        } else if (w == kTimerFrames) {
            PumpFrames();
        }
        return 0;
    case WM_DESTROY:
        SavePlacement();
        KillTimer(hwnd_, kTimerPoll);
        KillTimer(hwnd_, kTimerFrames);
        live_.Stop();
        KillTimer(hwnd_, kTimerFs);
        StopWatcher();
        if (controller_) controller_->Close();
        controller_.Reset();
        webview_.Reset();
        env_.Reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
