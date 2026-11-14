#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace fflags {

int Write(const std::vector<std::wstring>& versionDirs, const std::string& json, std::string& error);
std::string ReadNewest(const std::vector<std::wstring>& versionDirs);
bool LooksLikeObject(const std::string& json);

}
