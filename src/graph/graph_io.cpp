#include "graph_io.hpp"

#include "dsp/module_json.hpp"
#include "node_registry.hpp"
#include "pins.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace graph_io {
namespace {

constexpr int kFileVersion = 2;

std::string node_json(const GraphNode& node) {
    std::string params = "{";
    module_json::write_bool(params, "bypassed", node.bypassed);
    module_json::append_object_members(params, node.module->save());
    params += '}';

    std::string out = "{";
    module_json::write_int(out, "id", node.id);
    module_json::write_string(out, "type", node.module->name());
    module_json::write_float(out, "x", node.x);
    module_json::write_float(out, "y", node.y);
    out += ",\"params\":" + params + '}';
    return out;
}

std::string link_json(const GraphLink& link) {
    std::string out = "{";
    module_json::write_int(out, "id", link.id);
    module_json::write_int(out, "from", link.from_node);
    module_json::write_int(out, "to", link.to_node);
    module_json::write_int(out, "to_slot", link.to_slot);
    out += '}';
    return out;
}

bool parse_node(const std::string& json, GraphNode& node, std::string& error) {
    std::string type;
    std::string params = "{}";
    const bool ok = module_json::for_each_member(
        json, [&](const std::string& key, const std::string& text, size_t& i) {
            if (key == "id") return module_json::parse_value(text, i, node.id);
            if (key == "x") return module_json::parse_value(text, i, node.x);
            if (key == "y") return module_json::parse_value(text, i, node.y);
            if (key == "type") return module_json::parse_value(text, i, type);
            if (key == "params") return module_json::take_value(text, i, params);
            error = "Unknown node field: " + key;
            return false;
        });
    if (!ok) {
        if (error.empty()) error = "Invalid node object";
        return false;
    }
    if (node.id <= 0) {
        error = "Node missing id";
        return false;
    }
    node.module = make_module(type);
    if (!node.module) {
        error = "Unknown module name '" + type + "' on node " + std::to_string(node.id);
        return false;
    }
    module_json::read(params, "bypassed", node.bypassed);
    if (!node.module->load(params)) {
        error = "Invalid params on node " + std::to_string(node.id);
        return false;
    }
    return true;
}

bool parse_link(const std::string& json, GraphLink& link, std::string& error) {
    const bool ok = module_json::for_each_member(
        json, [&](const std::string& key, const std::string& text, size_t& i) {
            if (key == "id") return module_json::parse_value(text, i, link.id);
            if (key == "from") return module_json::parse_value(text, i, link.from_node);
            if (key == "to") return module_json::parse_value(text, i, link.to_node);
            if (key == "to_slot") return module_json::parse_value(text, i, link.to_slot);
            error = "Unknown link field: " + key;
            return false;
        });
    if (!ok) {
        if (error.empty()) error = "Invalid link object";
        return false;
    }
    if (link.id <= 0) {
        error = "Link missing id";
        return false;
    }
    return true;
}

} // namespace

std::string serialize(const AudioGraph& graph) {
    std::string out = "{\n";
    out += "  \"version\":" + std::to_string(kFileVersion) + ",\n";
    out += "  \"nodes\":[\n";
    const auto& nodes = graph.nodes();
    for (size_t n = 0; n < nodes.size(); ++n) {
        out += "    " + node_json(nodes[n]) + (n + 1 < nodes.size() ? ",\n" : "\n");
    }
    out += "  ],\n  \"links\":[\n";
    const auto& links = graph.links();
    for (size_t n = 0; n < links.size(); ++n) {
        out += "    " + link_json(links[n]) + (n + 1 < links.size() ? ",\n" : "\n");
    }
    out += "  ]\n}\n";
    return out;
}

bool deserialize(AudioGraph& graph, const std::string& text, std::string& error) {
    error.clear();
    int version = 0;
    module_json::read(text, "version", version);
    if (version != kFileVersion) {
        error = "Unsupported patch version " + std::to_string(version) + " (expected " +
                std::to_string(kFileVersion) + ")";
        return false;
    }

    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    std::unordered_set<int> node_ids;
    std::unordered_set<int> link_ids;
    bool have_nodes = false;

    const bool ok = module_json::for_each_member(
        text, [&](const std::string& key, const std::string& body, size_t& i) {
            if (key == "version") return module_json::skip_value(body, i);
            if (key == "nodes") {
                have_nodes = true;
                return module_json::for_each_element(body, i, [&](const std::string& json) {
                    GraphNode node;
                    if (!parse_node(json, node, error)) return false;
                    if (!node_ids.insert(node.id).second) {
                        error = "Duplicate node id " + std::to_string(node.id);
                        return false;
                    }
                    nodes.push_back(std::move(node));
                    return true;
                });
            }
            if (key == "links") {
                return module_json::for_each_element(body, i, [&](const std::string& json) {
                    GraphLink link;
                    if (!parse_link(json, link, error)) return false;
                    if (!link_ids.insert(link.id).second) {
                        error = "Duplicate link id " + std::to_string(link.id);
                        return false;
                    }
                    links.push_back(link);
                    return true;
                });
            }
            error = "Unknown top-level key: " + key;
            return false;
        });
    if (!ok) {
        if (error.empty()) error = "Malformed graph file";
        return false;
    }
    if (!have_nodes) {
        error = "Graph file missing nodes";
        return false;
    }

    auto find = [&](int id) -> const Module* {
        for (const auto& node : nodes) {
            if (node.id == id) return node.module.get();
        }
        return nullptr;
    };
    // Drop links to missing nodes or pins; a second link into the same pin is an error
    // because every input pin reads exactly one link buffer.
    std::vector<GraphLink> valid_links;
    std::unordered_set<int> used_pins;
    for (const auto& link : links) {
        const Module* from = find(link.from_node);
        const Module* to = find(link.to_node);
        if (!from || from->output_count() == 0 || !to || link.to_slot < 0 ||
            link.to_slot >= to->input_count()) {
            continue;
        }
        if (!used_pins.insert(input_pin_id(link.to_node, link.to_slot)).second) {
            error = "Link " + std::to_string(link.id) + " connects to an input pin that is "
                    "already connected";
            return false;
        }
        valid_links.push_back(link);
    }

    AudioGraph loaded;
    loaded.nodes() = std::move(nodes);
    loaded.links() = std::move(valid_links);
    if (loaded.has_cycle()) {
        error = "The graph contains a cycle: a signal feeds back into itself.";
        return false;
    }
    graph.nodes() = std::move(loaded.nodes());
    graph.links() = std::move(loaded.links());
    return true;
}

bool save_to_file(const AudioGraph& graph, const std::string& path, std::string& error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open file for writing: " + path;
        return false;
    }
    file << serialize(graph);
    if (!file) {
        error = "Failed while writing: " + path;
        return false;
    }
    return true;
}

bool load_from_file(AudioGraph& graph, const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open file for reading: " + path;
        return false;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return deserialize(graph, oss.str(), error);
}

} // namespace graph_io
