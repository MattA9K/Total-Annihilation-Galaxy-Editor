#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace galaxy {

// Names whose extension says the file is text: TA unit and feature definitions (.fbi,
// .tdf), map descriptions, interface layouts, script sources, .txt and the like.
bool hasTextExtension(std::string_view name);

// Names whose extension says the file is binary: pictures, models, sounds, archives and
// maps. Such files are never shown as text even when their first bytes would pass.
bool hasBinaryExtension(std::string_view name);

// True when the bytes read as text: no NUL bytes and almost no other control characters.
// Only the first few kilobytes are looked at. An empty file counts as text.
bool looksLikeText(const std::uint8_t* data, std::size_t size);

struct DisplayText {
    std::string text;   // UTF-8 with "\n" line ends and tabs turned into spaces
    int lines = 0;      // lines in the whole file, not just the part shown
    bool truncated = false;
};

// Text ready for a text box. UTF-8 stays as it is, and any byte that is not part of a valid
// UTF-8 sequence is read as Latin-1, which is what TA files use (e.g. "Véhicule"). Line ends
// become "\n", tabs are expanded to stops every tabWidth columns, other control characters are
// dropped. At most limit bytes of the file are shown.
DisplayText displayText(const std::uint8_t* data, std::size_t size, std::size_t limit = 512 * 1024, int tabWidth = 4);

}  // namespace galaxy
