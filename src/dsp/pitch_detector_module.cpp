#include "pitch_detector_module.hpp"

void PitchDetectorModule::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, 1.0f, 0.85f);
    }
}
