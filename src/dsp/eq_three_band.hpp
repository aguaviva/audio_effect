#pragma once

#include "module_json.hpp"

#include <cstddef>
#include <string>

// Stereo 3-band EQ: low shelf, mid peak, high shelf (RBJ-style biquads).
class EqThreeBand {
public:
    static std::string name() { return "eq_three_band"; }

    struct Params {
        float low_db = 0.0f;
        float mid_db = 0.0f;
        float high_db = 0.0f;

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    void process(float* interleaved, size_t sample_count, float low_gain_db,
                 float mid_gain_db, float high_gain_db, float sample_rate);
    void reset();

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0; }
    };

    void design(float sample_rate, float low_gain_db, float mid_gain_db, float high_gain_db);
    static void low_shelf(Biquad& s, float freq, float gain_db, float sample_rate);
    static void peaking(Biquad& s, float freq, float gain_db, float q, float sample_rate);
    static void high_shelf(Biquad& s, float freq, float gain_db, float sample_rate);

    Biquad low_l_{}, mid_l_{}, high_l_{};
    Biquad low_r_{}, mid_r_{}, high_r_{};
    float last_sr_ = 0, last_low_ = 0, last_mid_ = 0, last_high_ = 0;
};
