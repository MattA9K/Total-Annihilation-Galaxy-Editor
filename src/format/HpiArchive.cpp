#include "format/HpiArchive.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace galaxy {
namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Could not open " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 20) {
        throw std::runtime_error(path.string() + " is too small to be an HPI archive");
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in) {
        throw std::runtime_error("Could not read " + path.string());
    }
    return data;
}

std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string readCString(const std::vector<std::uint8_t>& data, std::uint32_t offset) {
    require(offset < data.size(), "HPI name points past the directory");
    std::size_t end = offset;
    while (end < data.size() && data[end] != 0) {
        ++end;
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
}

void decryptWithKey(std::uint8_t key, std::uint8_t seed, std::uint8_t* data, std::size_t size) {
    if (key == 0) {
        return;
    }
    for (std::size_t i = 0; i < size; ++i) {
        const auto pos = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
        data[i] = static_cast<std::uint8_t>((pos ^ key) ^ data[i]);
    }
}

void decryptChunk(std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto pos = static_cast<std::uint8_t>(i);
        data[i] = static_cast<std::uint8_t>((data[i] - pos) ^ pos);
    }
}

std::vector<std::uint8_t> inflateZlib(const std::uint8_t* data, std::size_t size, std::uint32_t expected) {
    std::vector<std::uint8_t> out(expected);
    uLongf destLen = expected;
    const int rc = uncompress(out.data(), &destLen, data, static_cast<uLong>(size));
    require(rc == Z_OK, "HPI zlib chunk failed to decompress");
    out.resize(destLen);
    return out;
}

std::vector<std::uint8_t> inflateLz77(const std::uint8_t* data, std::size_t size, std::uint32_t expected) {
    std::uint8_t window[4096] = {};
    int windowPos = 1;
    std::vector<std::uint8_t> out;
    out.reserve(expected);
    std::size_t cursor = 0;

    auto readByte = [&]() -> std::uint8_t {
        require(cursor < size, "HPI LZ77 chunk ended early");
        return data[cursor++];
    };

    while (out.size() < expected && cursor < size) {
        std::uint8_t tag = readByte();
        for (int bit = 0; bit < 8 && out.size() < expected; ++bit) {
            if ((tag & 1) == 0) {
                const std::uint8_t value = readByte();
                out.push_back(value);
                window[windowPos] = value;
                windowPos = (windowPos + 1) & 0xFFF;
            } else {
                require(cursor + 2 <= size, "HPI LZ77 chunk ended early");
                const std::uint16_t packed = readU16(data + cursor);
                cursor += 2;
                int windowRead = packed >> 4;
                if (windowRead == 0) {
                    return out;
                }
                const int count = (packed & 0x0F) + 2;
                for (int i = 0; i < count && out.size() < expected; ++i) {
                    const std::uint8_t value = window[windowRead];
                    out.push_back(value);
                    window[windowPos] = value;
                    windowRead = (windowRead + 1) & 0xFFF;
                    windowPos = (windowPos + 1) & 0xFFF;
                }
            }
            tag = static_cast<std::uint8_t>(tag >> 1);
        }
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

}  // namespace

HpiArchive HpiArchive::open(const std::filesystem::path& path) {
    HpiArchive archive;
    archive.path_ = path;
    archive.data_ = readFile(path);
    const auto& data = archive.data_;

    require(data.size() >= 20, path.string() + " is too small to be an HPI archive");
    require(std::memcmp(data.data(), "HAPI", 4) == 0, path.string() + " is not an HPI/UFO archive");
    const std::uint32_t version = readU32(data.data() + 4);
    require(version == 0x00010000, path.string() + " uses an unsupported HPI version");

    const std::uint32_t directorySize = readU32(data.data() + 8);
    const std::uint32_t headerKey = readU32(data.data() + 12);
    const std::uint32_t start = readU32(data.data() + 16);
    archive.key_ = static_cast<std::uint8_t>(((headerKey & 0xFF) << 2) | ((headerKey & 0xFF) >> 6));

    require(directorySize >= start && directorySize <= data.size(), "HPI directory is out of range");
    std::vector<std::uint8_t> directory(directorySize);
    const std::size_t stored = directorySize - start;
    std::memcpy(directory.data() + start, data.data() + start, stored);
    decryptWithKey(archive.key_, static_cast<std::uint8_t>(start), directory.data() + start, stored);

    const auto walk = [&](auto&& self, std::uint32_t dirOff, const std::string& prefix) -> void {
        require(static_cast<std::size_t>(dirOff) + 8 <= directory.size(), "HPI directory entry is out of range");
        const std::uint32_t count = readU32(directory.data() + dirOff);
        const std::uint32_t list = readU32(directory.data() + dirOff + 4);
        require(count < 1'000'000, "HPI directory is unreasonably large");
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = list + i * 9;
            require(static_cast<std::size_t>(entry) + 9 <= directory.size(), "HPI directory entry is out of range");
            const std::uint32_t nameOff = readU32(directory.data() + entry);
            const std::uint32_t dataOff = readU32(directory.data() + entry + 4);
            const std::uint8_t isDir = directory[entry + 8];
            const std::string name = readCString(directory, nameOff);
            const std::string full = prefix.empty() ? name : prefix + "/" + name;
            if (isDir != 0) {
                self(self, dataOff, full);
                continue;
            }
            require(static_cast<std::size_t>(dataOff) + 9 <= directory.size(), "HPI file record is out of range");
            HpiFile file;
            file.path = full;
            file.offset = readU32(directory.data() + dataOff);
            file.size = readU32(directory.data() + dataOff + 4);
            file.compression = directory[dataOff + 8];
            archive.files_.push_back(std::move(file));
        }
    };
    walk(walk, start, "");
    return archive;
}

