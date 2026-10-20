#include "audio_graph.hpp"

#include "dsp/mixer.hpp"
#include "node_registry.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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

bool AudioGraph::add_link(int from_node, int to_node, int to_slot) {
    if (from_node == to_node) {
        return false;
    }
    const GraphNode* from = find_node(from_node);
    const GraphNode* to = find_node(to_node);
    if (!from || !to || from->module->output_count() == 0) {
        return false;
    }
    if (to_slot < 0 || to_slot >= input_pin_count(to_node)) {
        return false;
    }

    std::unordered_set<int> visited;
    std::vector<int> stack{to_node};
    while (!stack.empty()) {
        int current = stack.back();
        stack.pop_back();
        if (current == from_node) {
            return false;
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
            return false;
        }
    }

    links_.push_back({next_free_id(links_), from_node, to_node, to_slot});
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

void AudioGraph::copy_structure_from(const AudioGraph& other) {
    nodes_ = other.nodes_;
    links_ = other.links_;
    wav_path_ = other.wav_path_;
    record_path_ = other.record_path_;
}

std::shared_ptr<const CompiledGraph> AudioGraph::compile() const {
    auto compiled = std::make_shared<CompiledGraph>();

    std::unordered_map<int, std::vector<int>> next_nodes;
    std::unordered_map<int, std::vector<int>> out_links;
    std::unordered_map<int, std::vector<CompiledGraph::WiredInput>> ins;
    std::unordered_map<int, int> indegree;

    for (const auto& node : nodes_) {
        indegree[node.id] = 0;
        next_nodes[node.id] = {};
        out_links[node.id] = {};
        ins[node.id] = {};
    }
    for (const auto& link : links_) {
        const GraphNode* to = find_node(link.to_node);
        if (!indegree.count(link.from_node) || !to || link.to_slot < 0 ||
            link.to_slot >= to->module->input_count()) {
            continue;
        }
        const int buffer = static_cast<int>(compiled->link_count++);
        next_nodes[link.from_node].push_back(link.to_node);
        out_links[link.from_node].push_back(buffer);
        ins[link.to_node].push_back({buffer, link.to_slot});
        indegree[link.to_node] += 1;
    }

    std::queue<int> ready;
    for (const auto& [id, deg] : indegree) {
        if (deg == 0) {
            ready.push(id);
        }
    }

    std::vector<int> order_ids;
    order_ids.reserve(nodes_.size());
    auto deg = indegree;
    while (!ready.empty()) {
        int id = ready.front();
        ready.pop();
        order_ids.push_back(id);
        for (int next : next_nodes[id]) {
            if (--deg[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (order_ids.size() != nodes_.size()) {
        compiled->valid = false;
        return compiled;
    }

    compiled->order.reserve(order_ids.size());
    for (int id : order_ids) {
        const GraphNode* src = find_node(id);
        if (!src) {
            continue;
        }
        compiled->order.push_back({src->id, src->module, src->bypassed, ins[id], out_links[id]});
    }
    compiled->valid = true;
    return compiled;
}

void AudioGraph::publish(std::shared_ptr<const CompiledGraph> compiled) {
    std::lock_guard<std::mutex> lock(publish_mutex_);
    published_ = std::move(compiled);
}

std::shared_ptr<const CompiledGraph> AudioGraph::latest() const {
    std::lock_guard<std::mutex> lock(publish_mutex_);
    return published_;
}
