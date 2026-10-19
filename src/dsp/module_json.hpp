#pragma once

#include <cctype>
#include <string>

// Shared JSON field helpers for Module::save / Module::load.
namespace module_json {

inline void append_comma(std::string& out, bool& first) {
    if (!first) {
        out.push_back(',');
    }
    first = false;
}

inline void write_bool(std::string& out, bool& first, const char* key, bool value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += value ? "true" : "false";
}

inline void write_float(std::string& out, bool& first, const char* key, float value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

inline void write_int(std::string& out, bool& first, const char* key, int value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

inline void write_string(std::string& out, bool& first, const char* key, const std::string& value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":\"";
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out += '"';
}

constexpr int kKindNumber = 0;
constexpr int kKindBool = 1;
constexpr int kKindString = 2;

inline bool skip_ws(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    return i < text.size();
}

inline bool expect_char(const std::string& text, size_t& i, char ch) {
    if (!skip_ws(text, i) || text[i] != ch) {
        return false;
    }
    ++i;
    return true;
}

inline bool parse_string_key(const std::string& text, size_t& i, std::string& key) {
    if (!expect_char(text, i, '"')) {
        return false;
    }
    key.clear();
    while (i < text.size()) {
        char c = text[i++];
        if (c == '\\' && i < text.size()) {
            key.push_back(text[i++]);
            continue;
        }
        if (c == '"') {
            return true;
        }
        key.push_back(c);
    }
    return false;
}

inline bool parse_number_value(const std::string& text, size_t& i, double& out) {
    if (!skip_ws(text, i)) {
        return false;
    }
    size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        ++i;
    }
    while (i < text.size()) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E') {
            ++i;
            continue;
        }
        if ((c == '+' || c == '-') && i > start && (text[i - 1] == 'e' || text[i - 1] == 'E')) {
            ++i;
            continue;
        }
        break;
    }
    if (start == i) {
        return false;
    }
    try {
        out = std::stod(text.substr(start, i - start));
    } catch (...) {
        return false;
    }
    return true;
}

template <typename T>
inline bool parse_object(const std::string& text, size_t& i, T& module) {
    if (!expect_char(text, i, '{')) {
        return false;
    }
    while (true) {
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '}') {
            ++i;
            return true;
        }
        std::string key;
        if (!parse_string_key(text, i, key) || !expect_char(text, i, ':')) {
            return false;
        }
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '"') {
            std::string str;
            if (!parse_string_key(text, i, str)) {
                return false;
            }
            module.load(key, 0.0, false, str, kKindString);
        } else if (text.compare(i, 4, "true") == 0 || text.compare(i, 5, "false") == 0) {
            const bool flag = text.compare(i, 4, "true") == 0;
            i += flag ? 4 : 5;
            module.load(key, 0.0, flag, std::string{}, kKindBool);
        } else {
            double num = 0.0;
            if (!parse_number_value(text, i, num)) {
                return false;
            }
            module.load(key, num, false, std::string{}, kKindNumber);
        }
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == ',') {
            ++i;
            continue;
        }
        if (text[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
}

template <typename T>
inline std::string save_object(const T& module) {
    std::string out = "{";
    bool first = true;
    module.save(out, first);
    out += '}';
    return out;
}

} // namespace module_json
