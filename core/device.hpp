#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "props.hpp"

namespace solme {

class Machine;
struct Options;

class Device {
public:
    virtual ~Device() = default;
    virtual void reset() {}
    virtual void shutdown() {}
};

class DeviceRegistry {
public:
    using Factory = std::function<std::unique_ptr<Device>(Machine&, const Props&)>;

    static DeviceRegistry& instance() {
        static DeviceRegistry registry;
        return registry;
    }

    bool add(const std::string& name, Factory factory) {
        factories[name] = std::move(factory);
        return true;
    }

    std::unique_ptr<Device> create(const std::string& name, Machine& machine, const Props& props) const {
        auto it = factories.find(name);
        return it == factories.end() ? nullptr : it->second(machine, props);
    }

    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (const auto& entry : factories) {
            out.push_back(entry.first);
        }
        return out;
    }

private:
    std::map<std::string, Factory> factories;
};

struct MachineType {
    std::string name;
    std::string arch;
    std::string description;
    bool isDefault = false;
    std::function<void(Machine&, const Options&)> build;
};

class MachineRegistry {
public:
    static MachineRegistry& instance() {
        static MachineRegistry registry;
        return registry;
    }

    bool add(MachineType type) {
        types.push_back(std::move(type));
        return true;
    }

    const MachineType* find(const std::string& arch, const std::string& name) const {
        for (const auto& type : types) {
            if (type.arch == arch && (name.empty() ? type.isDefault : type.name == name)) {
                return &type;
            }
        }
        return nullptr;
    }

    const std::vector<MachineType>& all() const { return types; }

private:
    std::vector<MachineType> types;
};

}

#define SOLME_CONCAT_INNER(a, b) a##b
#define SOLME_CONCAT(a, b) SOLME_CONCAT_INNER(a, b)

#define SOLME_REGISTER_DEVICE(deviceName, ClassName)                                              \
    static bool SOLME_CONCAT(solmeDeviceRegistered_, __LINE__) =                                  \
        ::solme::DeviceRegistry::instance().add(                                                  \
            deviceName, [](::solme::Machine& machine, const ::solme::Props& props) {              \
                return std::unique_ptr<::solme::Device>(new ClassName(machine, props));           \
            })

#define SOLME_REGISTER_MACHINE(arch, name, description, isDefault, builder)                       \
    static bool SOLME_CONCAT(solmeMachineRegistered_, __LINE__) =                                 \
        ::solme::MachineRegistry::instance().add(                                                 \
            ::solme::MachineType{name, arch, description, isDefault, builder})
