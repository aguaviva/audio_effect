#include "graph_io.hpp"

#include "node_state.hpp"
#include "paths.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace graph_io {
namespace {

std::string escape_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

bool parse_string(const std::string& text, size_t& i, std::string& out) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i >= text.size() || text[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < text.size()) {
        char c = text[i++];
        if (c == '\\' && i < text.size()) {
            out.push_back(text[i++]);
            continue;
        }
        if (c == '"') {
            return true;
        }
        out.push_back(c);
    }
    return false;
}

bool expect(const std::string& text, size_t& i, char ch) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i >= text.size() || text[i] != ch) {
        return false;
    }
    ++i;
    return true;
}

bool parse_number(const std::string& text, size_t& i, double& out) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        ++i;
    }
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.' ||
                               text[i] == 'e' || text[i] == 'E' || text[i] == '+' || text[i] == '-')) {
        // Stop at structural chars; allow exponent signs after e/E only.
        if ((text[i] == '+' || text[i] == '-') && i > start &&
            text[i - 1] != 'e' && text[i - 1] != 'E') {
            break;
        }
        if (text[i] == ',' || text[i] == '}' || text[i] == ']') {
            break;
        }
        ++i;
    }
    if (start == i) {
        return false;
    }
    try {
        out = std::stod(text.substr(start, i - start));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

std::string serialize(const AudioGraph& graph) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"version\":2,\n";
    oss << "  \"wav_path\":\"" << escape_string(graph.wav_path()) << "\",\n";
    oss << "  \"record_path\":\"" << escape_string(graph.record_path()) << "\",\n";
    oss << "  \"next_node_id\":" << graph.next_node_id() << ",\n";
    oss << "  \"next_link_id\":" << graph.next_link_id() << ",\n";
    oss << "  \"nodes\":[\n";
    const auto& nodes = graph.nodes();
    for (size_t n = 0; n < nodes.size(); ++n) {
        const auto& node = nodes[n];
        oss << "    {\"id\":" << node.id()
            << ",\"type\":\"" << escape_string(node_params::name_for_type(node.type())) << "\""
            << ",\"x\":" << node.x()
            << ",\"y\":" << node.y()
            << ",\"params\":" << node.state().write_params() << "}";
        if (n + 1 < nodes.size()) oss << ',';
        oss << '\n';
    }
    oss << "  ],\n  \"links\":[\n";
    const auto& links = graph.links();
    for (size_t n = 0; n < links.size(); ++n) {
        const auto& link = links[n];
        oss << "    {\"id\":" << link.id()
            << ",\"from\":" << link.from_node()
            << ",\"to\":" << link.to_node()
            << ",\"to_slot\":" << link.to_slot() << "}";
        if (n + 1 < links.size()) oss << ',';
        oss << '\n';
    }
    oss << "  ]\n}\n";
    return oss.str();
}

