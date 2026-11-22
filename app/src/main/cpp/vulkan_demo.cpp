#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#include "ahb_offscreen.h"
#include "framegen_vulkan.h"
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iomanip>
#include <sstream>
#include <time.h>

namespace {
using Clock = std::chrono::steady_clock;
using Ns = std::chrono::nanoseconds;

uint64_t monotonicNs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
}

struct PresentedFrame {
    uint32_t id;
    bool generated;
    uint64_t sampleNs;
    uint64_t presentedNs = 0;
};

std::string vkError(const char* stage, VkResult result) {
    return std::string(stage) + " (VkResult=" + std::to_string((int)result) + ")";
}

class DemoRenderer {
public:
    DemoRenderer(ANativeWindow* nativeWindow, bool useAhb, bool live)
        : window(nativeWindow), ahbMode(useAhb), liveMode(live) {}
    ~DemoRenderer() { cleanup(); }

    bool init(std::string& error) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "FrameRoom Native Vulkan Demo";
        app.apiVersion = (liveMode || ahbMode) ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
        const char* instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        instanceInfo.enabledExtensionCount = 2;
        instanceInfo.ppEnabledExtensionNames = instanceExtensions;
        VkResult v = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (v == VK_ERROR_INCOMPATIBLE_DRIVER && liveMode) {
            app.apiVersion = VK_API_VERSION_1_0;
            v = vkCreateInstance(&instanceInfo, nullptr, &instance);
        }
        if (v != VK_SUCCESS) { error = vkError("vkCreateInstance", v); return false; }

        VkAndroidSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.window = window;
        v = vkCreateAndroidSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
        if (v != VK_SUCCESS) { error = vkError("vkCreateAndroidSurfaceKHR", v); return false; }

