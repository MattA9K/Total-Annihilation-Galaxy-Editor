#include "core/Version.hpp"

namespace galaxy {

std::string applicationTitle() {
    return std::string("Galaxy Editor v") + versionString();
}

}  // namespace galaxy
