#pragma once
#include <windows.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "WebView2.h"
#include "live.h"
#include "settings.h"
#include "tweaks.h"

class App {
public:
    static constexpr wchar_t kClass[] = L"RobloxFpsManager.Main";

    App(HINSTANCE instance, HMODULE loader);
    ~App();
    int Run(int show);

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT Handle(UINT m, WPARAM w, LPARAM l);

    bool CreateMainWindow(int show);
    void ApplyFrame();
    void InitWebView();
    void OnControllerReady(ICoreWebView2Controller* controller);
    void OnResource(ICoreWebView2WebResourceRequestedEventArgs* args);
    void OnMessage(const std::wstring& msg);
    void Resize();
    int FrameX() const;
    int FrameY() const;
    int TopInset() const;
    void SavePlacement();

    void LoadConfig();
    void SaveConfig();
    void StartWatcher();
    void StopWatcher();
    static DWORD WINAPI WatchThread(LPVOID p);

    void Refresh();
    void Poll();
    void PushState(bool force = false);
    void Log(const char* kind, const std::string& text);
    void Post(const std::string& json);
    int RefreshRate() const;
    std::wstring BackupPath() const;
    void ApplyExeTweaks(const std::vector<std::wstring>& exes, bool fso, bool gpu);
    void WriteFlags(bool announce);
    void UpdateLive();
    void PumpFrames();
    void Elevate();
    std::string PowerJson() const;

    std::vector<DWORD> pids_;
    std::vector<std::wstring> exes_;
    std::string exeKey_;
    int priority_ = 0;
    bool fso_ = false;
    bool gpu_ = false;
    bool liveEnabled_ = true;
    bool liveTried_ = false;
    bool elevated_ = false;
    bool flagsManaged_ = false;
    int flagDirs_ = 0;
    DWORD livePid_ = 0;
    std::string flags_;
    std::string profiles_;
    tweaks::GpuInfo gpus_;
    LiveMonitor live_;

    HINSTANCE instance_;
    HMODULE loader_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    RobloxSettings settings_;
    std::wstring dataDir_;
    std::wstring configPath_;
    int target_ = 240;
    bool enforce_ = false;
    bool ready_ = false;
    bool roblox_ = false;
    int hz_ = 0;
    std::string lastState_;
    std::vector<std::string> pending_;
    HANDLE watchThread_ = nullptr;
    HANDLE watchStop_ = nullptr;
    std::wstring watchDir_;
};
