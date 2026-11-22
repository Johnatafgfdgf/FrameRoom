#pragma once
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include <android/hardware_buffer.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

// Self-owned AHardwareBuffer imported into this app's Vulkan device.
// This is NOT capture, frame generation or an inter-process Vulkan hook.
class AhbOffscreen final {
public:
    AhbOffscreen() = default;
    AhbOffscreen(const AhbOffscreen&) = delete;
    AhbOffscreen& operator=(const AhbOffscreen&) = delete;
    ~AhbOffscreen() = default;  // Call destroy() before destroying VkDevice.

    bool create(VkPhysicalDevice physical, VkDevice device, VkExtent2D dimensions,
                VkFormat format, std::string& error);
    void record(VkCommandBuffer command, VkImage swapchainImage, uint64_t frameIndex);
    void destroy(VkDevice device);
    bool ready() const { return image_ != VK_NULL_HANDLE; }
    // The returned handle is owned by this object; future consumers must not release it.
    AHardwareBuffer* hardwareBuffer() const { return buffer_; }

private:
    AHardwareBuffer* buffer_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkBuffer barBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory barMemory_ = VK_NULL_HANDLE;
    uint32_t barWidth_ = 0;
    uint32_t barHeight_ = 0;
    VkExtent2D dimensions_{};
    bool firstUse_ = true;
};
