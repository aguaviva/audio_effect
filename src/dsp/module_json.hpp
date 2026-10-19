#pragma once

#include <cctype>
#include <string>

// Shared JSON helpers used inside Module::save / Module::load.
namespace module_json {

inline void append_comma(std::string& out) {
    if (out.empty()) {
        return;
    }
    const char last = out.back();
    if (last != '{' && last != '[' && last != ',') {
        out.push_back(',');
    }
}

inline void write_bool(std::string& out, const char* key, bool value) {
    append_comma(out);
    out += '"';
    out += key;
    out += "\":";
    out += value ? "true" : "false";
}

inline void write_float(std::string& out, const char* key, float value) {
    append_comma(out);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

inline void write_int(std::string& out, const char* key, int value) {
    append_comma(out);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

inline void write_string(std::string& out, const char* key, const std::string& value) {
    append_comma(out);
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

inline bool parse_bool_value(const std::string& text, size_t& i, bool& out) {
    if (!skip_ws(text, i)) {
        return false;
    }
    if (text.compare(i, 4, "true") == 0) {
        i += 4;
        out = true;
        return true;
    }
    if (text.compare(i, 5, "false") == 0) {
        i += 5;
        out = false;
        return true;
    }
    return false;
}

// Skip one JSON value (scalar, array, or object) starting at i.
inline bool skip_value(const std::string& text, size_t& i) {
    if (!skip_ws(text, i)) {
        return false;
    }
    const char c = text[i];
    if (c == '"') {
        std::string tmp;
        return parse_string_key(text, i, tmp);
    }
    if (c == 't' || c == 'f') {
        bool flag = false;
        return parse_bool_value(text, i, flag);
    }
    if (c == 'n' && text.compare(i, 4, "null") == 0) {
        i += 4;
        return true;
    }
    if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
        double num = 0.0;
        return parse_number_value(text, i, num);
    }
    if (c == '{' || c == '[') {
        const char open = c;
        const char close = (c == '{') ? '}' : ']';
        ++i;
        int depth = 1;
        bool in_string = false;
        bool escape = false;
        while (i < text.size() && depth > 0) {
            const char ch = text[i++];
            if (in_string) {
                if (escape) {
                    escape = false;
                } else if (ch == '\\') {
                    escape = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
            } else if (ch == open) {
                ++depth;
            } else if (ch == close) {
                --depth;
            }
        }
        return depth == 0;
    }
    return false;
}

// Walk members of a JSON object. Fn(key, text, i) must consume the value at i.
template <typename Fn>
inline bool for_each_member(const std::string& text, Fn&& fn) {
    size_t i = 0;
    if (!expect_char(text, i, '{')) {
        return false;
    }
    while (true) {
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '}') {
            return true;
        }
        std::string key;
        if (!parse_string_key(text, i, key) || !expect_char(text, i, ':')) {
            return false;
        }
        if (!fn(key, text, i)) {
            return false;
        }
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == ',') {
            ++i;
            continue;
        }
        if (text[i] == '}') {
            return true;
        }
        return false;
    }
}

// Append members from a JSON object (without its braces) into out.
inline void append_object_members(std::string& out, const std::string& object) {
    size_t i = 0;
    if (!expect_char(object, i, '{')) {
        return;
    }
    if (!skip_ws(object, i) || object[i] == '}') {
        return;
    }
    const size_t begin = i;
    size_t end = object.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(object[end - 1]))) {
        --end;
    }
    if (end > begin && object[end - 1] == '}') {
        --end;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(object[end - 1]))) {
        --end;
    }
    if (end <= begin) {
        return;
    }
    append_comma(out);
    out.append(object, begin, end - begin);
}

} // namespace module_json
