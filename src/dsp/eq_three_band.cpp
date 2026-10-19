#include "eq_three_band.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void EqThreeBand::reset() {
    low_l_.clear();
    mid_l_.clear();
    high_l_.clear();
    low_r_.clear();
    mid_r_.clear();
    high_r_.clear();
}

void EqThreeBand::low_shelf(Biquad& s, float freq, float gain_db, float sample_rate) {
    const float A = std::pow(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * kPi * freq / sample_rate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / 2.0f * std::sqrt(2.0f);
    const float two_sqrtA_alpha = 2.0f * std::sqrt(A) * alpha;
    const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + two_sqrtA_alpha);
    const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - two_sqrtA_alpha);
    const float a0 = (A + 1.0f) + (A - 1.0f) * cosw + two_sqrtA_alpha;
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw);
    const float a2 = (A + 1.0f) + (A - 1.0f) * cosw - two_sqrtA_alpha;
    s.b0 = b0 / a0;
    s.b1 = b1 / a0;
    s.b2 = b2 / a0;
    s.a1 = a1 / a0;
    s.a2 = a2 / a0;
}

void EqThreeBand::high_shelf(Biquad& s, float freq, float gain_db, float sample_rate) {
    const float A = std::pow(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * kPi * freq / sample_rate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / 2.0f * std::sqrt(2.0f);
    const float two_sqrtA_alpha = 2.0f * std::sqrt(A) * alpha;
    const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + two_sqrtA_alpha);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - two_sqrtA_alpha);
    const float a0 = (A + 1.0f) - (A - 1.0f) * cosw + two_sqrtA_alpha;
    const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    const float a2 = (A + 1.0f) - (A - 1.0f) * cosw - two_sqrtA_alpha;
    s.b0 = b0 / a0;
    s.b1 = b1 / a0;
    s.b2 = b2 / a0;
    s.a1 = a1 / a0;
    s.a2 = a2 / a0;
}

void EqThreeBand::peaking(Biquad& s, float freq, float gain_db, float q, float sample_rate) {
    const float A = std::pow(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * kPi * freq / sample_rate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * std::max(0.1f, q));
    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw;
    const float a2 = 1.0f - alpha / A;
    s.b0 = b0 / a0;
    s.b1 = b1 / a0;
    s.b2 = b2 / a0;
    s.a1 = a1 / a0;
    s.a2 = a2 / a0;
}

void EqThreeBand::design(float sample_rate, float low_gain_db, float mid_gain_db,
                         float high_gain_db) {
    low_shelf(low_l_, 200.0f, low_gain_db, sample_rate);
    peaking(mid_l_, 1000.0f, mid_gain_db, 0.9f, sample_rate);
    high_shelf(high_l_, 4000.0f, high_gain_db, sample_rate);
    low_r_.b0 = low_l_.b0; low_r_.b1 = low_l_.b1; low_r_.b2 = low_l_.b2;
    low_r_.a1 = low_l_.a1; low_r_.a2 = low_l_.a2;
    mid_r_.b0 = mid_l_.b0; mid_r_.b1 = mid_l_.b1; mid_r_.b2 = mid_l_.b2;
    mid_r_.a1 = mid_l_.a1; mid_r_.a2 = mid_l_.a2;
    high_r_.b0 = high_l_.b0; high_r_.b1 = high_l_.b1; high_r_.b2 = high_l_.b2;
    high_r_.a1 = high_l_.a1; high_r_.a2 = high_l_.a2;
}

void EqThreeBand::process(float* interleaved, size_t sample_count, float sample_rate) {
    if (interleaved == nullptr || sample_count < 2 || sample_rate <= 0.0f) {
        return;
    }
    float low_gain_db = std::clamp(low_db, -18.0f, 18.0f);
    float mid_gain_db = std::clamp(mid_db, -18.0f, 18.0f);
    float high_gain_db = std::clamp(high_db, -18.0f, 18.0f);
    if (sample_rate != last_sr_ || low_gain_db != last_low_ || mid_gain_db != last_mid_ ||
        high_gain_db != last_high_) {
        design(sample_rate, low_gain_db, mid_gain_db, high_gain_db);
        last_sr_ = sample_rate;
        last_low_ = low_gain_db;
        last_mid_ = mid_gain_db;
        last_high_ = high_gain_db;
    }
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        float l = interleaved[i];
        float r = interleaved[i + 1];
        l = high_l_.process(mid_l_.process(low_l_.process(l)));
        r = high_r_.process(mid_r_.process(low_r_.process(r)));
        interleaved[i] = l;
        interleaved[i + 1] = r;
    }
}

bool EqThreeBand::render() {
    bool changed = false;
    ImGui::TextUnformatted("Low dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##eq_low", &low_db, -18.0f, 18.0f, "%.1f");
    ImGui::TextUnformatted("Mid dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##eq_mid", &mid_db, -18.0f, 18.0f, "%.1f");
    ImGui::TextUnformatted("High dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##eq_high", &high_db, -18.0f, 18.0f, "%.1f");
    return changed;
}

void EqThreeBand::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "low_db", low_db);
    module_json::write_float(out, first, "mid_db", mid_db);
    module_json::write_float(out, first, "high_db", high_db);
}

bool EqThreeBand::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "low_db") { low_db = static_cast<float>(num); return true; }
    if (key == "mid_db") { mid_db = static_cast<float>(num); return true; }
    if (key == "high_db") { high_db = static_cast<float>(num); return true; }
    return false;
}
