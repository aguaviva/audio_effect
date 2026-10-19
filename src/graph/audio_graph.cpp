#include "audio_graph.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

GraphNode::GraphNode(int id, NodeType type, float x, float y)
    : id_(id), type_(type), x_(x), y_(y), state_(NodeState::make(type)) {}

GraphLink::GraphLink(int id, int from_node, int to_node, int to_slot)
    : id_(id), from_node_(from_node), to_node_(to_node), to_slot_(to_slot) {}

AudioGraph::AudioGraph() {
    reset_to_default_chain();
}

int AudioGraph::add_node(NodeType type, float x, float y) {
    const int id = next_node_id_++;
    nodes_.emplace_back(id, type, x, y);
    return id;
}

bool AudioGraph::remove_node(int node_id) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                           [node_id](const GraphNode& n) { return n.id() == node_id; });
    if (it == nodes_.end()) {
        return false;
    }
    nodes_.erase(it);
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [node_id](const GraphLink& l) {
                                    return l.from_node() == node_id || l.to_node() == node_id;
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
    if (!from || !to) {
        return false;
    }
    if (!node_has_output_pin(from->type()) || !node_has_input_pin(to->type())) {
        return false;
    }

    const int slot_limit = node_input_slot_limit(to->type());
    if (to_slot < 0 || to_slot >= slot_limit) {
        return false;
    }
    // Mixer: only expose connected pins + one spare; reject wiring past that.
    if (node_is_mixer(to->type()) && to_slot >= mixer_pin_count(to_node)) {
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
            if (link.from_node() == current) {
                stack.push_back(link.to_node());
            }
        }
    }

    for (const auto& link : links_) {
        if (link.from_node() == from_node && link.to_node() == to_node &&
            link.to_slot() == to_slot) {
            return false; // duplicate edge into the same slot
        }
        // Exclusive slots (mixer / dry-wet): one wire per input.
        if (node_uses_exclusive_input_slots(to->type()) && link.to_node() == to_node &&
            link.to_slot() == to_slot) {
            return false;
        }
    }

    links_.emplace_back(next_link_id_++, from_node, to_node, to_slot);
    return true;
}

int AudioGraph::mixer_connected_count(int node_id) const {
    int count = 0;
    for (const auto& link : links_) {
        if (link.to_node() == node_id) {
            ++count;
        }
    }
    return count;
}

int AudioGraph::mixer_pin_count(int node_id) const {
    const GraphNode* node = find_node(node_id);
    if (!node || !node_is_mixer(node->type())) {
        return 1;
    }
    int max_slot = -1;
    int connected = 0;
    for (const auto& link : links_) {
        if (link.to_node() != node_id) {
            continue;
        }
        ++connected;
        max_slot = std::max(max_slot, link.to_slot());
    }
    // Auto-detect: show every used slot, plus one empty pin for the next wire.
    const int needed = std::max(max_slot + 1, connected) + 1;
    return std::clamp(needed, 1, kMaxMixerInputs);
}

bool AudioGraph::remove_link(int link_id) {
    auto it = std::find_if(links_.begin(), links_.end(),
                           [link_id](const GraphLink& l) { return l.id() == link_id; });
    if (it == links_.end()) {
        return false;
    }
    links_.erase(it);
    return true;
}

bool AudioGraph::remove_link_to(int to_node) {
    size_t before = links_.size();
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [to_node](const GraphLink& l) { return l.to_node() == to_node; }),
                 links_.end());
    return links_.size() != before;
}

GraphLink* AudioGraph::find_link(int link_id) {
    for (auto& link : links_) {
        if (link.id() == link_id) {
            return &link;
        }
    }
    return nullptr;
}

GraphNode* AudioGraph::find_node(int node_id) {
    for (auto& node : nodes_) {
        if (node.id() == node_id) {
            return &node;
        }
    }
    return nullptr;
}

const GraphNode* AudioGraph::find_node(int node_id) const {
    for (const auto& node : nodes_) {
        if (node.id() == node_id) {
            return &node;
        }
    }
    return nullptr;
}

