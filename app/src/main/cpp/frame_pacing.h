#pragma once

#include <algorithm>
#include <cstdint>

namespace pacing {

using Time = int64_t;
constexpr Time ms = 1'000'000;

struct PairTimes {
    Time generated = 0;
    Time original = 0;
    explicit operator bool() const { return original != 0; }
};

// Give the next original the current slot if the renderer has already missed
// its deadline. There is no full-period idle wait after a long submit.
inline Time nextOriginal(Time frameStart, Time now, Time period) {
    return std::max(frameStart + period, now);
}

inline PairTimes pairTimes(Time frameStart, Time ready, Time period) {
    const Time half = period / 2;
    // Reserve time for submission. The compositor, rather than the render
    // thread, spaces the two images when timed presentation is available.
    const Time generated = std::max(frameStart + half, ready + ms);
    const Time original = generated + half;
    if (original > frameStart + period + ms) return {};
    return {generated, original};
}

inline bool enoughRefresh(Time originalPeriod, Time refreshPeriod) {
    // Permit small nominal differences such as 59.94 Hz on a 60 Hz mode.
    return refreshPeriod == 0 || refreshPeriod * 2 <= originalPeriod + originalPeriod / 40;
}

class GenerationBudget {
public:
    bool shouldReduce(bool dropped) {
        history = ((history << 1) | static_cast<unsigned>(dropped)) & 0xffu;
        if (count < 8) ++count;
        if (count < 8) return false;
        unsigned misses = 0;
        for (unsigned x = history; x; x &= x - 1) ++misses;
        return misses >= 5;
    }

private:
    unsigned history = 0;
    unsigned count = 0;
};

} // namespace pacing