const HpiFile* HpiArchive::find(std::string_view name) const {
    std::string normalized(name);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    for (const HpiFile& file : files_) {
        if (iequals(file.path, normalized)) {
            return &file;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> HpiArchive::extract(const HpiFile& file) const {
    if (file.compression == 0) {
        require(static_cast<std::size_t>(file.offset) + file.size <= data_.size(), "HPI file extends past the archive");
        std::vector<std::uint8_t> out(
            data_.begin() + file.offset,
            data_.begin() + file.offset + file.size);
        decryptWithKey(key_, static_cast<std::uint8_t>(file.offset), out.data(), out.size());
        return out;
    }

    const std::uint32_t chunkCount = file.size / 65536 + (file.size % 65536 == 0 ? 0 : 1);
    std::size_t cursor = static_cast<std::size_t>(file.offset) + static_cast<std::size_t>(chunkCount) * 4;
    std::vector<std::uint8_t> out;
    out.reserve(file.size);

    for (std::uint32_t i = 0; i < chunkCount; ++i) {
        require(cursor + 19 <= data_.size(), "HPI chunk header extends past the archive");
        std::uint8_t header[19];
        std::memcpy(header, data_.data() + cursor, 19);
        decryptWithKey(key_, static_cast<std::uint8_t>(cursor), header, 19);
        cursor += 19;

        const std::uint32_t marker = readU32(header);
        require(marker == 0x48535153, "HPI chunk is missing the SQSH marker");
        const std::uint8_t scheme = header[5];
        const std::uint8_t encrypted = header[6];
        const std::uint32_t compressedSize = readU32(header + 7);
        const std::uint32_t decompressedSize = readU32(header + 11);
        const std::uint32_t checksum = readU32(header + 15);

        require(cursor + compressedSize <= data_.size(), "HPI chunk extends past the archive");
        std::vector<std::uint8_t> chunk(data_.begin() + static_cast<std::ptrdiff_t>(cursor),
            data_.begin() + static_cast<std::ptrdiff_t>(cursor + compressedSize));
        decryptWithKey(key_, static_cast<std::uint8_t>(cursor), chunk.data(), chunk.size());
        cursor += compressedSize;

        std::uint32_t sum = 0;
        for (std::uint8_t byte : chunk) {
            sum += byte;
        }
        require(sum == checksum, "HPI chunk checksum does not match");

        if (encrypted != 0) {
            decryptChunk(chunk.data(), chunk.size());
        }

        std::vector<std::uint8_t> piece;
        if (scheme == 0) {
            piece = std::move(chunk);
        } else if (scheme == 1) {
            piece = inflateLz77(chunk.data(), chunk.size(), decompressedSize);
        } else if (scheme == 2) {
            piece = inflateZlib(chunk.data(), chunk.size(), decompressedSize);
        } else {
            throw std::runtime_error("HPI chunk uses an unknown compression scheme");
        }
        out.insert(out.end(), piece.begin(), piece.end());
    }
    return out;
}

namespace {

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void patchU32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value);
    out[at + 1] = static_cast<std::uint8_t>(value >> 8);
    out[at + 2] = static_cast<std::uint8_t>(value >> 16);
    out[at + 3] = static_cast<std::uint8_t>(value >> 24);
}

void encryptChunk(std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto pos = static_cast<std::uint8_t>(i);
        data[i] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(data[i] ^ pos) + pos);
    }
}

