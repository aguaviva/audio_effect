#include "audio_graph.hpp"

#include "dsp/mixer.hpp"
#include "node_registry.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

template <typename T>
static int next_free_id(const std::vector<T>& items) {
    int max_id = 0;
    for (const auto& item : items) {
        max_id = std::max(max_id, item.id);
    }
    return max_id + 1;
}

int AudioGraph::add_node(const std::string& type, float x, float y) {
    auto module = make_module(type);
    if (!module) {
        return -1;
    }
    // Every module in a graph has been through load(): from its patch file, or from its
    // defaults here (a WAV Player decodes its default file).
    module->load("{}");
    const int id = next_free_id(nodes_);
    nodes_.push_back({id, x, y, false, std::move(module)});
    return id;
}

bool AudioGraph::remove_node(int node_id) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                           [node_id](const GraphNode& n) { return n.id == node_id; });
    if (it == nodes_.end()) {
        return false;
    }
    nodes_.erase(it);
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [node_id](const GraphLink& l) {
                                    return l.from_node == node_id || l.to_node == node_id;
                                }),
                 links_.end());
    return true;
}

bool AudioGraph::add_link(int from_node, int from_slot, int to_node, int to_slot, std::string* why) {
    auto refuse = [why](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    const GraphNode* from = find_node(from_node);
    const GraphNode* to = find_node(to_node);
    if (!from || !to || from_slot < 0 || from_slot >= from->module->output_count() ||
        to_slot < 0 || to_slot >= input_pin_count(to_node)) {
        return refuse("Connect an output pin to an input pin.");
    }
    std::unordered_set<int> visited;
    std::vector<int> stack{to_node};
    while (!stack.empty()) {
        int current = stack.back();
        stack.pop_back();
        if (current == from_node) {
            return refuse("That link would create a cycle: the signal would feed back into itself.");
        }
        if (!visited.insert(current).second) {
            continue;
        }
        for (const auto& link : links_) {
            if (link.from_node == current) {
                stack.push_back(link.to_node);
            }
        }
    }

    // One link per input pin; fan-in goes through a Mixer.
    for (const auto& link : links_) {
        if (link.to_node == to_node && link.to_slot == to_slot) {
            return refuse("That input is already connected. Combine signals with a Mixer.");
        }
    }

    links_.push_back({next_free_id(links_), from_node, from_slot, to_node, to_slot});
    return true;
}

int AudioGraph::input_pin_count(int node_id) const {
    const GraphNode* node = find_node(node_id);
    if (!node) {
        return 0;
    }
    if (!node->as<Mixer>()) {
        return node->module->input_count();
    }
    int max_slot = -1;
    int connected = 0;
    for (const auto& link : links_) {
        if (link.to_node != node_id) {
            continue;
        }
        ++connected;
        max_slot = std::max(max_slot, link.to_slot);
    }
    // Auto-detect: show every used slot, plus one empty pin for the next wire.
    const int needed = std::max(max_slot + 1, connected) + 1;
    return std::clamp(needed, 1, node->module->input_count());
}

bool AudioGraph::remove_link(int link_id) {
    auto it = std::find_if(links_.begin(), links_.end(),
                           [link_id](const GraphLink& l) { return l.id == link_id; });
    if (it == links_.end()) {
        return false;
    }
    links_.erase(it);
    return true;
}

GraphNode* AudioGraph::find_node(int node_id) {
    for (auto& node : nodes_) {
        if (node.id == node_id) {
            return &node;
        }
    }
    return nullptr;
}

const GraphNode* AudioGraph::find_node(int node_id) const {
    for (const auto& node : nodes_) {
        if (node.id == node_id) {
            return &node;
        }
    }
    return nullptr;
}

void AudioGraph::clear() {
    nodes_.clear();
    links_.clear();
}

// Links are valid here: add_link() and graph_io check endpoints, pins and cycles.
std::shared_ptr<const CompiledGraph> AudioGraph::compile() const {
    auto compiled = std::make_shared<CompiledGraph>();

    std::unordered_map<int, size_t> index; // node id -> position in nodes_
    std::vector<CompiledGraph::Node> nodes(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        index[nodes_[i].id] = i;
        nodes[i] = {nodes_[i].id, nodes_[i].module, nodes_[i].bypassed, {}, {}};
    }

    // Each link gets its own buffer; reader[b] is the node that reads buffer b.
    std::vector<size_t> reader;
    std::vector<int> waiting(nodes_.size(), 0); // inputs not yet produced, per node
    for (const auto& link : links_) {
        const int buffer = static_cast<int>(reader.size());
        const size_t to = index.at(link.to_node);
        reader.push_back(to);
        nodes[index.at(link.from_node)].outputs.push_back({buffer, link.from_slot});
        nodes[to].inputs.push_back({buffer, link.to_slot});
        ++waiting[to];
    }

    // A node runs once everything feeding it has run. The order doubles as the work queue.
    std::vector<size_t> order;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (waiting[i] == 0) {
            order.push_back(i);
        }
    }
    for (size_t k = 0; k < order.size(); ++k) {
        for (const auto& out : nodes[order[k]].outputs) {
            if (--waiting[reader[out.buffer]] == 0) {
                order.push_back(reader[out.buffer]);
            }
        }
    }

    compiled->buffer_count = reader.size();
    for (size_t i : order) {
        compiled->order.push_back(std::move(nodes[i]));
    }
    return compiled;
}

void AudioGraph::publish(std::shared_ptr<const CompiledGraph> compiled) {
    std::shared_ptr<const CompiledGraph> old;
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        old = std::exchange(published_, std::move(compiled));
    }
    if (old) {
        retired_.push_back(std::move(old));
    }
    // latest() only hands out published_, so a retired snapshot held only here can never
    // be picked up again and its modules are destroyed on this thread.
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [](const auto& g) { return g.use_count() == 1; }),
                   retired_.end());
}

std::shared_ptr<const CompiledGraph> AudioGraph::latest() const {
    std::lock_guard<std::mutex> lock(publish_mutex_);
    return published_;
}
