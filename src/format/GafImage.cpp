#include "format/GafImage.hpp"

#include "format/TaPalette.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace galaxy {
namespace {

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size)
        : data_(data)
        , size_(size) {}

    bool ok() const { return ok_; }

    bool seek(std::size_t pos) {
        if (pos > size_) {
            ok_ = false;
            return false;
        }
        pos_ = pos;
        return true;
    }

    bool u8(std::uint8_t& value) {
        if (pos_ >= size_) {
            ok_ = false;
            return false;
        }
        value = data_[pos_++];
        return true;
    }

    bool u16(std::uint16_t& value) {
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        if (!u8(lo) || !u8(hi)) {
            return false;
        }
        value = static_cast<std::uint16_t>(lo) | (static_cast<std::uint16_t>(hi) << 8);
        return true;
    }

    bool u32(std::uint32_t& value) {
        std::uint16_t lo = 0;
        std::uint16_t hi = 0;
        if (!u16(lo) || !u16(hi)) {
            return false;
        }
        value = static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 16);
        return true;
    }

    bool i16(std::int16_t& value) {
        std::uint16_t raw = 0;
        if (!u16(raw)) {
            return false;
        }
        value = static_cast<std::int16_t>(raw);
        return true;
    }

    bool bytes(std::size_t count, std::vector<std::uint8_t>& out) {
        if (count > size_ - pos_) {
            ok_ = false;
            return false;
        }
        out.assign(data_ + pos_, data_ + pos_ + count);
        pos_ += count;
        return true;
    }

    bool name32(std::string& out) {
        if (pos_ + 32 > size_) {
            ok_ = false;
            return false;
        }
        std::size_t length = 0;
        while (length < 32 && data_[pos_ + length] != 0) {
            ++length;
        }
        out.assign(reinterpret_cast<const char*>(data_ + pos_), length);
        pos_ += 32;
        return true;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

bool sameText(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

struct RawFrame {
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::uint8_t transparency = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<RawFrame> children;
};

bool decompress(Reader& reader, int width, int height, std::uint8_t transparency, std::vector<std::uint8_t>& pixels) {
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), transparency);
    std::size_t cursor = 0;
    for (int y = 0; y < height; ++y) {
        std::uint16_t count = 0;
        if (!reader.u16(count)) {
            return false;
        }
        std::vector<std::uint8_t> row;
        if (!reader.bytes(count, row)) {
            return false;
        }
        std::size_t index = 0;
        int left = width;
        while (left > 0 && index < row.size()) {
            const std::uint8_t mask = row[index++];
            if ((mask & 0x01) != 0) {
                const int skip = std::min(mask >> 1, left);
                if (skip <= 0) {
                    break;
                }
                cursor += static_cast<std::size_t>(skip);
                left -= skip;
            } else if ((mask & 0x02) != 0) {
                if (index >= row.size()) {
                    break;
                }
                const std::uint8_t value = row[index++];
                const int repeat = std::min((mask >> 2) + 1, left);
                for (int i = 0; i < repeat; ++i) {
                    pixels[cursor++] = value;
                }
                left -= repeat;
            } else {
                const int literal = (mask >> 2) + 1;
                const int take = std::min(literal, left);
                int wrote = 0;
                while (wrote < take && index < row.size()) {
                    pixels[cursor++] = row[index++];
                    ++wrote;
                }
                if (wrote < take) {
                    break;
                }
                left -= take;
            }
        }
        cursor += static_cast<std::size_t>(std::max(left, 0));
    }
    return reader.ok() && cursor <= pixels.size();
}

