#include "wav_generator.hpp"

#include "miniaudio.h"

std::shared_ptr<const WavClip> WavGenerator::load(const std::string& path,
                                                  uint32_t target_sample_rate,
                                                  std::string& error) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, target_sample_rate);
    config.encodingFormat = ma_encoding_format_wav;
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        error = "Cannot read WAV file";
        return nullptr;
    }

    auto clip = std::make_shared<WavClip>();
    float block[4096 * 2];
    ma_uint64 frames = 0;
    do {
        ma_decoder_read_pcm_frames(&decoder, block, 4096, &frames);
        clip->interleaved_stereo.insert(clip->interleaved_stereo.end(), block, block + frames * 2);
    } while (frames > 0);
    ma_decoder_uninit(&decoder);

    if (clip->interleaved_stereo.empty()) {
        error = "WAV file has no audio";
        return nullptr;
    }
    error.clear();
    return clip;
}
