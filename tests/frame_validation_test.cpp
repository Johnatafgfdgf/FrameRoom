#include "frame_validation.h"
#include <array>
#include <cassert>
#include <iostream>

namespace {
constexpr size_t pixels = 16;
using Row = std::array<uint8_t, pixels * 4>;

Row makeFrame(size_t start, size_t width) {
    Row row{};
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t color = i >= start && i < start + width ? 210 : 23;
        row[i * 4] = row[i * 4 + 1] = row[i * 4 + 2] = color;
        row[i * 4 + 3] = 255;
    }
    return row;
}
}

int main() {
    const auto first = makeFrame(2, 3);
    const auto second = makeFrame(4, 3);
    const auto middle = makeFrame(3, 3);
    const auto blank = makeFrame(0, 0);
    const auto check = [&](const Row& a, const Row& b, const Row& generated) {
        return inspectFrames(a.data(), b.data(), generated.data(), pixels);
    };
    assert(check(first, second, middle).usable(pixels));
    assert(!check(first, second, first).usable(pixels));
    assert(!check(first, second, second).usable(pixels));
    assert(!check(first, first, middle).usable(pixels));
    assert(!check(first, second, blank).usable(pixels));
    auto partial = middle;
    partial[12] = 255;
    partial[13] = 0;
    partial[14] = 255;
    assert(check(first, second, partial).unwritten == 1);
    assert(!check(first, second, partial).usable(pixels));
    std::cout << "PASS: frame verifier accepts a distinct image and rejects blank, incomplete or repeated frames\n";
}
