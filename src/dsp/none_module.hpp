#pragma once

#include "module.hpp"

#include <string>

// Placeholder module for nodes without a concrete type.
class NoneModule : public Module {
public:
    std::string name() const override { return "none"; }
    Role role() const override { return Role::effect; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(ProcessContext& /*ctx*/) override {}
};
