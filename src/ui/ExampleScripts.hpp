#pragma once

#include <cstddef>

// The scripts the Random Map Generator's Example Scripts menu loads, one for each
// base tileset, each mixing a little of two other tilesets into its own, and
// Frostfall Reach, a continent of six.
//
// Each script is its own file in assets/rms, named "NN World - Title.rms". CMake
// compiles them into the executable, in file name order, so edit a script there and
// build again.

namespace galaxy {

struct ExampleRandomMapScript {
    const char* world;
    const char* title;
    const char* text;
};

// Defined in a file CMake writes from assets/rms at configure time.
extern const ExampleRandomMapScript kExampleRandomMapScripts[];
extern const std::size_t kExampleRandomMapScriptCount;

}  // namespace galaxy
