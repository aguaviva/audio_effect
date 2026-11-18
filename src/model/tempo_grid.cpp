#include "tempo_grid.hpp"

#include "dsp/module_json.hpp"

#include <algorithm>
#include <cmath>

void TempoGrid::set_time_signature(int numerator, int denominator) {
    numerator_ = std::clamp(numerator, 1, 32);
    int d = 1;
    while (d < denominator && d < 32) {
        d *= 2;
    }
    denominator_ = d;
}

double TempoGrid::beats_per_bar() const {
    return numerator_ * 4.0 / static_cast<double>(denominator_);
}

void TempoGrid::set_bpm(double bpm) {
    changes_ = {{0.0, 60.0 / std::clamp(bpm, 1.0, 999.0)}};
}

double TempoGrid::bpm() const { return 60.0 / changes_.front().seconds_per_beat; }

void TempoGrid::set_tempo_map(std::vector<TempoGridChange> changes) {
    changes.erase(std::remove_if(changes.begin(), changes.end(),
                                 [](const TempoGridChange& c) { return !(c.seconds_per_beat > 0.0); }),
                  changes.end());
    if (changes.empty()) {
        return;
    }
    std::stable_sort(changes.begin(), changes.end(),
                     [](const TempoGridChange& a, const TempoGridChange& b) { return a.beat < b.beat; });
    changes.front().beat = 0.0;
    changes_ = std::move(changes);
}

double TempoGrid::seconds_at_beat(double beat) const {
    double seconds = 0.0;
    double from = 0.0;
    double seconds_per_beat = changes_.front().seconds_per_beat;
    for (const TempoGridChange& change : changes_) {
        if (change.beat >= beat) {
            break;
        }
        seconds += (change.beat - from) * seconds_per_beat;
        from = change.beat;
        seconds_per_beat = change.seconds_per_beat;
    }
    return seconds + (beat - from) * seconds_per_beat;
}

double TempoGrid::beat_at_seconds(double seconds) const {
    double at = 0.0; // seconds at beat `from`
    double from = 0.0;
    double seconds_per_beat = changes_.front().seconds_per_beat;
    for (const TempoGridChange& change : changes_) {
        const double at_change = at + (change.beat - from) * seconds_per_beat;
        if (at_change >= seconds) {
            break;
        }
        at = at_change;
        from = change.beat;
        seconds_per_beat = change.seconds_per_beat;
    }
    return from + (seconds - at) / seconds_per_beat;
}

void TempoGrid::set_division(int divisions_per_beat) {
    division_ = std::clamp(divisions_per_beat, 1, 64);
}

double TempoGrid::snap_beat(double beat) const {
    const double step = 1.0 / division_;
    return std::max(0.0, std::round(beat / step) * step);
}

double TempoGrid::snap_seconds(double seconds) const {
    return seconds_at_beat(snap_beat(beat_at_seconds(std::max(0.0, seconds))));
}

std::string TempoGrid::save() const {
    std::string out = "{";
    module_json::write_int(out, "numerator", numerator_);
    module_json::write_int(out, "denominator", denominator_);
    module_json::write_int(out, "division", division_);
    module_json::append_comma(out);
    out += "\"tempo\":[";
    for (const TempoGridChange& c : changes_) {
        module_json::append_comma(out);
        std::string change = "{";
        module_json::write_float(change, "beat", static_cast<float>(c.beat));
        module_json::write_float(change, "bpm", static_cast<float>(60.0 / c.seconds_per_beat));
        out += change + '}';
    }
    out += "]}";
    return out;
}

bool TempoGrid::load(const std::string& json) {
    int numerator = numerator_;
    int denominator = denominator_;
    int division = division_;
    module_json::read(json, "numerator", numerator);
    module_json::read(json, "denominator", denominator);
    module_json::read(json, "division", division);
    set_time_signature(numerator, denominator);
    set_division(division);
    std::vector<TempoGridChange> changes;
    const bool ok = module_json::for_each_member(
        json, [&](const std::string& key, const std::string& text, size_t& i) {
            if (key != "tempo") {
                return module_json::skip_value(text, i);
            }
            return module_json::for_each_element(text, i, [&](const std::string& element) {
                float beat = 0.0f;
                float bpm = 120.0f;
                module_json::read(element, "beat", beat);
                module_json::read(element, "bpm", bpm);
                changes.push_back({beat, 60.0 / std::clamp<double>(bpm, 1.0, 999.0)});
                return true;
            });
        });
    set_tempo_map(std::move(changes));
    return ok;
}

TempoGrid& song_grid() {
    static TempoGrid grid;
    return grid;
}
