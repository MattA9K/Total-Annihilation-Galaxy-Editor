#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace galaxy {

// The number for the next "Untitled Map N": the lowest number from 1 up that no
// open map uses. Zero and negative entries mean a map with a file and are ignored.
inline int lowestUnusedUntitledNumber(std::vector<int> used) {
    std::sort(used.begin(), used.end());
    int next = 1;
    for (const int number : used) {
        if (number == next) {
            ++next;
        } else if (number > next) {
            break;
        }
    }
    return next;
}

// The name a document tab shows: the file name without its extension, or the
// fallback (the map's own name) when the map has no file yet.
inline std::string documentTabTitle(const std::filesystem::path& source, const std::string& fallback) {
    if (!source.empty()) {
        const std::string stem = source.stem().string();
        if (!stem.empty()) {
            return stem;
        }
    }
    return fallback;
}

// Which document becomes active after the one at closedIndex closes, as an index
// into the list once that document is gone. The neighbour to the right takes its
// place, or the one to the left when it was last. -1 when no document is left.
// Closing a background tab keeps the active document, shifted down past the gap.
inline int activeIndexAfterClose(int count, int active, int closedIndex) {
    if (count <= 1) {
        return -1;
    }
    if (closedIndex != active) {
        return closedIndex < active ? active - 1 : active;
    }
    return std::min(closedIndex, count - 2);
}

}  // namespace galaxy
