#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include "app.h"
#include "bootstrap.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--after") != 0) continue;
        DWORD pid = static_cast<DWORD>(_wtoi(argv[i + 1]));
        if (HANDLE prev = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            WaitForSingleObject(prev, 10000);
            CloseHandle(prev);
        }
    }
    if (argv) LocalFree(argv);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\RobloxFpsManager.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(App::kClass, nullptr);
        if (existing) {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    std::wstring error;
    HMODULE loader = boot::EnsureRuntime(instance, error);
    if (!loader) {
        MessageBoxW(nullptr, error.c_str(), L"Roblox FPS Manager", MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }
    int code = 0;
    {
        App app(instance, loader);
        code = app.Run(show);
    }
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
