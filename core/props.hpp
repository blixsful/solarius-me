#pragma once
#include <map>
#include <string>
#include <vector>
#include "types.hpp"

namespace solme {

class Props {
public:
    void set(const std::string& key, const std::string& value);
    bool has(const std::string& key) const;
    std::string str(const std::string& key, const std::string& fallback = "") const;
    u64 num(const std::string& key, u64 fallback = 0) const;
    bool flag(const std::string& key, bool fallback = false) const;
    const std::map<std::string, std::string>& all() const { return values; }

private:
    std::map<std::string, std::string> values;
};

struct Spec {
    std::string name;
    Props props;
};

bool parseNumber(const std::string& text, u64& out);
Spec parseSpec(const std::string& text);
std::vector<std::string> splitString(const std::string& text, char separator);

}
