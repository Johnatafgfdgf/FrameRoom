#pragma once
#include "game_scene.h"
#include "third_party/bb_lsfg.h"
#include <array>
#include <string>

class FrameGeneration final {
public:
    static constexpr VkExtent2D extent{640, 360};
    static constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    FrameGeneration(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family);
    ~FrameGeneration();
    FrameGeneration(const FrameGeneration&) = delete;
    FrameGeneration& operator=(const FrameGeneration&) = delete;

    bool start(const std::string& dll, std::string& error);
    bool renderInput(const SceneFrame& scene, uint32_t input, std::string& error);
    bool generate(bool warmup, double& elapsedMs, std::string& error);
    bool verify(uint32_t current, bool& usable, std::string& diagnostic);
    const std::string& backend() const { return description; }
    bool deviceLost() const { return lost; }

    VkImage original(uint32_t index) const { return inputs.at(index).image; }
    VkImage generated() const { return output.image; }
    void acquireRead(VkCommandBuffer cmd, VkImage image) const;
    void releaseRead(VkCommandBuffer cmd, VkImage image) const;

private:
    struct Api;
    struct SharedImage {
        AHardwareBuffer* buffer = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        void destroy(VkDevice device);
    };
    bool prepareRenderPass(std::string& error);
    bool createImage(SharedImage& target, bool isOutput, uint64_t usage, std::string& error);
    bool prepareReadback(std::string& error);
    bool begin(std::string& error);
    bool submit(std::string& error);
    bool check(VkResult result, const char* operation, std::string& error);
    uint32_t memoryType(uint32_t mask, VkMemoryPropertyFlags flags) const;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    Api* api = nullptr;
    bool initialized = false;
    bool lost = false;
    bool submitted = false;
    int context = -1;
    std::string description;
    std::array<SharedImage, 2> inputs;
    SharedImage output;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
};