std::vector<std::uint8_t> zlibCompress(const std::uint8_t* data, std::size_t size) {
    uLongf destLen = compressBound(static_cast<uLong>(size));
    std::vector<std::uint8_t> out(destLen);
    const int rc = compress2(out.data(), &destLen, data, static_cast<uLong>(size), Z_DEFAULT_COMPRESSION);
    require(rc == Z_OK, "Could not compress an HPI chunk");
    out.resize(destLen);
    return out;
}

// Chunk table entries are the on-disk size of each SQSH block, header included.
std::vector<std::uint8_t> packCompressed(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kPiece = 65536;
    const std::size_t chunkCount = (bytes.size() + kPiece - 1) / kPiece;
    std::vector<std::uint8_t> packed;
    packed.reserve(chunkCount * 128);
    std::vector<std::uint32_t> chunkSizes;
    chunkSizes.reserve(chunkCount);
    std::vector<std::uint8_t> blocks;

    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const std::size_t offset = chunk * kPiece;
        const std::size_t piece = std::min(kPiece, bytes.size() - offset);
        auto compressed = zlibCompress(bytes.data() + offset, piece);
        encryptChunk(compressed.data(), compressed.size());
        std::uint32_t checksum = 0;
        for (std::uint8_t byte : compressed) {
            checksum += byte;
        }

        const auto compressedSize = static_cast<std::uint32_t>(compressed.size());
        chunkSizes.push_back(19 + compressedSize);
        appendU32(blocks, 0x48535153);
        blocks.push_back(0x02);
        blocks.push_back(0x02);
        blocks.push_back(0x01);
        appendU32(blocks, compressedSize);
        appendU32(blocks, static_cast<std::uint32_t>(piece));
        appendU32(blocks, checksum);
        blocks.insert(blocks.end(), compressed.begin(), compressed.end());
    }

    for (std::uint32_t size : chunkSizes) {
        appendU32(packed, size);
    }
    packed.insert(packed.end(), blocks.begin(), blocks.end());
    return packed;
}

struct ArchiveNode {
    std::string name;
    bool directory = false;
    std::vector<std::uint8_t> contents;
    std::vector<ArchiveNode> children;
};

void addArchiveFile(ArchiveNode& root, std::string path, std::vector<std::uint8_t> bytes) {
    std::replace(path.begin(), path.end(), '\\', '/');
    ArchiveNode* node = &root;
    std::size_t start = 0;
    while (start < path.size()) {
        const auto slash = path.find('/', start);
        const auto part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        const bool last = slash == std::string::npos;
        if (part.empty()) {
            if (last) {
                break;
            }
            start = slash + 1;
            continue;
        }
        if (last) {
            ArchiveNode file;
            file.name = part;
            file.contents = std::move(bytes);
            node->children.push_back(std::move(file));
            return;
        }
        ArchiveNode* child = nullptr;
        for (ArchiveNode& existing : node->children) {
            if (existing.directory && existing.name == part) {
                child = &existing;
            }
        }
        if (child == nullptr) {
            ArchiveNode directory;
            directory.name = part;
            directory.directory = true;
            node->children.push_back(std::move(directory));
            child = &node->children.back();
        }
        node = child;
        start = slash + 1;
    }
}

void commitBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const auto parent = path.parent_path();
    const auto temp = (parent.empty() ? std::filesystem::path() : parent) / (path.filename().string() + ".galaxy-tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(out), "Could not write " + temp.string());
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(static_cast<bool>(out), "Could not write " + temp.string());
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        std::filesystem::rename(temp, path);
    }
}

}  // namespace

void writeHpiArchive(const std::filesystem::path& path, std::vector<HpiWriteFile> files) {
    require(!files.empty(), "An HPI archive needs at least one file");
    ArchiveNode root;
    root.directory = true;
    for (HpiWriteFile& file : files) {
        require(!file.path.empty(), "An HPI entry needs a name");
        addArchiveFile(root, std::move(file.path), std::move(file.bytes));
    }

    std::vector<std::uint8_t> buffer(20, 0);
    struct PendingFile {
        std::size_t offsetAt = 0;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<PendingFile> pending;

    const auto emitDir = [&](auto&& self, ArchiveNode& node) -> std::uint32_t {
        const auto at = static_cast<std::uint32_t>(buffer.size());
        const auto count = static_cast<std::uint32_t>(node.children.size());
        require(count > 0, "An HPI directory is empty");
        appendU32(buffer, count);
        const std::size_t listField = buffer.size();
        appendU32(buffer, 0);
        const auto list = static_cast<std::uint32_t>(buffer.size());
        buffer.resize(buffer.size() + static_cast<std::size_t>(count) * 9, 0);
        patchU32(buffer, listField, list);

        for (std::uint32_t i = 0; i < count; ++i) {
            ArchiveNode& child = node.children[i];
            const auto nameOff = static_cast<std::uint32_t>(buffer.size());
            buffer.insert(buffer.end(), child.name.begin(), child.name.end());
            buffer.push_back(0);

            std::uint32_t dataOff = 0;
            std::uint8_t flag = 0;
            if (child.directory) {
                dataOff = self(self, child);
                flag = 1;
            } else {
                dataOff = static_cast<std::uint32_t>(buffer.size());
                PendingFile stored;
                stored.offsetAt = buffer.size();
                stored.bytes = std::move(child.contents);
                appendU32(buffer, 0);
                appendU32(buffer, static_cast<std::uint32_t>(stored.bytes.size()));
                buffer.push_back(stored.bytes.empty() ? 0 : 2);
                pending.push_back(std::move(stored));
            }

            const std::size_t entry = static_cast<std::size_t>(list) + static_cast<std::size_t>(i) * 9;
            patchU32(buffer, entry, nameOff);
            patchU32(buffer, entry + 4, dataOff);
            buffer[entry + 8] = flag;
        }
        return at;
    };
    emitDir(emitDir, root);

    const auto directorySize = static_cast<std::uint32_t>(buffer.size());
    for (PendingFile& file : pending) {
        if (file.bytes.empty()) {
            continue;
        }
        auto packed = packCompressed(file.bytes);
        patchU32(buffer, file.offsetAt, static_cast<std::uint32_t>(buffer.size()));
        buffer.insert(buffer.end(), packed.begin(), packed.end());
    }

    std::memcpy(buffer.data(), "HAPI", 4);
    patchU32(buffer, 4, 0x00010000);
    patchU32(buffer, 8, directorySize);
    patchU32(buffer, 12, 0);
    patchU32(buffer, 16, 20);
    // Total Annihilation mounts an archive only when the last 36 bytes match
    // "Copyright 0000 Cavedog Entertainment", with any four-digit year accepted.
    static constexpr char kCopyright[] = "Copyright 2026 Cavedog Entertainment";
    static_assert(sizeof(kCopyright) - 1 == 36, "HPI copyright trailer must be 36 bytes");
    buffer.insert(buffer.end(), kCopyright, kCopyright + 36);
    commitBytes(path, buffer);
}

}  // namespace galaxy
