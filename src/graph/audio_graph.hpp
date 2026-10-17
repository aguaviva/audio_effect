#pragma once

#include "node_state.hpp"
#include "node_types.hpp"
#include "paths.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class GraphNode {
public:
    GraphNode() = default;
    GraphNode(int id, NodeType type, float x, float y);

    int id() const { return id_; }
    void set_id(int id) { id_ = id; }

    NodeType type() const { return type_; }
    void set_type(NodeType type) { type_ = type; }

    float x() const { return x_; }
    float y() const { return y_; }
    void set_x(float x) { x_ = x; }
    void set_y(float y) { y_ = y; }
    void set_position(float x, float y) {
        x_ = x;
        y_ = y;
    }

    const NodeState& state() const { return state_; }
    NodeState& state() { return state_; }
    void set_state(const NodeState& state) { state_ = state; }

private:
    int id_ = 0;
    NodeType type_ = NodeType::preamp;
    float x_ = 0.0f;
    float y_ = 0.0f;
    NodeState state_{};
};

class GraphLink {
public:
    GraphLink() = default;
    GraphLink(int id, int from_node, int to_node, int to_slot = 0);

    int id() const { return id_; }
    void set_id(int id) { id_ = id; }

    int from_node() const { return from_node_; }
    int to_node() const { return to_node_; }
    int to_slot() const { return to_slot_; }
    void set_from_node(int id) { from_node_ = id; }
    void set_to_node(int id) { to_node_ = id; }
    void set_to_slot(int slot) { to_slot_ = slot; }

private:
    int id_ = 0;
    int from_node_ = 0;
    int to_node_ = 0;
    int to_slot_ = 0; // destination input slot (mixers have several)
};

// Snapshot consumed by the audio thread.
struct CompiledGraph {
    struct WiredInput {
        int from_node = -1;
        int to_slot = 0;
    };

    class Node {
    public:
        int id() const { return id_; }
        void set_id(int id) { id_ = id; }

        NodeType type() const { return type_; }
        void set_type(NodeType type) { type_ = type; }

        const NodeState& state() const { return state_; }
        NodeState& state() { return state_; }
        void set_state(const NodeState& state) { state_ = state; }

        const std::vector<WiredInput>& inputs() const { return inputs_; }
        const std::vector<int>& outputs() const { return outputs_; }
        std::vector<WiredInput>& inputs() { return inputs_; }
        std::vector<int>& outputs() { return outputs_; }

    private:
        int id_ = 0;
        NodeType type_ = NodeType::preamp;
        NodeState state_{};
        std::vector<WiredInput> inputs_;
        std::vector<int> outputs_;
    };

    std::vector<Node> order; // topological order
    std::unordered_map<int, size_t> index_by_id;
    int output_node_id = -1;
    bool valid = false;
};

class AudioGraph {
public:
    AudioGraph();

    int add_node(NodeType type, float x, float y);
    bool remove_node(int node_id);
    bool add_link(int from_node, int to_node, int to_slot = 0);
    bool remove_link(int link_id);
    bool remove_link_to(int to_node);
    // Visible mixer input pins: one per connection plus one spare (capped).
    int mixer_pin_count(int node_id) const;
    int mixer_connected_count(int node_id) const;
    GraphLink* find_link(int link_id);
    GraphNode* find_node(int node_id);
    const GraphNode* find_node(int node_id) const;

    const std::vector<GraphNode>& nodes() const { return nodes_; }
    const std::vector<GraphLink>& links() const { return links_; }
    std::vector<GraphNode>& nodes() { return nodes_; }
    std::vector<GraphLink>& links() { return links_; }

    int next_node_id() const { return next_node_id_; }
    int next_link_id() const { return next_link_id_; }
    void set_next_node_id(int id) { next_node_id_ = id; }
    void set_next_link_id(int id) { next_link_id_ = id; }

    const std::string& wav_path() const { return wav_path_; }
    void set_wav_path(const std::string& path) { wav_path_ = path; }

    const std::string& record_path() const { return record_path_; }
    void set_record_path(const std::string& path) { record_path_ = path; }

    void clear();
    void reset_to_default_chain();
    void copy_structure_from(const AudioGraph& other);
    std::shared_ptr<const CompiledGraph> compile() const;

    void publish(std::shared_ptr<const CompiledGraph> compiled);
    std::shared_ptr<const CompiledGraph> latest() const;

private:
    std::vector<GraphNode> nodes_;
    std::vector<GraphLink> links_;
    int next_node_id_ = 1;
    int next_link_id_ = 1;
    std::string wav_path_{kDefaultWavPath};
    std::string record_path_{kDefaultRecordPath};

    mutable std::mutex publish_mutex_;
    std::shared_ptr<const CompiledGraph> published_;
};
