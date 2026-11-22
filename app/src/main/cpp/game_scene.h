#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct SceneFrame {
    static constexpr size_t kRectangles = 32;
    static constexpr size_t kFields = 7;
    std::array<float, kRectangles * kFields> data{};
    size_t count = 0;

    void add(float x, float y, float w, float h, float r, float g, float b) {
        if (count == kRectangles) return;
        const std::array<float, kFields> values{x,y,w,h,r,g,b};
        std::copy(values.begin(), values.end(), data.begin() + count++ * kFields);
    }
};

inline VkRect2D fitScene(VkExtent2D extent) {
    uint32_t width = (std::min)(extent.width, uint32_t(extent.height * 16ULL / 9));
    uint32_t height = (std::min)(extent.height, uint32_t(extent.width * 9ULL / 16));
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);
    return {{int32_t((extent.width - width) / 2), int32_t((extent.height - height) / 2)}, {width, height}};
}

inline void drawScene(VkCommandBuffer commands, const SceneFrame& scene, VkRect2D area) {
    for (size_t i = 0; i < scene.count; ++i) {
        const float* r = scene.data.data() + i * SceneFrame::kFields;
        if (!std::all_of(r, r + SceneFrame::kFields, [](float value) { return std::isfinite(value); }))
            continue;
        auto bound = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
        int32_t x = int32_t(bound(r[0]) * area.extent.width);
        int32_t y = int32_t(bound(r[1]) * area.extent.height);
        int32_t right = int32_t(bound(r[0] + r[2]) * area.extent.width);
        int32_t bottom = int32_t(bound(r[1] + r[3]) * area.extent.height);
        if (right <= x || bottom <= y) continue;
        VkClearAttachment color{};
        color.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        color.clearValue.color = {{bound(r[4]), bound(r[5]), bound(r[6]), 1.0f}};
        VkClearRect rectangle{{{x + area.offset.x, y + area.offset.y}, {uint32_t(right-x), uint32_t(bottom-y)}}, 0, 1};
        vkCmdClearAttachments(commands, 1, &color, 1, &rectangle);
    }
}

inline SceneFrame movingTestScene(uint64_t index) {
    SceneFrame frame;
    frame.add(0, 0, 1, 1, .055f, .07f, .11f);
    frame.add(0, .88f, 1, .12f, .09f, .11f, .18f);
    for (int i = 0; i < 10; ++i)
        frame.add(i / 10.f, .90f, .044f, .012f, .27f, .31f, .47f);
    auto triangle = [](uint64_t n, uint64_t period) {
        uint64_t phase = n % period;
        return float((std::min)(phase, period - phase)) / (period / 2);
    };
    const float x = .09f + .72f * triangle(index, 84);
    frame.add(x, .25f, .12f, .22f, .38f, .20f, .90f);
    frame.add(x + .02f, .28f, .08f, .028f, .75f, .69f, 1.0f);
    frame.add(x + .02f, .37f, .04f, .07f, .61f, .45f, .97f);
    frame.add(.76f - .57f * triangle(index + 12, 108), .63f, .15f, .12f, .22f, .85f, .74f);
    return frame;
}
