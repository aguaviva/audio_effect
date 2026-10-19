#pragma once

#include "module_json.hpp"

#include <string>

// Common graph-module interface. Controllable fields live on the concrete class
// (no nested Params). JSON object assembly stays outside; save/load only emit
// / consume this module's fields.
class Module {
public:
    virtual ~Module() = default;

    virtual std::string name() const = 0;
    virtual void save(std::string& out, bool& first) const = 0;
    virtual bool load(const std::string& key, double num, bool flag, const std::string& str,
                      int kind) = 0;
    // ImGui controls for this module. Returns true if any field changed.
    virtual bool render() = 0;
};