bool deserialize(AudioGraph& graph, const std::string& text, std::string& error) {
    size_t i = 0;
    if (!expect(text, i, '{')) {
        error = "Expected object";
        return false;
    }

    AudioGraph loaded;
    loaded.clear();
    loaded.set_wav_path(kDefaultWavPath);
    loaded.set_record_path(kDefaultRecordPath);
    int next_node = 1;
    int next_link = 1;
    int file_version = 1;
    bool have_nodes = false;
    bool have_links = false;

    while (true) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        std::string key;
        if (!parse_string(text, i, key) || !expect(text, i, ':')) {
            error = "Invalid key";
            return false;
        }

        if (key == "version") {
            double version = 0;
            if (!parse_number(text, i, version)) {
                error = "Invalid version";
                return false;
            }
            file_version = static_cast<int>(version);
        } else if (key == "wav_path") {
            std::string path;
            if (!parse_string(text, i, path)) {
                error = "Invalid wav_path";
                return false;
            }
            loaded.set_wav_path(path);
        } else if (key == "record_path") {
            std::string path;
            if (!parse_string(text, i, path)) {
                error = "Invalid record_path";
                return false;
            }
            loaded.set_record_path(path);
        } else if (key == "next_node_id" || key == "next_link_id") {
            double value = 0;
            if (!parse_number(text, i, value)) {
                error = "Invalid id counter";
                return false;
            }
            if (key == "next_node_id") next_node = static_cast<int>(value);
            else next_link = static_cast<int>(value);
        } else if (key == "nodes") {
            if (!expect(text, i, '[')) {
                error = "nodes must be an array";
                return false;
            }
            while (true) {
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                if (i < text.size() && text[i] == ']') {
                    ++i;
                    break;
                }
                if (!expect(text, i, '{')) {
                    error = "Invalid node object";
                    return false;
                }
                GraphNode node;
                bool have_params = false;
                NodeType parsed_type = NodeType::preamp;
                bool have_type = false;
                while (true) {
                    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                    if (i < text.size() && text[i] == '}') {
                        ++i;
                        break;
                    }
                    std::string nkey;
                    if (!parse_string(text, i, nkey) || !expect(text, i, ':')) {
                        error = "Invalid node field";
                        return false;
                    }
                    if (nkey == "id" || nkey == "x" || nkey == "y") {
                        double value = 0;
                        if (!parse_number(text, i, value)) {
                            error = "Invalid numeric node field";
                            return false;
                        }
                        if (nkey == "id") node.set_id(static_cast<int>(value));
                        if (nkey == "x") node.set_x(static_cast<float>(value));
                        if (nkey == "y") node.set_y(static_cast<float>(value));
                    } else if (nkey == "type") {
                        std::string type_key;
                        // Match JSON type against each module's name().
                        if (!parse_string(text, i, type_key) ||
                            !node_params::try_resolve_name(type_key, parsed_type)) {
                            error = "Unknown module name";
                            return false;
                        }
                        have_type = true;
                        node.set_type(parsed_type);
                        if (!have_params) {
                            node.set_state(NodeState::make(parsed_type));
                        }
                    } else if (nkey == "params") {
                        NodeState state;
                        if (have_type) {
                            // Prefer name-dispatched load once the module id is known.
                            if (!state.load_by_module_name(node_params::name_for_type(parsed_type),
                                                           text, i)) {
                                error = "Invalid params";
                                return false;
                            }
                        } else if (!state.read_params(node.type(), text, i)) {
                            error = "Invalid params";
                            return false;
                        }
                        node.set_state(state);
                        have_params = true;
                    } else {
                        error = "Unknown node field: " + nkey;
                        return false;
                    }
                    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                    if (i < text.size() && text[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < text.size() && text[i] == '}') {
                        ++i;
                        break;
                    }
                    error = "Malformed node";
                    return false;
                }
                if (node.id() <= 0) {
                    error = "Node missing id";
                    return false;
                }
                if (have_type && !have_params) {
                    node.set_state(NodeState::make(node.type()));
                }
                loaded.nodes().push_back(node);
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                if (i < text.size() && text[i] == ',') ++i;
            }
            have_nodes = true;
        } else if (key == "links") {
            if (!expect(text, i, '[')) {
                error = "links must be an array";
                return false;
            }
            while (true) {
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                if (i < text.size() && text[i] == ']') {
                    ++i;
                    break;
                }
                if (!expect(text, i, '{')) {
                    error = "Invalid link object";
                    return false;
                }
                GraphLink link;
                while (true) {
                    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                    if (i < text.size() && text[i] == '}') {
                        ++i;
                        break;
                    }
                    std::string lkey;
                    if (!parse_string(text, i, lkey) || !expect(text, i, ':')) {
                        error = "Invalid link field";
                        return false;
                    }
                    double value = 0;
                    if (!parse_number(text, i, value)) {
                        error = "Invalid link number";
                        return false;
                    }
                    if (lkey == "id") link.set_id(static_cast<int>(value));
                    else if (lkey == "from") link.set_from_node(static_cast<int>(value));
                    else if (lkey == "to") link.set_to_node(static_cast<int>(value));
                    else if (lkey == "to_slot") link.set_to_slot(static_cast<int>(value));
                    else {
                        error = "Unknown link field";
                        return false;
                    }
                    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                    if (i < text.size() && text[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < text.size() && text[i] == '}') {
                        ++i;
                        break;
                    }
                    error = "Malformed link";
                    return false;
                }
                loaded.links().push_back(link);
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                if (i < text.size() && text[i] == ',') ++i;
            }
            have_links = true;
        } else {
            error = "Unknown top-level key: " + key;
            return false;
        }

        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        if (i >= text.size()) break;
    }

    if (!have_nodes) {
        error = "Graph file missing nodes";
        return false;
    }
    (void)have_links;
    loaded.set_next_node_id(next_node);
    loaded.set_next_link_id(next_link);

    // Validate link endpoints exist; drop broken ones rather than failing hard.
    std::vector<GraphLink> valid_links;
    for (const auto& link : loaded.links()) {
        if (loaded.find_node(link.from_node()) && loaded.find_node(link.to_node())) {
            valid_links.push_back(link);
        }
    }
    loaded.links() = std::move(valid_links);

    // v1 patches kept the WAV on the graph; copy onto each WavPlayer.
    if (file_version < 2) {
        for (auto& node : loaded.nodes()) {
            if (node.type() == NodeType::wav_player) {
                if (auto* p = node.state().as<node_params::WavPlayer>()) {
                    p->set_path(loaded.wav_path());
                }
            }
        }
    }

    graph.copy_structure_from(loaded);
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
