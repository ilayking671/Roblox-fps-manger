#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace power {

struct Plan {
    std::wstring id;
    std::wstring name;
};

std::vector<Plan> Plans();
std::wstring ActivePlan();
bool SetActivePlan(const std::wstring& id);
std::string PlanKind(const std::wstring& id);

bool OverlaySupported();
std::string Overlay();
bool SetOverlay(const std::string& kind);

}
