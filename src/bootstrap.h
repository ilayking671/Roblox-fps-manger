#pragma once
#include <windows.h>
#include <string>

namespace boot {

HMODULE EnsureRuntime(HINSTANCE instance, std::wstring& error);

}
