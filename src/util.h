#pragma once
#include <windows.h>
#include <string>

namespace util {

std::wstring ExeDir();
std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);
std::string JsonEscape(const std::string& s);
bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
bool EnsureDir(const std::wstring& path);
bool ReadAll(const std::wstring& path, std::string& out);
bool WriteAtomic(const std::wstring& path, const std::string& data);
std::string FormatTime(const FILETIME& ft);
std::wstring FileName(const std::wstring& path);
std::wstring ParentDir(const std::wstring& path);

}
