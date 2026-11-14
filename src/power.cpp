#include "power.h"
#include <objbase.h>
#include <powrprof.h>

namespace power {

namespace {

constexpr GUID kBalanced = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
constexpr GUID kHigh = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
constexpr GUID kSaver = {0xa1841308, 0x3538, 0x4b1c, {0x91, 0xa7, 0x7e, 0x2b, 0x2c, 0x82, 0xc6, 0xd6}};
constexpr GUID kUltimate = {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};
constexpr GUID kOverlayBest = {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};
constexpr GUID kOverlayEfficiency = {0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
constexpr GUID kOverlayBattery = {0x3af9b8d9, 0x7c97, 0x431d, {0xad, 0x78, 0x34, 0xa8, 0xbf, 0xea, 0x43, 0x9f}};
constexpr GUID kOverlayBalanced = {};
constexpr GUID kProcGroup = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
constexpr GUID kProcMin = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
constexpr GUID kProcMax = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};

using GetOverlayFn = DWORD(WINAPI*)(GUID*);
using SetOverlayFn = DWORD(WINAPI*)(GUID*);

HMODULE PowrProf()
{
    static HMODULE m = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return m;
}

std::wstring ToString(const GUID& g)
{
    wchar_t b[40]{};
    StringFromGUID2(g, b, 40);
    return b;
}

bool FromString(const std::wstring& s, GUID& g)
{
    return SUCCEEDED(CLSIDFromString(s.c_str(), &g));
}

}

std::vector<Plan> Plans()
{
    std::vector<Plan> out;
    for (ULONG i = 0;; ++i) {
        GUID g{};
        DWORD size = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, reinterpret_cast<UCHAR*>(&g), &size) !=
            ERROR_SUCCESS) {
            break;
        }
        wchar_t name[256]{};
        DWORD n = sizeof(name);
        PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, reinterpret_cast<UCHAR*>(name), &n);
        out.push_back({ToString(g), name});
    }
    return out;
}

std::wstring ActivePlan()
{
    GUID* g = nullptr;
    std::wstring r;
    if (PowerGetActiveScheme(nullptr, &g) == ERROR_SUCCESS && g) r = ToString(*g);
    if (g) LocalFree(g);
    return r;
}

bool SetActivePlan(const std::wstring& id)
{
    GUID g{};
    return FromString(id, g) && PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS;
}

std::string PlanKind(const std::wstring& id)
{
    GUID g{};
    if (!FromString(id, g)) return "other";
    if (IsEqualGUID(g, kBalanced)) return "balanced";
    if (IsEqualGUID(g, kHigh)) return "high";
    if (IsEqualGUID(g, kUltimate)) return "ultimate";
    if (IsEqualGUID(g, kSaver)) return "saver";
    DWORD minState = 0, maxState = 100;
    bool hasMin = PowerReadACValueIndex(nullptr, &g, &kProcGroup, &kProcMin, &minState) == ERROR_SUCCESS;
    bool hasMax = PowerReadACValueIndex(nullptr, &g, &kProcGroup, &kProcMax, &maxState) == ERROR_SUCCESS;
    if (hasMax && maxState < 100) return "saver";
    if (hasMin && minState >= 100) return "high";
    return "other";
}

bool OverlaySupported()
{
    HMODULE m = PowrProf();
    return m && GetProcAddress(m, "PowerGetEffectiveOverlayScheme") && GetProcAddress(m, "PowerSetActiveOverlayScheme");
}

std::string Overlay()
{
    if (!OverlaySupported()) return "none";
    auto fn = reinterpret_cast<GetOverlayFn>(GetProcAddress(PowrProf(), "PowerGetEffectiveOverlayScheme"));
    GUID g{};
    if (fn(&g) != ERROR_SUCCESS) return "none";
    if (IsEqualGUID(g, kOverlayBest)) return "best";
    if (IsEqualGUID(g, kOverlayEfficiency) || IsEqualGUID(g, kOverlayBattery)) return "efficiency";
    if (IsEqualGUID(g, kOverlayBalanced)) return "balanced";
    return "other";
}

bool SetOverlay(const std::string& kind)
{
    if (!OverlaySupported()) return false;
    GUID g = kind == "best" ? kOverlayBest : kind == "efficiency" ? kOverlayEfficiency : kOverlayBalanced;
    auto fn = reinterpret_cast<SetOverlayFn>(GetProcAddress(PowrProf(), "PowerSetActiveOverlayScheme"));
    return fn(&g) == ERROR_SUCCESS;
}

}