std::optional<RawFrame> readFrame(Reader& reader, int depth) {
    if (depth > 6) {
        return std::nullopt;
    }
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t transparency = 0;
    std::uint8_t compressed = 0;
    std::uint16_t subs = 0;
    std::uint32_t ignored = 0;
    std::uint32_t pointer = 0;
    if (!reader.u16(width) || !reader.u16(height) || !reader.i16(x) || !reader.i16(y) || !reader.u8(transparency)
        || !reader.u8(compressed) || !reader.u16(subs) || !reader.u32(ignored) || !reader.u32(pointer) || !reader.u32(ignored)) {
        return std::nullopt;
    }
    if (width > 1024 || height > 1024 || subs > 64) {
        return std::nullopt;
    }
    RawFrame frame;
    frame.width = width;
    frame.height = height;
    frame.x = x;
    frame.y = y;
    frame.transparency = transparency;
    if (!reader.seek(pointer)) {
        return std::nullopt;
    }
    if (subs > 0) {
        std::vector<std::uint32_t> pointers(subs);
        for (std::uint16_t i = 0; i < subs; ++i) {
            if (!reader.u32(pointers[i])) {
                return std::nullopt;
            }
        }
        for (std::uint32_t childPointer : pointers) {
            if (!reader.seek(childPointer)) {
                return std::nullopt;
            }
            auto child = readFrame(reader, depth + 1);
            if (!child) {
                return std::nullopt;
            }
            frame.children.push_back(std::move(*child));
        }
        return frame;
    }
    if (width == 0 || height == 0) {
        return frame;
    }
    if (compressed != 0) {
        if (!decompress(reader, width, height, transparency, frame.pixels)) {
            return std::nullopt;
        }
    } else if (!reader.bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), frame.pixels)) {
        return std::nullopt;
    }
    return frame;
}

void blitFrame(RawFrame& dest, const RawFrame& src, int dx, int dy) {
    if (src.pixels.empty()) {
        for (const RawFrame& child : src.children) {
            blitFrame(dest, child, dx + child.x, dy + child.y);
        }
        return;
    }
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const int targetX = dx + x;
            const int targetY = dy + y;
            if (targetX < 0 || targetY < 0 || targetX >= dest.width || targetY >= dest.height) {
                continue;
            }
            const std::uint8_t pixel = src.pixels[static_cast<std::size_t>(y * src.width + x)];
            if (pixel == src.transparency) {
                continue;
            }
            dest.pixels[static_cast<std::size_t>(targetY * dest.width + targetX)] = pixel;
        }
    }
}

GafImage flatten(RawFrame frame) {
    GafImage image;
    image.width = frame.width;
    image.height = frame.height;
    image.hotspotX = frame.x;
    image.hotspotY = frame.y;
    image.transparency = frame.transparency;
    if (!frame.children.empty() && frame.width > 0 && frame.height > 0) {
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height), frame.transparency);
        for (const RawFrame& child : frame.children) {
            blitFrame(frame, child, child.x, child.y);
        }
        frame.children.clear();
    }
    image.pixels = std::move(frame.pixels);
    return image;
}

}  // namespace

std::optional<GafImage> readGafSequence(const std::uint8_t* data, std::size_t size, std::string_view sequence) {
    if (data == nullptr || sequence.empty() || size < 16) {
        return std::nullopt;
    }
    Reader reader(data, size);
    std::uint32_t version = 0;
    std::uint32_t entries = 0;
    std::uint32_t ignored = 0;
    if (!reader.u32(version) || !reader.u32(entries) || !reader.u32(ignored)) {
        return std::nullopt;
    }
    if (version != 0x00010100 || entries == 0 || entries > 10000) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> pointers(entries);
    for (std::uint32_t i = 0; i < entries; ++i) {
        if (!reader.u32(pointers[i])) {
            return std::nullopt;
        }
    }
    for (std::uint32_t pointer : pointers) {
        if (!reader.seek(pointer)) {
            return std::nullopt;
        }
        std::uint16_t frames = 0;
        std::uint16_t unknown = 0;
        std::string name;
        if (!reader.u16(frames) || !reader.u16(unknown) || !reader.u32(ignored) || !reader.name32(name)) {
            return std::nullopt;
        }
        if (frames == 0 || frames > 512 || !sameText(name, sequence)) {
            continue;
        }
        std::uint32_t framePointer = 0;
        std::uint32_t frameUnknown = 0;
        if (!reader.u32(framePointer) || !reader.u32(frameUnknown) || !reader.seek(framePointer)) {
            return std::nullopt;
        }
        auto frame = readFrame(reader, 0);
        if (!frame || frame->width <= 0 || frame->height <= 0) {
            return std::nullopt;
        }
        GafImage image = flatten(std::move(*frame));
        if (image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
            return std::nullopt;
        }
        return image;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> gafImageRgba(const GafImage& image) {
    std::vector<std::uint8_t> rgba(image.pixels.size() * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const std::uint8_t index = image.pixels[i];
        rgba[i * 4] = kTaPalette[index][0];
        rgba[i * 4 + 1] = kTaPalette[index][1];
        rgba[i * 4 + 2] = kTaPalette[index][2];
        rgba[i * 4 + 3] = index == image.transparency ? 0 : 255;
    }
    return rgba;
}

}  // namespace galaxy
