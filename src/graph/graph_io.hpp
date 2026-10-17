#pragma once

#include "audio_graph.hpp"

#include <string>

namespace graph_io {

bool save_to_file(const AudioGraph& graph, const std::string& path, std::string& error);
bool load_from_file(AudioGraph& graph, const std::string& path, std::string& error);

std::string serialize(const AudioGraph& graph);
bool deserialize(AudioGraph& graph, const std::string& text, std::string& error);

} // namespace graph_io
