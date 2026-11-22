#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#include "ahb_offscreen.h"
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

namespace {
using Clock = std::chrono::steady_clock;

std::string vkError(const char* stage, VkResult result) {
    return std::string(stage) + " (VkResult=" + std::to_string((int)result) + ")";
}

class DemoRenderer {
public:
    explicit DemoRenderer(ANativeWindow* nativeWindow, bool useAhb) : window(nativeWindow), ahbMode(useAhb) {}
    ~DemoRenderer() { cleanup(); }

    bool init(std::string& error) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "FrameRoom Native Vulkan Demo";
        app.apiVersion = ahbMode ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
        const char* instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        instanceInfo.enabledExtensionCount = 2;
        instanceInfo.ppEnabledExtensionNames = instanceExtensions;
        VkResult v = vkCreateInstance(&instanceInfo, nullptr, &instance);
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
                    bool swapchain = false;
                    for (const auto& e : exts) if (std::string(e.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) swapchain = true;
                    if (!swapchain) continue;
                    if (ahbMode) {
                        VkPhysicalDeviceProperties props{};
                        vkGetPhysicalDeviceProperties(candidate, &props);
                        if (VK_VERSION_MAJOR(props.apiVersion) < 1 ||
                            (VK_VERSION_MAJOR(props.apiVersion) == 1 && VK_VERSION_MINOR(props.apiVersion) < 1))
                            continue;
                        bool ahbExtension = false;
                        for (const auto& extension : exts) {
                            if (std::string(extension.extensionName) ==
                                VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) {
                                ahbExtension = true; break;
                            }
                        }
                        if (!ahbExtension) continue;
                    }
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
        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME
        };
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = ahbMode ? 2 : 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        v = vkCreateDevice(gpu, &deviceInfo, nullptr, &device);
        if (v != VK_SUCCESS) { error = vkError("vkCreateDevice", v); return false; }
        vkGetDeviceQueue(device, queueFamily, 0, &graphicsQueue);
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

    bool draw(uint64_t index, std::string& error) {
        VkResult v = vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
        if (v != VK_SUCCESS) { error = vkError("vkWaitForFences", v); return false; }
        uint32_t imageIndex = 0;
        v = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (v == VK_ERROR_OUT_OF_DATE_KHR) return recreate(error);
        if (v != VK_SUCCESS && v != VK_SUBOPTIMAL_KHR) { error = vkError("vkAcquireNextImageKHR", v); return false; }
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
        if (ahbMode) {
            AhbOffscreen& current = offscreenFrames[index % offscreenFrames.size()];
            if (!current.ready()) { error = "Quadro AHB não inicializado."; return false; }
            current.record(commandBuffer, images[imageIndex], index);
        } else {
        vkCmdBeginRenderPass(commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
        // A moving rectangle verifies actual Vulkan GPU presentation, not LSFG.
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
        VkPipelineStageFlags stage = ahbMode ? VK_PIPELINE_STAGE_TRANSFER_BIT :
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
        if (v != VK_SUCCESS) { error = vkError("vkQueueSubmit", v); return false; }
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &presentSemaphores[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        v = vkQueuePresentKHR(graphicsQueue, &present);
        if (v == VK_ERROR_OUT_OF_DATE_KHR || v == VK_SUBOPTIMAL_KHR) return recreate(error);
        if (v != VK_SUCCESS) { error = vkError("vkQueuePresentKHR", v); return false; }
        return true;
    }

private:
    bool recreate(std::string& error) {
        vkDeviceWaitIdle(device);
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
    bool ahbMode = false;
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

struct Runner {
    explicit Runner(ANativeWindow* w, int fps, bool ahb) : window(w), targetFps(fps), ahbMode(ahb) {}
    ~Runner() { if (window) ANativeWindow_release(window); }
    ANativeWindow* window;
    int targetFps;
    bool ahbMode;
    std::atomic<bool> running{true};
    std::mutex mu;
    std::string status = "Iniciando o renderizador Vulkan...";
    std::thread thread;
    void setStatus(const std::string& value) { std::lock_guard<std::mutex> lock(mu); status = value; }
    std::string getStatus() { std::lock_guard<std::mutex> lock(mu); return status; }
    void execute() {
        DemoRenderer renderer(window, ahbMode);
        std::string error;
        if (!renderer.init(error)) { setStatus("ERRO: " + error); running = false; return; }
        auto last = Clock::now();
        uint64_t frames = 0, total = 0;
        double measuredFps = 0;
        const auto frameTime = std::chrono::nanoseconds((int64_t)(1000000000.0 / targetFps));
        auto next = Clock::now();
        while (running) {
            if (!renderer.draw(total++, error)) { setStatus("ERRO: " + error); running = false; break; }
            frames++;
            auto now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - last).count();
            if (elapsed >= 1) {
                measuredFps = frames / elapsed;
                setStatus(std::string(ahbMode ? "Vulkan AHB duplo (2 quadros) → tela" : "Vulkan direto → tela") +
                    " • " + std::to_string((int)(measuredFps + 0.5)) +
                    " envios/s (não é FPS interpolado) • FG DESLIGADO");
                frames = 0;
                last = now;
            }
            next += frameTime;
            if (next < now) next = now;
            std::this_thread::sleep_until(next);
        }
    }
};
std::mutex globalMutex;
std::unique_ptr<Runner> runner;
void stopLocked() {
    if (!runner) return;
    runner->running = false;
    if (runner->thread.joinable()) runner->thread.join();
    runner.reset();
}
}

extern "C" JNIEXPORT void JNICALL
Java_dev_frameroom_VulkanDemoActivity_nativeStart(JNIEnv* env, jclass, jobject surface, jint fps, jboolean ahb) {
    std::lock_guard<std::mutex> lock(globalMutex);
    stopLocked();
    if (!surface) return;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return;
    runner = std::make_unique<Runner>(window, fps == 60 ? 60 : 30, ahb == JNI_TRUE);
    Runner* current = runner.get();
    current->thread = std::thread([current] { current->execute(); });
}

extern "C" JNIEXPORT void JNICALL
Java_dev_frameroom_VulkanDemoActivity_nativeStop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(globalMutex);
    stopLocked();
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_VulkanDemoActivity_nativeStatus(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(globalMutex);
    std::string value = runner ? runner->getStatus() : "Renderizador parado.";
    return env->NewStringUTF(value.c_str());
}
