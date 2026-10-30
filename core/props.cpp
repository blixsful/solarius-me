#include "props.hpp"
#include <cctype>
#include <cstdlib>

namespace solme {

void Props::set(const std::string& key, const std::string& value) { values[key] = value; }

bool Props::has(const std::string& key) const { return values.count(key) != 0; }

std::string Props::str(const std::string& key, const std::string& fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

u64 Props::num(const std::string& key, u64 fallback) const {
    auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    u64 out = 0;
    return parseNumber(it->second, out) ? out : fallback;
}

bool Props::flag(const std::string& key, bool fallback) const {
    auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    const std::string& v = it->second;
    if (v == "on" || v == "yes" || v == "true" || v == "1") {
        return true;
    }
    if (v == "off" || v == "no" || v == "false" || v == "0") {
        return false;
    }
    return fallback;
}

bool parseNumber(const std::string& text, u64& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    u64 value = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str()) {
        return false;
    }
    std::string suffix(end);
    if (suffix.empty()) {
    } else if (suffix == "K" || suffix == "k") {
        value <<= 10;
    } else if (suffix == "M" || suffix == "m") {
        value <<= 20;
    } else if (suffix == "G" || suffix == "g") {
        value <<= 30;
    } else {
        return false;
    }
    out = value;
    return true;
}

std::vector<std::string> splitString(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

Spec parseSpec(const std::string& text) {
    Spec spec;
    auto parts = splitString(text, ',');
    size_t first = 0;
    if (!parts.empty() && parts[0].find('=') == std::string::npos) {
        spec.name = parts[0];
        first = 1;
    }
    for (size_t i = first; i < parts.size(); ++i) {
        if (parts[i].empty()) {
            continue;
        }
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) {
            spec.props.set(parts[i], "on");
        } else {
            spec.props.set(parts[i].substr(0, eq), parts[i].substr(eq + 1));
        }
    }
    return spec;
}

}
