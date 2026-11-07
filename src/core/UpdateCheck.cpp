#include "core/UpdateCheck.hpp"

namespace galaxy {
namespace {

std::string jsonString(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char character : text) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[character >> 4]);
                out.push_back(hex[character & 0x0f]);
            } else {
                out.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return out;
}

}  // namespace

std::string updateCheckBody(std::string_view version, std::string_view platform, std::string_view operatingSystem, bool later) {
    std::string body = "{\"current_version\":\"";
    body += jsonString(version);
    body += "\",\"is_the_second_time_checked\":";
    body += later ? "true" : "false";
    body += ",\"platform\":\"";
    body += jsonString(platform);
    body += "\",\"operating_system\":\"";
    body += jsonString(operatingSystem);
    body += "\"}";
    return body;
}

}  // namespace galaxy
