#pragma once
#include <string>
#include <vector>
#include "props.hpp"
#include "types.hpp"

namespace solme {

struct LoadSpec {
    std::string path;
    std::string space;
    Addr address = 0;
};

struct TypeSpec {
    u64 atMs = 0;
    std::string text;
};

struct Options {
    Props opts;
    std::vector<Spec> drives;
    std::vector<Spec> devices;
    std::vector<LoadSpec> loads;
    std::vector<TypeSpec> typed;
};

}
