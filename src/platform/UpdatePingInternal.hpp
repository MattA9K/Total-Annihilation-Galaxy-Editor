#pragma once

#include <string>
#include <string_view>

namespace galaxy {

void postUpdateCheck(std::string_view body);
std::string hostPlatformName();
std::string hostOperatingSystem();

}  // namespace galaxy
