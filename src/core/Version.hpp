#pragma once

#include <string>

namespace galaxy {

// The release number, such as "1.0.0". It comes from the VERSION file at the top
// of the project, which CMake checks and compiles in, so this is the one place
// to change it.
const char* versionString();

// "Galaxy Editor v1.0.0", as shown in the title bar and the About box.
std::string applicationTitle();

}  // namespace galaxy
