#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

struct SettingsState {
    bool found = false;
    bool hasKey = false;
    int fps = 0;
    bool readOnly = false;
    uint64_t size = 0;
    std::string modified;
};

class RobloxSettings {
public:
    void Resolve();
    SettingsState Query() const;
    bool SetFps(int fps, std::string& error);
    bool SetReadOnly(bool on, std::string& error);
    bool SaveBackup(const std::wstring& backupPath, std::string& error) const;
    bool RestoreBackup(const std::wstring& backupPath, std::string& error);
    const std::wstring& Path() const { return path_; }
    const std::wstring& Dir() const { return dir_; }

private:
    bool WriteContent(const std::string& data, std::string& error);
    std::wstring dir_;
    std::wstring path_;
};