        uint32_t count = 0;
        v = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (v != VK_SUCCESS || count == 0) { error = "Nenhum dispositivo Vulkan."; return false; }
        std::vector<VkPhysicalDevice> physicalDevices(count);
        v = vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data());
        if (v != VK_SUCCESS) { error = vkError("vkEnumeratePhysicalDevices", v); return false; }
        for (VkPhysicalDevice candidate : physicalDevices) {
            uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (uint32_t q = 0; q < queueCount; ++q) {
                VkBool32 supported = VK_FALSE;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, q, surface, &supported) != VK_SUCCESS) continue;
                if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported) {
                    uint32_t n = 0;
                    if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, nullptr) != VK_SUCCESS) continue;
                    std::vector<VkExtensionProperties> exts(n);
                    if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, exts.data()) != VK_SUCCESS) continue;
                    auto supports = [&](const char* extension) {
                        return std::any_of(exts.begin(), exts.end(), [&](const auto& item) {
                            return std::strcmp(item.extensionName, extension) == 0;
                        });
                    };
                    if (!supports(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(candidate, &properties);
                    canImport = app.apiVersion >= VK_API_VERSION_1_1 &&
                        properties.apiVersion >= VK_API_VERSION_1_1 &&
                        supports(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) &&
                        supports(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
                    if (ahbMode && !canImport) continue;
                    displayTiming = supports(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
                    gpu = candidate;
                    queueFamily = q;
                    break;
                }
            }
            if (gpu != VK_NULL_HANDLE) break;
        }
        if (gpu == VK_NULL_HANDLE) { error = ahbMode ? "Sem GPU Vulkan 1.1 + swapchain + VK_ANDROID_external_memory_android_hardware_buffer." : "Nenhuma fila Vulkan com gráficos, apresentação e swapchain."; return false; }

        float priority = 1.f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (canImport) {
            extensions.push_back(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
            extensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        }
        if (displayTiming) extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();
        v = vkCreateDevice(gpu, &deviceInfo, nullptr, &device);
        if (v != VK_SUCCESS) { error = vkError("vkCreateDevice", v); return false; }
        vkGetDeviceQueue(device, queueFamily, 0, &graphicsQueue);
        if (displayTiming)
            getTimings = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
                vkGetDeviceProcAddr(device, "vkGetPastPresentationTimingGOOGLE"));
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = queueFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        v = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
        if (v != VK_SUCCESS) { error = vkError("vkCreateCommandPool", v); return false; }
        VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdInfo.commandPool = commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        v = vkAllocateCommandBuffers(device, &cmdInfo, &commandBuffer);
        if (v != VK_SUCCESS) { error = vkError("vkAllocateCommandBuffers", v); return false; }

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        v = vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailable);
        if (v != VK_SUCCESS) { error = vkError("vkCreateSemaphore (acquire)", v); return false; }
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        v = vkCreateFence(device, &fenceInfo, nullptr, &inFlight);
        if (v != VK_SUCCESS) { error = vkError("vkCreateFence", v); return false; }
        return createSwapchain(error);
    }

    bool supportsFG() const { return blit && canImport; }
    bool hasDisplayTiming() const { return getTimings != nullptr; }
    void forgetTimings() { pending.clear(); }
    bool deviceLost() const { return lastResult == VK_ERROR_DEVICE_LOST; }
    std::unique_ptr<FrameGeneration> createFG() {
        return std::make_unique<FrameGeneration>(gpu, device, graphicsQueue, queueFamily);
    }

    std::vector<PresentedFrame> collectTimings() {
        std::vector<PresentedFrame> observed;
        if (!getTimings) return observed;
        uint32_t count = 0;
        if (getTimings(device, swapchain, &count, nullptr) != VK_SUCCESS || !count) return observed;
        std::vector<VkPastPresentationTimingGOOGLE> results(count);
        const auto result = getTimings(device, swapchain, &count, results.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) return observed;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& timing = results[i];
            const auto found = std::find_if(pending.begin(), pending.end(),
                [&](const auto& frame) { return frame.id == timing.presentID; });
            if (found == pending.end()) continue;
            if (timing.actualPresentTime) {
                found->presentedNs = timing.actualPresentTime;
                observed.push_back(*found);
            }
            pending.erase(found);
        }
        std::sort(observed.begin(), observed.end(), [](const PresentedFrame& a, const PresentedFrame& b) {
            return a.presentedNs < b.presentedNs;
        });
        return observed;
    }

    bool draw(uint64_t index, std::string& error, const SceneFrame* scene = nullptr,
              FrameGeneration* synthesis = nullptr, VkImage source = VK_NULL_HANDLE,
              bool generated = false, uint64_t sampleNs = 0) {
        sent = false;
        if (source && !blit) {
            if (generated) return true;
            source = VK_NULL_HANDLE;
        }
        VkResult v = vkWaitForFences(device, 1, &inFlight, VK_TRUE, 250'000'000);
        if (v == VK_TIMEOUT) return true;
        if (v != VK_SUCCESS) return drawError("vkWaitForFences", v, error);
        uint32_t imageIndex = 0;
        v = vkAcquireNextImageKHR(device, swapchain, 50'000'000, imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (v == VK_TIMEOUT || v == VK_NOT_READY) return true;
        if (v == VK_ERROR_OUT_OF_DATE_KHR) return recreate(error);
        if (v != VK_SUCCESS && v != VK_SUBOPTIMAL_KHR) return drawError("vkAcquireNextImageKHR", v, error);
        v = vkResetCommandBuffer(commandBuffer, 0);
        if (v != VK_SUCCESS) { error = vkError("vkResetCommandBuffer", v); return false; }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        v = vkBeginCommandBuffer(commandBuffer, &begin);
        if (v != VK_SUCCESS) { error = vkError("vkBeginCommandBuffer", v); return false; }

        float wave = (float)((std::sin(index * 0.045) + 1.0) * 0.5);
        VkClearValue background{};
        background.color.float32[0] = 0.035f + wave * 0.08f;
        background.color.float32[1] = 0.065f;
        background.color.float32[2] = 0.16f + wave * 0.17f;
        background.color.float32[3] = 1.f;
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffers[imageIndex];
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &background;
        if (source) {
            recordBlit(images[imageIndex], *synthesis, source);
        } else if (ahbMode) {
            AhbOffscreen& current = offscreenFrames[index % offscreenFrames.size()];
            if (!current.ready()) { error = "Quadro AHB não inicializado."; return false; }
            current.record(commandBuffer, images[imageIndex], index);
        } else if (scene) {
            background.color = {{0.024f, 0.032f, 0.052f, 1.0f}};
            vkCmdBeginRenderPass(commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
            drawScene(commandBuffer, *scene, fitScene(extent));
            vkCmdEndRenderPass(commandBuffer);
        } else {
            vkCmdBeginRenderPass(commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
        int x = (int)((index * 6) % std::max(1u, extent.width));
        uint32_t barWidth = std::min(110u, extent.width - (uint32_t)x);
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear.colorAttachment = 0;
        clear.clearValue.color.float32[0] = 0.64f;
        clear.clearValue.color.float32[1] = 0.47f;
        clear.clearValue.color.float32[2] = 1.f;
        clear.clearValue.color.float32[3] = 1.f;
        VkClearRect rect{};
        rect.rect.offset = {x, (int32_t)(extent.height / 3)};
        rect.rect.extent = {barWidth, std::min(85u, extent.height - extent.height / 3)};
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        vkCmdClearAttachments(commandBuffer, 1, &clear, 1, &rect);
        vkCmdEndRenderPass(commandBuffer);
        }
        v = vkEndCommandBuffer(commandBuffer);
        if (v != VK_SUCCESS) { error = vkError("vkEndCommandBuffer", v); return false; }

        v = vkResetFences(device, 1, &inFlight);
        if (v != VK_SUCCESS) { error = vkError("vkResetFences", v); return false; }
        VkPipelineStageFlags stage = (ahbMode || source) ? VK_PIPELINE_STAGE_TRANSFER_BIT :
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable;
        submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &presentSemaphores[imageIndex];
        v = vkQueueSubmit(graphicsQueue, 1, &submit, inFlight);
        if (v != VK_SUCCESS) return drawError("vkQueueSubmit", v, error);
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &presentSemaphores[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        VkPresentTimeGOOGLE stamp{nextPresentId++, 0};
        VkPresentTimesInfoGOOGLE timestamps{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
        if (getTimings) {
            timestamps.swapchainCount = 1;
            timestamps.pTimes = &stamp;
            present.pNext = &timestamps;
        }
        v = vkQueuePresentKHR(graphicsQueue, &present);
        if (v == VK_ERROR_OUT_OF_DATE_KHR) return recreate(error);
        if (v != VK_SUCCESS && v != VK_SUBOPTIMAL_KHR) return drawError("vkQueuePresentKHR", v, error);
        sent = true;
        if (getTimings) {
            pending.push_back({stamp.presentID, generated, sampleNs});
            if (pending.size() > 96) pending.pop_front();
        }
        return true;
    }

    bool sent = false;

private:
    bool drawError(const char* operation, VkResult result, std::string& error) {
        lastResult = result;
        error = vkError(operation, result);
        return false;
    }

    void recordBlit(VkImage destination, const FrameGeneration& synthesis, VkImage source) {
        VkImageMemoryBarrier target{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        target.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        target.image = destination;
        target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &target);
        const VkRect2D area = fitScene(extent);
        if (area.extent.width != extent.width || area.extent.height != extent.height) {
            VkClearColorValue background{{0.024f, 0.032f, 0.052f, 1.0f}};
            vkCmdClearColorImage(commandBuffer, destination, target.newLayout, &background, 1, &target.subresourceRange);
            target.oldLayout = target.newLayout;
            target.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &target);
        }
        synthesis.acquireRead(commandBuffer, source);
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {int32_t(FrameGeneration::extent.width), int32_t(FrameGeneration::extent.height), 1};
        region.dstSubresource = region.srcSubresource;
        region.dstOffsets[0] = {area.offset.x, area.offset.y, 0};
        region.dstOffsets[1] = {area.offset.x + int32_t(area.extent.width),
                                area.offset.y + int32_t(area.extent.height), 1};
        vkCmdBlitImage(commandBuffer, source, VK_IMAGE_LAYOUT_GENERAL, destination, target.newLayout,
                       1, &region, VK_FILTER_NEAREST);
        synthesis.releaseRead(commandBuffer, source);
        target.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        target.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        target.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        target.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &target);
    }

    bool recreate(std::string& error) {
        VkResult idle = vkDeviceWaitIdle(device);
        if (idle != VK_SUCCESS) return drawError("Reconstruir swapchain", idle, error);
        destroySwapchain();
        return createSwapchain(error);
    }

    bool createSwapchain(std::string& error) {
        VkSurfaceCapabilitiesKHR caps{};
        VkResult v = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
        if (v != VK_SUCCESS) { error = vkError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", v); return false; }
        VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                (ahbMode ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0);
        if ((caps.supportedUsageFlags & requiredUsage) != requiredUsage) {
            error = ahbMode ? "Swapchain não suporta COLOR_ATTACHMENT + TRANSFER_DST (AHB)." :
                "Swapchain não suporta VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT.";
            return false;
        }
        uint32_t n = 0;
        v = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, nullptr);
        if (v != VK_SUCCESS || n == 0) { error = "Superfície Vulkan sem formatos disponíveis."; return false; }
        std::vector<VkSurfaceFormatKHR> formats(n);
        v = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, formats.data());
        if (v != VK_SUCCESS) { error = vkError("vkGetPhysicalDeviceSurfaceFormatsKHR", v); return false; }
        VkSurfaceFormatKHR selected = formats[0];
        if (selected.format == VK_FORMAT_UNDEFINED) selected.format = VK_FORMAT_R8G8B8A8_UNORM;
        else {
            for (const auto& f : formats)
                if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { selected = f; break; }
        }
        VkExtent2D size = caps.currentExtent;
        if (size.width == UINT32_MAX) {
            size.width = std::clamp((uint32_t)std::max(1, ANativeWindow_getWidth(window)), caps.minImageExtent.width, caps.maxImageExtent.width);
            size.height = std::clamp((uint32_t)std::max(1, ANativeWindow_getHeight(window)), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!size.width || !size.height) { error = "A superfície ainda não possui dimensões válidas."; return false; }
        VkFormatProperties fgFormat{}, surfaceFormat{};
        vkGetPhysicalDeviceFormatProperties(gpu, FrameGeneration::format, &fgFormat);
        vkGetPhysicalDeviceFormatProperties(gpu, selected.format, &surfaceFormat);
        blit = liveMode && canImport && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
            (fgFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
            (surfaceFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
        if (blit) requiredUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (ahbMode && selected.format != VK_FORMAT_R8G8B8A8_UNORM) {
            error = "Modo AHB exige swapchain VK_FORMAT_R8G8B8A8_UNORM."; return false;
        }
        extent = size;
        uint32_t count = caps.minImageCount + 1;
        if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alpha)) {
            for (auto candidate : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
                if (caps.supportedCompositeAlpha & candidate) { alpha = candidate; break; }
            }
        }
        VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swap.surface = surface;
        swap.minImageCount = count;
        swap.imageFormat = selected.format;
        swap.imageColorSpace = selected.colorSpace;
        swap.imageExtent = extent;
        swap.imageArrayLayers = 1;
        swap.imageUsage = requiredUsage;
        swap.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swap.preTransform = caps.currentTransform;
        swap.compositeAlpha = alpha;
        swap.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swap.clipped = VK_TRUE;
        v = vkCreateSwapchainKHR(device, &swap, nullptr, &swapchain);
        if (v != VK_SUCCESS) { error = vkError("vkCreateSwapchainKHR", v); return false; }
        uint32_t actual = 0;
        v = vkGetSwapchainImagesKHR(device, swapchain, &actual, nullptr);
        if (v != VK_SUCCESS || !actual) { error = "Não foi possível consultar as imagens da swapchain."; return false; }
        images.resize(actual);
        v = vkGetSwapchainImagesKHR(device, swapchain, &actual, images.data());
        if (v != VK_SUCCESS) { error = vkError("vkGetSwapchainImagesKHR", v); return false; }
        VkAttachmentDescription color{};
        color.format = selected.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference attachment{};
        attachment.attachment = 0;
        attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &attachment;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 1;
        pass.pAttachments = &color;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies = &dependency;
        v = vkCreateRenderPass(device, &pass, nullptr, &renderPass);
        if (v != VK_SUCCESS) { error = vkError("vkCreateRenderPass", v); return false; }
        VkSemaphoreCreateInfo perImageSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (size_t i = 0; i < images.size(); ++i) {
            VkSemaphore sem = VK_NULL_HANDLE;
            v = vkCreateSemaphore(device, &perImageSemaphore, nullptr, &sem);
            if (v != VK_SUCCESS) { error = vkError("vkCreateSemaphore (por imagem)", v); return false; }
            presentSemaphores.push_back(sem);
        }
        for (VkImage image : images) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = selected.format;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            VkImageView imageView = VK_NULL_HANDLE;
            v = vkCreateImageView(device, &view, nullptr, &imageView);
            if (v != VK_SUCCESS) { error = vkError("vkCreateImageView", v); return false; }
            imageViews.push_back(imageView);
            VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &imageViews.back();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            v = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);
            if (v != VK_SUCCESS) { error = vkError("vkCreateFramebuffer", v); return false; }
            framebuffers.push_back(framebuffer);
        }
        if (ahbMode) {
            // Two source frames are retained independently for a future interpolation engine.
            // No frames are currently interpolated or passed to Bloodframe.
            for (AhbOffscreen& frame : offscreenFrames) {
                if (!frame.create(gpu, device, extent, selected.format, error)) return false;
            }
        }
        return true;
    }

    void destroySwapchain() {
        if (!device) return;
        for (AhbOffscreen& frame : offscreenFrames) frame.destroy(device);
        for (VkSemaphore s : presentSemaphores) vkDestroySemaphore(device, s, nullptr);
        presentSemaphores.clear();
        for (VkFramebuffer f : framebuffers) vkDestroyFramebuffer(device, f, nullptr);
        framebuffers.clear();
        for (VkImageView v : imageViews) vkDestroyImageView(device, v, nullptr);
        imageViews.clear();
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        images.clear();
        pending.clear();
    }
    void cleanup() {
        if (device) {
            vkDeviceWaitIdle(device);
            destroySwapchain();
            if (inFlight) vkDestroyFence(device, inFlight, nullptr);
            if (imageAvailable) vkDestroySemaphore(device, imageAvailable, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    ANativeWindow* window;
    bool ahbMode;
    bool liveMode;
    bool canImport = false;
    bool blit = false;
    bool displayTiming = false;
    VkResult lastResult = VK_SUCCESS;
    PFN_vkGetPastPresentationTimingGOOGLE getTimings = nullptr;
    uint32_t nextPresentId = 1;
    std::deque<PresentedFrame> pending;
    std::array<AhbOffscreen, 2> offscreenFrames;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> presentSemaphores;
    VkFence inFlight = VK_NULL_HANDLE;
};

struct Metrics {
    uint64_t originals = 0, sent = 0, sentGenerated = 0, shown = 0, shownGenerated = 0, omitted = 0;
    uint64_t waitSamples = 0, latencySamples = 0, displaySamples = 0, lastDisplayNs = 0;
    double batchMs = 0, latencyMs = 0, displayMs = 0;
    void record(const std::vector<PresentedFrame>& observed) {
        for (const auto& frame : observed) {
            ++shown;
            shownGenerated += frame.generated;
            if (frame.sampleNs && frame.presentedNs >= frame.sampleNs) {
                latencyMs += (frame.presentedNs - frame.sampleNs) / 1e6;
                ++latencySamples;
            }
            if (lastDisplayNs && frame.presentedNs > lastDisplayNs) {
                displayMs += (frame.presentedNs - lastDisplayNs) / 1e6;
                ++displaySamples;
            }
            if (frame.presentedNs > lastDisplayNs) lastDisplayNs = frame.presentedNs;
        }
    }

    std::string report(double seconds, bool timing) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(1);
        out << originals / seconds << " FPS originais · ";
        if (timing && shown) out << shown / seconds << " FPS na tela (" << shownGenerated << " FG)";
        else out << sent / seconds << " envios/s (" << sentGenerated << " FG)";
        if (displaySamples) out << "\n" << displayMs / displaySamples << " ms por quadro na tela";
        if (waitSamples) out << "\nLSFG " << batchMs / waitSamples << " ms por lote (inclui espera)";
        if (latencySamples) out << "\nCena → exibição " << latencyMs / latencySamples << " ms";
        if (omitted) out << "\nFG descartados: " << omitted;
        const uint64_t previousDisplay = lastDisplayNs;
        const uint64_t totalOmitted = omitted;
        *this = {};
        lastDisplayNs = previousDisplay;
        omitted = totalOmitted;
        return out.str();
    }
};

class SceneSource {
public:
    SceneSource(JNIEnv* env, jobject object) : env(env), object(object) {
        if (!object) return;
        jclass type = env->GetObjectClass(object);
        render = env->GetMethodID(type, "render", "(J[F)I");
        env->DeleteLocalRef(type);
        if (render) values = env->NewFloatArray(SceneFrame::kRectangles * SceneFrame::kFields);
    }
    ~SceneSource() { if (values) env->DeleteLocalRef(values); }

    bool sample(uint64_t index, uint64_t nowNs, SceneFrame& scene) {
        if (!object) {
            scene = movingTestScene(index);
            return true;
        }
        if (!render || !values) return false;
        jint count = env->CallIntMethod(object, render, static_cast<jlong>(nowNs), values);
        if (env->ExceptionCheck() || count < 0 || count > static_cast<jint>(SceneFrame::kRectangles)) return false;
        scene.count = count;
        env->GetFloatArrayRegion(values, 0, count * SceneFrame::kFields, scene.data.data());
        return !env->ExceptionCheck();
    }

private:
    JNIEnv* env;
    jobject object;
    jmethodID render = nullptr;
    jfloatArray values = nullptr;
};

struct Runner {
    ANativeWindow* window;
    JavaVM* vm;
    jobject game;
    int targetFps;
    bool ahbMode;
    bool live;
    std::string dll;
    std::atomic<bool> fgRequested;
    std::atomic<bool> running{true};
    std::mutex surfaceMutex;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::mutex statusMutex;
    std::string status = "Abrindo Vulkan…";
    std::string diagnostics;

    Runner(ANativeWindow* w, JavaVM* vm, jobject game, int fps, bool ahb, bool live,
           std::string path, bool fg)
        : window(w), vm(vm), game(game), targetFps(fps), ahbMode(ahb), live(live),
          dll(std::move(path)), fgRequested(fg) {}
    ~Runner() { ANativeWindow_release(window); }

    void stop() {
        running = false;
        wake.notify_all();
        // Finish an in-progress WSI call before SurfaceHolder releases the window.
        // Engine compilation, compute, and resource teardown stay on the render thread.
        std::lock_guard<std::mutex> wsi(surfaceMutex);
    }

    void sleepUntil(Clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(wakeMutex);
        wake.wait_until(lock, deadline, [&] { return !running.load(); });
    }
    void setStatus(std::string value, const std::string& report) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status = std::move(value);
        diagnostics = report;
    }
    std::string getStatus(bool detailed) {
        std::lock_guard<std::mutex> lock(statusMutex);
        return detailed && !diagnostics.empty() ? status + "\n\n" + diagnostics : status;
    }

    bool execute(SceneSource& source, const std::string& recovery = "") {
        DemoRenderer renderer(window, ahbMode, live);
        std::string error;
        {
            std::lock_guard<std::mutex> lock(surfaceMutex);
            if (!running) return false;
            if (!renderer.init(error)) {
                setStatus("Vulkan indisponível", error);
                return false;
            }
        }
        std::unique_ptr<FrameGeneration> fg;
        bool failed = !recovery.empty();
        std::string detail = recovery;
        std::string evidence;
        uint32_t age = 0;
        uint32_t validationAttempts = 0;
        bool verified = false;
        const auto period = Ns(1'000'000'000 / targetFps);
        auto next = Clock::now();
        auto measure = next;
        uint64_t sequence = 0;
        Metrics metrics;
        auto render = [&](const SceneFrame* frame, VkImage image, bool generated, uint64_t sampleNs) {
            std::lock_guard<std::mutex> lock(surfaceMutex);
            if (!running) return true;
            if (!renderer.draw(sequence, error, frame, fg.get(), image, generated, sampleNs)) return false;
            if (renderer.sent) {
                ++metrics.sent;
                metrics.sentGenerated += generated;
            } else if (generated) ++metrics.omitted;
            metrics.record(renderer.collectTimings());
            return true;
        };
        auto disable = [&](std::string why) {
            failed = true;
            detail = std::move(why);
            // A submitted swapchain copy may still reference the shared images.
            // FrameGeneration owns the same queue, which is drained at teardown.
            fg.reset();
            verified = false;
            const std::string header = detail.substr(0, detail.find('\n'));
            setStatus("FG indisponível\n" + header, detail);
            __android_log_print(ANDROID_LOG_WARN, "FrameRoomFG", "%s", detail.c_str());
        };
        while (running) {
            const bool requested = live && fgRequested.load();
            if (!requested && (fg || failed)) {
                fg.reset();
                failed = verified = false;
                renderer.forgetTimings();
                age = validationAttempts = 0;
                detail.clear();
                evidence.clear();
                if (recovery.empty()) setStatus("Frame Generation desligado", {});
            }
            if (requested && !fg && !failed) {
                setStatus("Inicializando LSFG 2×…", {});
                if (renderer.supportsFG()) {
                    fg = renderer.createFG();
                    if (fg->start(dll, error)) {
                        detail = fg->backend();
                        setStatus("LSFG iniciado. Verificando quadros…", detail);
                    } else {
                        const bool lost = fg->deviceLost();
                        disable(error);
                        if (lost) return true;
                    }
                } else disable("A GPU ou a superfície não oferece AHB e blit Vulkan para este modo.");
                // Setup can compile shaders; exclude it from frame pacing and FPS.
                next = measure = Clock::now();
                metrics = {};
                renderer.forgetTimings();
            }
            if (!running) break;
            SceneFrame frame;
            const uint64_t sampleNs = monotonicNs();
            if (live && !source.sample(sequence, sampleNs, frame)) {
                setStatus("O jogo não entregou um quadro válido", "O protocolo da cena cooperativa falhou.");
                break;
            }
            ++metrics.originals;
            VkImage original = VK_NULL_HANDLE;
            if (fg) {
                const bool warmup = age < 3;
                const uint32_t current = age++ % 2;
                double elapsed = 0;
                if (!fg->renderInput(frame, current, error) || !fg->generate(warmup, elapsed, error)) {
                    const bool lost = fg->deviceLost();
                    disable(error + "\n" + detail);
                    if (lost) return true;
                } else {
                    original = fg->original(current);
                    if (!warmup) {
                        metrics.batchMs += elapsed;
                        ++metrics.waitSamples;
                        if (!verified) {
                            if (!fg->verify(current, verified, evidence)) {
                                const bool lost = fg->deviceLost();
                                disable(evidence + "\n" + detail);
                                if (lost) return true;
                                original = VK_NULL_HANDLE;
                            } else if (!verified && ++validationAttempts >= 12) {
                                disable("A imagem LSFG não passou na verificação de pixels.\n" + evidence + "\n" + detail);
                                original = VK_NULL_HANDLE;
                            }
                        }
                        if (verified && running && fgRequested) {
                            const auto now = Clock::now();
                            // Prefer an original frame if this batch has already consumed
                            // the next original's slot. No repeat frame is submitted.
                            if (now <= next + period) {
                                if (!render(&frame, fg->generated(), true, sampleNs)) {
                                    setStatus("Falha ao apresentar FG", error + "\n" + detail);
                                    return renderer.deviceLost();
                                }
                                if (renderer.sent)
                                    sleepUntil((std::min)(next + period, Clock::now() + period / 2));
                            } else ++metrics.omitted;
                        } else ++metrics.omitted;
                    }
                }
            }
            if (!render(live ? &frame : nullptr, original, false, sampleNs)) {
                setStatus("Erro na apresentação Vulkan", error + "\n" + detail);
                return renderer.deviceLost();
            }
            ++sequence;
            auto now = Clock::now();
            const double seconds = std::chrono::duration<double>(now - measure).count();
            if (seconds >= 1.0) {
                std::string state;
                if (failed) state = "FG indisponível · Vulkan direto";
                else if (!fg) state = live ? "FG desligado" : (ahbMode ? "Vulkan AHB duplo" : "Vulkan direto");
                else if (metrics.shownGenerated) state = "FG 2× confirmado pela tela";
                else if (metrics.sentGenerated) state = "FG 2× enviado à tela";
                else if (verified) state = "FG sem apresentação neste intervalo";
                else state = "LSFG 2× · verificando imagem";
                const auto rates = metrics.report(seconds, renderer.hasDisplayTiming());
                setStatus(state + "\n" + rates, evidence + (evidence.empty() ? "" : "\n") + detail +
                    "\nLatência da cena: da amostra do jogo ao horário de exibição informado pelo Vulkan. " +
                    "Sem VK_GOOGLE_display_timing, o contador usa envios à swapchain.");
                measure = now;
            }
            next += period;
            // Skip expired deadlines rather than submitting several originals at once.
            if (next < now) next = now + period;
            sleepUntil(next);
        }
        return false;
    }

    void run() {
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        if (running) {
            SceneSource source(env, game);
            if (execute(source) && running) {
                __android_log_print(ANDROID_LOG_WARN, "FrameRoomFG", "Dispositivo Vulkan perdido; retomando em modo direto");
                execute(source, "Dispositivo Vulkan perdido. Renderização direta retomada.");
            }
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
        if (game) env->DeleteGlobalRef(game);
        vm->DetachCurrentThread();
    }
};

std::mutex globalMutex;
std::mutex renderMutex;
std::shared_ptr<Runner> runner;
jlong session = 0;
}

extern "C" JNIEXPORT jlong JNICALL
Java_dev_frameroom_VulkanPlayerView_nativeStart(JNIEnv* env, jclass, jobject surface, jint fps,
        jboolean ahb, jboolean live, jstring dll, jboolean fg, jobject game) {
    std::lock_guard<std::mutex> lock(globalMutex);
    if (runner) runner->stop();
    runner.reset();
    if (!surface) return 0;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return 0;
    std::string path;
    if (dll) {
        const char* raw = env->GetStringUTFChars(dll, nullptr);
        if (raw) {
            path = raw;
            env->ReleaseStringUTFChars(dll, raw);
        }
    }
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    runner = std::make_shared<Runner>(window, vm, game ? env->NewGlobalRef(game) : nullptr,
        std::clamp(static_cast<int>(fps), 15, 60), ahb, live, std::move(path), fg);
    std::thread([current = runner] {
        std::lock_guard<std::mutex> lock(renderMutex);
        current->run();
    }).detach();
    return ++session;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_frameroom_VulkanPlayerView_nativeStop(JNIEnv*, jclass, jlong id) {
    std::lock_guard<std::mutex> lock(globalMutex);
    if (runner && id == session) {
        runner->stop();
        runner.reset();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_dev_frameroom_VulkanPlayerView_nativeSetFrameGeneration(JNIEnv*, jclass, jlong id, jboolean enabled) {
    std::lock_guard<std::mutex> lock(globalMutex);
    if (runner && id == session) runner->fgRequested = enabled;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_VulkanPlayerView_nativeStatus(JNIEnv* env, jclass, jlong id, jboolean detailed) {
    std::lock_guard<std::mutex> lock(globalMutex);
    const std::string value = runner && id == session ? runner->getStatus(detailed) : "Aguardando superfície Vulkan…";
    return env->NewStringUTF(value.c_str());
}
