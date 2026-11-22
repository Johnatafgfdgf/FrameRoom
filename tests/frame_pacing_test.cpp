#include "frame_pacing.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace pacing;
    constexpr Time frame = 10'000 * ms;
    constexpr Time at30 = 1'000'000'000 / 30;
    constexpr Time at60 = 1'000'000'000 / 60;

    assert(nextOriginal(frame, frame + 10 * ms, at30) == frame + at30);
    // A slow 38 ms batch used to add another 33 ms before the next original.
    assert(nextOriginal(frame, frame + 38 * ms, at30) == frame + 38 * ms);

    auto poCoAt30 = pairTimes(frame, frame + 12 * ms, at30);
    assert(poCoAt30);
    assert(poCoAt30.original - poCoAt30.generated == at30 / 2);
    assert(poCoAt30.original <= frame + at30 + ms);
    assert(!pairTimes(frame, frame + 13 * ms, at60));
    assert(pairTimes(frame, frame + 6 * ms, at60));

    assert(enoughRefresh(at30, at60));
    assert(enoughRefresh(at30, 16'683'350));  // 59.94 Hz
    assert(enoughRefresh(at60, 0));          // No refresh measurement
    assert(!enoughRefresh(at60, at60));
    assert(enoughRefresh(at60, 1'000'000'000 / 120));
    assert(!enoughRefresh(1'000'000'000 / 45, at60));

    GenerationBudget sustained;
    for (int i = 0; i < 7; ++i) assert(!sustained.shouldReduce(i >= 3));
    assert(sustained.shouldReduce(true));
    GenerationBudget alternating;
    for (int i = 0; i < 24; ++i) assert(!alternating.shouldReduce(i % 2 == 0));
    std::cout << "PASS: Vulkan cadence does not add a period after a miss, FG pairs fit their deadline, "
                 "and the overload guard ignores sporadic misses.\n";
}
