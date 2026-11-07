#pragma once

#include <string>
#include <string_view>

namespace galaxy {

// The JSON body posted to the update service. `later` is false for the check
// made when the editor opens and true for each check made after that.
std::string updateCheckBody(std::string_view version, std::string_view platform, std::string_view operatingSystem, bool later);

}  // namespace galaxy
