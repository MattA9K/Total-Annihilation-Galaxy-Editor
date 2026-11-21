#include "core/TextPreview.hpp"

#include <algorithm>
#include <initializer_list>

namespace galaxy {

namespace {

std::string extensionOf(std::string_view name) {
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return std::string();
    std::string extension(name.substr(dot + 1));
    for (char& c : extension) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return extension;
}

bool oneOf(const std::string& extension, std::initializer_list<const char*> list) {
    return std::any_of(list.begin(), list.end(), [&](const char* item) { return extension == item; });
}

// The length of the valid UTF-8 sequence at data, or 0 when there is none.
std::size_t utf8Length(const std::uint8_t* data, std::size_t size) {
    const std::uint8_t lead = data[0];
    std::size_t length = 0;
    std::uint32_t lowest = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        lowest = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        lowest = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        lowest = 0x10000;
    } else {
        return 0;
    }
    if (size < length) return 0;
    std::uint32_t code = lead & (0xFF >> (length + 1));
    for (std::size_t i = 1; i < length; ++i) {
        if ((data[i] & 0xC0) != 0x80) return 0;
        code = (code << 6) | (data[i] & 0x3F);
    }
    if (code < lowest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0;
    return length;
}

}  // namespace

bool hasTextExtension(std::string_view name) {
    return oneOf(extensionOf(name), {"txt", "fbi", "tdf", "ota", "gui", "bos", "h", "ini", "cfg", "log", "csv", "md", "json", "xml", "rms", "lst", "bat"});
}

bool hasBinaryExtension(std::string_view name) {
    return oneOf(extensionOf(name), {"pcx", "bmp", "jpg", "jpeg", "png", "tga", "gaf", "3do", "cob", "wav", "tnt", "sct", "hpi", "ufo", "ccx", "gp3", "gpf", "zip", "exe", "dll", "fnt", "pal", "alp", "lht", "shd", "pdf"});
}

bool looksLikeText(const std::uint8_t* data, std::size_t size) {
    const std::size_t checked = std::min<std::size_t>(size, 8192);
    std::size_t odd = 0;
    for (std::size_t i = 0; i < checked; ++i) {
        const std::uint8_t c = data[i];
        if (c == 0) return false;
        // Tab, line feed, form feed, carriage return and the DOS end-of-file mark are text.
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\f' && c != '\r' && c != 0x1A) ++odd;
        if (c == 0x7F) ++odd;
    }
    return odd * 50 <= checked;  // up to 2%
}

DisplayText displayText(const std::uint8_t* data, std::size_t size, std::size_t limit, int tabWidth) {
    DisplayText out;
    const std::size_t shown = std::min(size, limit);
    out.truncated = shown < size;
    out.text.reserve(shown + shown / 8);
    int column = 0;
    for (std::size_t i = 0; i < shown;) {
        const std::uint8_t c = data[i];
        if (c == '\r') {
            out.text.push_back('\n');
            column = 0;
            i += (i + 1 < size && data[i + 1] == '\n') ? 2 : 1;
        } else if (c == '\n') {
            out.text.push_back('\n');
            column = 0;
            ++i;
        } else if (c == '\t') {
            const int spaces = tabWidth > 0 ? tabWidth - column % tabWidth : 1;
            out.text.append(static_cast<std::size_t>(spaces), ' ');
            column += spaces;
            ++i;
        } else if (c < 0x20 || c == 0x7F) {
            ++i;
        } else if (c < 0x80) {
            out.text.push_back(static_cast<char>(c));
            ++column;
            ++i;
        } else if (const std::size_t length = utf8Length(data + i, shown - i)) {
            out.text.append(reinterpret_cast<const char*>(data + i), length);
            ++column;
            i += length;
        } else {
            // Latin-1. The C1 range has no printable characters, so it shows as "?".
            if (c < 0xA0) {
                out.text.push_back('?');
            } else {
                out.text.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.text.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            ++column;
            ++i;
        }
    }

    // Lines in the whole file: line ends, plus a last line without one. A DOS end-of-file
    // mark after the last line end is not a line.
    while (size > 0 && data[size - 1] == 0x1A) --size;
    int lines = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] == '\n' || (data[i] == '\r' && (i + 1 >= size || data[i + 1] != '\n'))) ++lines;
    }
    if (size > 0 && data[size - 1] != '\n' && data[size - 1] != '\r') ++lines;
    out.lines = lines;
    return out;
}

}  // namespace galaxy
