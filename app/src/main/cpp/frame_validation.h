#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

struct FrameEvidence {
    size_t changedInput = 0;
    size_t differsFromA = 0;
    size_t differsFromB = 0;
    size_t contrast = 0;
    size_t unwritten = 0;

    bool usable(size_t pixels) const {
        const size_t minimum = std::max<size_t>(1, pixels / 1000);
        return changedInput >= minimum && differsFromA >= minimum &&
               differsFromB >= minimum && contrast >= minimum && unwritten == 0;
    }
};

// The startup output is filled with an opaque magenta sentinel before LSFG
// can write to it. The check reads RGB because the model can choose its alpha.
inline FrameEvidence inspectFrames(const uint8_t* a, const uint8_t* b,
                                  const uint8_t* generated, size_t pixels) {
    FrameEvidence result;
    for (size_t offset = 0; offset < pixels * 4; offset += 4) {
        auto differs = [offset](const uint8_t* left, const uint8_t* right) {
            for (size_t c = 0; c < 3; ++c)
                if (std::abs(int(left[offset + c]) - int(right[offset + c])) > 12) return true;
            return false;
        };
        const bool changed = differs(a, b);
        result.changedInput += changed;
        result.differsFromA += changed && differs(a, generated);
        result.differsFromB += changed && differs(b, generated);
        for (size_t c = 0; c < 3; ++c) {
            if (std::abs(int(generated[offset + c]) - int(generated[c])) <= 12) continue;
            ++result.contrast;
            break;
        }
        result.unwritten += generated[offset] == 255 && generated[offset + 1] == 0 &&
                            generated[offset + 2] == 255;
    }
    return result;
}
