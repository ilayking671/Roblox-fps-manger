#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tweaks {

std::vector<DWORD> RobloxPids();
std::vector<std::wstring> VersionDirs();
std::vector<std::wstring> PlayerExes(const std::vector<DWORD>& pids);
std::wstring Lower(const std::wstring& s);

int ApplyPriority(const std::vector<DWORD>& pids, int level);

bool FsoDisabled(const std::wstring& exe);
bool SetFsoDisabled(const std::wstring& exe, bool disabled);

bool HighPerfGpu(const std::wstring& exe);
bool SetHighPerfGpu(const std::wstring& exe, bool on);

struct GpuInfo {
    std::wstring highPerf;
    std::wstring powerSaving;
    int count = 0;
};
GpuInfo QueryGpus();

bool IsElevated();

}
