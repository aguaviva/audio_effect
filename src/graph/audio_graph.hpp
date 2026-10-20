#pragma once

#include "dsp/module.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct GraphNode {
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool bypassed = false;
    // Copies share this instance: the UI renders it and the audio thread processes it,
    // so settings never need to be copied between them.
    std::shared_ptr<Module> module;

    template <typename T>
    T* as() const {
        return dynamic_cast<T*>(module.get());
    }
};

struct GraphLink {
    int id = 0;
    int from_node = 0;
    int to_node = 0;
    int to_slot = 0;
};

// Snapshot consumed by the audio thread. Every link owns one audio buffer, numbered
// 0..link_count-1: its source node writes it and its destination node reads it.
struct CompiledGraph {
    struct WiredInput {
        int link = -1;
        int to_slot = 0;
    };

    struct Node {
        int id = 0;
        std::shared_ptr<Module> module;
        bool bypassed = false;
        std::vector<WiredInput> inputs;
        std::vector<int> outputs; // links this node writes
    };

    std::vector<Node> order; // topological order
    size_t link_count = 0;
    bool valid = false;
};

class AudioGraph {
public:
    // Returns the new node id, or -1 when no module has this name.
    int add_node(const std::string& type, float x, float y);
    bool remove_node(int node_id);
    bool add_link(int from_node, int to_node, int to_slot = 0);
    bool remove_link(int link_id);
    // Input pins to show: the module's count, except a Mixer shows one per connection
    // plus one spare.
    int input_pin_count(int node_id) const;
    GraphNode* find_node(int node_id);
    const GraphNode* find_node(int node_id) const;

    const std::vector<GraphNode>& nodes() const { return nodes_; }
    const std::vector<GraphLink>& links() const { return links_; }
    std::vector<GraphNode>& nodes() { return nodes_; }
    std::vector<GraphLink>& links() { return links_; }

    void clear();
    std::shared_ptr<const CompiledGraph> compile() const;

    // UI thread. Replaced snapshots are freed here, never on the audio thread, once the
    // audio thread has let go of them.
    void publish(std::shared_ptr<const CompiledGraph> compiled);
    // Audio thread.
    std::shared_ptr<const CompiledGraph> latest() const;

private:
    std::vector<GraphNode> nodes_;
    std::vector<GraphLink> links_;

    mutable std::mutex publish_mutex_;
    std::shared_ptr<const CompiledGraph> published_;
    std::vector<std::shared_ptr<const CompiledGraph>> retired_; // UI thread only
};