void AudioGraph::clear() {
    nodes_.clear();
    links_.clear();
    next_node_id_ = 1;
    next_link_id_ = 1;
}

void AudioGraph::copy_structure_from(const AudioGraph& other) {
    nodes_ = other.nodes_;
    links_ = other.links_;
    next_node_id_ = other.next_node_id_;
    next_link_id_ = other.next_link_id_;
    wav_path_ = other.wav_path_;
    record_path_ = other.record_path_;
}

void AudioGraph::reset_to_default_chain() {
    clear();
    set_wav_path(kDefaultWavPath);

    // Default patch: WAV -> preamp -> parallel echo/drive -> mixer -> tone -> meters -> out
    const int wav = add_node(NodeType::wav_player, 40.0f, 220.0f);
    const int pre = add_node(NodeType::preamp, 280.0f, 220.0f);
    const int echo = add_node(NodeType::echo, 520.0f, 80.0f);
    const int drive = add_node(NodeType::drive_pedal, 520.0f, 360.0f);
    const int mix = add_node(NodeType::mixer, 760.0f, 220.0f);
    const int hpf = add_node(NodeType::high_pass, 1000.0f, 220.0f);
    const int lpf = add_node(NodeType::low_pass, 1240.0f, 220.0f);
    const int vu = add_node(NodeType::vu_meter, 1480.0f, 80.0f);
    const int scope = add_node(NodeType::oscilloscope, 1480.0f, 280.0f);
    const int spec = add_node(NodeType::spectrum, 1480.0f, 480.0f);
    const int out = add_node(NodeType::output, 1720.0f, 220.0f);

    if (auto* p = find_node(wav)->state().as<node_params::WavPlayer>()) {
        p->loop = true;
        p->path = kDefaultWavPath;
    }
    if (auto* p = find_node(pre)->state().as<node_params::Preamp>()) {
        p->gain_db = 0.0f;
    }
    if (auto* p = find_node(echo)->state().as<node_params::Echo>()) {
        p->delay_ms = 320.0f;
        p->feedback = 0.35f;
        p->mix = 0.25f;
    }
    if (auto* p = find_node(drive)->state().as<node_params::Drive>()) {
        p->amount = 3.0f;
        p->tone = 0.65f;
    }
    if (auto* p = find_node(hpf)->state().as<node_params::HighPass>()) {
        p->cutoff_hz = 70.0f;
    }
    if (auto* p = find_node(lpf)->state().as<node_params::LowPass>()) {
        p->cutoff_hz = 8000.0f;
    }

    add_link(wav, pre);
    add_link(pre, echo);
    add_link(pre, drive);
    add_link(echo, mix, 0);
    add_link(drive, mix, 1);
    add_link(mix, hpf);
    add_link(hpf, lpf);
    add_link(lpf, vu);
    add_link(vu, scope);
    add_link(scope, spec);
    add_link(spec, out);
}

std::shared_ptr<const CompiledGraph> AudioGraph::compile() const {
    auto compiled = std::make_shared<CompiledGraph>();

    std::unordered_map<int, std::vector<int>> outs;
    std::unordered_map<int, std::vector<CompiledGraph::WiredInput>> ins;
    std::unordered_map<int, int> indegree;

    for (const auto& node : nodes_) {
        indegree[node.id()] = 0;
        outs[node.id()] = {};
        ins[node.id()] = {};
    }
    for (const auto& link : links_) {
        if (!indegree.count(link.from_node()) || !indegree.count(link.to_node())) {
            continue;
        }
        outs[link.from_node()].push_back(link.to_node());
        ins[link.to_node()].push_back({link.from_node(), link.to_slot()});
        indegree[link.to_node()] += 1;
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
        for (int next : outs[id]) {
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
        CompiledGraph::Node node;
        node.set_id(src->id());
        node.set_type(src->type());
        node.set_state(src->state());
        node.inputs() = ins[id];
        node.outputs() = outs[id];
        compiled->index_by_id[id] = compiled->order.size();
        compiled->order.push_back(std::move(node));
        if (src->type() == NodeType::output) {
            compiled->output_node_id = id;
        }
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
