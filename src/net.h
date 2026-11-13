#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>

namespace net {

using Progress = std::function<void(uint64_t done, uint64_t total)>;

bool Download(const std::wstring& url, const std::wstring& dest, const Progress& progress, std::wstring& error);

}
