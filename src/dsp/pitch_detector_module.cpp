#include "pitch_detector_module.hpp"

void PitchDetectorModule::process(const float* const* inputs, int input_count, float* output,
                                  size_t sample_count, float /*sample_rate*/) {
    copy_input(inputs, input_count, 0, output, sample_count);
    if (host_) {
        host_->analyze(node_id_, name(), output, sample_count, 1.0f, 0.85f);
    }
}
