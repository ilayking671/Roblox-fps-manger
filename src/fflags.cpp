#include "fflags.h"
#include "util.h"

namespace fflags {

bool LooksLikeObject(const std::string& json)
{
    size_t b = json.find_first_not_of(" \t\r\n");
    size_t e = json.find_last_not_of(" \t\r\n");
    return b != std::string::npos && json[b] == '{' && json[e] == '}' && json.size() < (1 << 20);
}

int Write(const std::vector<std::wstring>& versionDirs, const std::string& json, std::string& error)
{
    if (!LooksLikeObject(json)) {
        error = "Invalid FFlag data";
        return -1;
    }
    int written = 0;
    for (const auto& dir : versionDirs) {
        std::wstring cs = dir + L"\\ClientSettings";
        std::wstring file = cs + L"\\ClientAppSettings.json";
        std::string current;
        if (util::ReadAll(file, current) && current == json) {
            ++written;
            continue;
        }
        if (!util::EnsureDir(cs) || !util::WriteAtomic(file, json)) {
            error = "Could not write " + util::Narrow(file);
            continue;
        }
        ++written;
    }
    return written;
}

std::string ReadNewest(const std::vector<std::wstring>& versionDirs)
{
    FILETIME best{};
    std::string out;
    for (const auto& dir : versionDirs) {
        std::wstring file = dir + L"\\ClientSettings\\ClientAppSettings.json";
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &a)) continue;
        if (CompareFileTime(&a.ftLastWriteTime, &best) <= 0) continue;
        std::string data;
        if (util::ReadAll(file, data) && LooksLikeObject(data)) {
            best = a.ftLastWriteTime;
            out = data;
        }
    }
    return out;
}

}
