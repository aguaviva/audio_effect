#pragma once

#include "dsp/module.hpp"

#include <memory>
#include <string>
#include <vector>

// Every node kind the editor can create, in add-menu order. A node's saved "type" is its
// module's name().
struct NodeKind {
    const char* label;
    const char* category;
    std::shared_ptr<Module> (*make)();
};

const std::vector<NodeKind>& node_kinds();

// nullptr when no kind has a module with this name.
std::shared_ptr<Module> make_module(const std::string& name);

const char* node_label(const Module& module);
