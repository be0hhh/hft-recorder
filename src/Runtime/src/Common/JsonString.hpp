#pragma once

#include <string>
#include <string_view>

namespace hftrec::json {

inline void appendEscaped(std::string& out, std::string_view value) {
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20u) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(ch) >> 4) & 0x0f];
                    out += kHex[static_cast<unsigned char>(ch) & 0x0f];
                } else {
                    out += ch;
                }
                break;
        }
    }
}

inline std::string quote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2u);
    out.push_back('"');
    appendEscaped(out, value);
    out.push_back('"');
    return out;
}

}  // namespace hftrec::json
