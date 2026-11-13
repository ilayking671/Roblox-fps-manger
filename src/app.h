#pragma once
#include <windows.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "WebView2.h"
#include "settings.h"

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
    bool RobloxRunning() const;
    int RefreshRate() const;
    std::wstring BackupPath() const;

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
