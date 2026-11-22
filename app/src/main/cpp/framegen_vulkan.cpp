#include "framegen_vulkan.h"
#include "frame_validation.h"
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <poll.h>

namespace {
constexpr uint64_t gpuTimeoutNs = 500'000'000;
constexpr VkDeviceSize pixels = FrameGeneration::extent.width * FrameGeneration::extent.height;
constexpr VkDeviceSize imageBytes = pixels * 4;
constexpr VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

void barrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout,
             VkPipelineStageFlags source, VkPipelineStageFlags destination,
             VkAccessFlags reads, VkAccessFlags writes, uint32_t from, uint32_t to) {
    VkImageMemoryBarrier transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    transition.oldLayout = oldLayout;
    transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    transition.srcAccessMask = reads;
    transition.dstAccessMask = writes;
    transition.srcQueueFamilyIndex = from;
    transition.dstQueueFamilyIndex = to;
    transition.image = image;
    transition.subresourceRange = colorRange;
    vkCmdPipelineBarrier(command, source, destination, 0, 0, nullptr, 0, nullptr, 1, &transition);
}

struct SyncFiles {
    BbLsfgPresent result{{-1, -1, -1}, -1, 0};
    ~SyncFiles() {
        for (int fd : result.out_ready_fd) if (fd >= 0) close(fd);
        if (result.batch_complete_fd >= 0) close(result.batch_complete_fd);
    }
};
}

struct FrameGeneration::Api {
    // Bloodframe may retain device objects after a bounded teardown timeout.
    // Keep its code mapped for the render process lifetime.
    void* module = dlopen("libbloodframe.so", RTLD_NOW | RTLD_LOCAL);
    decltype(&bb_lsfg_init) init = load<decltype(init)>("bb_lsfg_init");
    decltype(&bb_lsfg_create_context) create = load<decltype(create)>("bb_lsfg_create_context");
    decltype(&bb_lsfg_present) present = load<decltype(present)>("bb_lsfg_present");
    decltype(&bb_lsfg_wait) wait = load<decltype(wait)>("bb_lsfg_wait");
    decltype(&bb_lsfg_destroy_context) destroy = load<decltype(destroy)>("bb_lsfg_destroy_context");
    decltype(&bb_lsfg_shutdown) shutdown = load<decltype(shutdown)>("bb_lsfg_shutdown");
    decltype(&bb_lsfg_diagnostics) diagnostics = load<decltype(diagnostics)>("bb_lsfg_diagnostics");

    template <typename T> T load(const char* name) const {
        return module ? reinterpret_cast<T>(dlsym(module, name)) : nullptr;
    }
    bool ready() const { return init && create && present && wait && destroy && shutdown; }
};

FrameGeneration::FrameGeneration(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family)
    : physical(physical), device(device), queue(queue), family(family) {}

FrameGeneration::~FrameGeneration() {
    if (submitted || context >= 0) vkQueueWaitIdle(queue);
    if (context >= 0) api->destroy(context);
    if (initialized) api->shutdown();
    inputs[0].destroy(device);
    inputs[1].destroy(device);
    output.destroy(device);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    if (readback) vkDestroyBuffer(device, readback, nullptr);
    if (readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
}

void FrameGeneration::SharedImage::destroy(VkDevice device) {
    if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
    if (view) vkDestroyImageView(device, view, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
    if (buffer) AHardwareBuffer_release(buffer);
}

bool FrameGeneration::check(VkResult result, const char* operation, std::string& error) {
    if (result == VK_SUCCESS) return true;
    if (result == VK_ERROR_DEVICE_LOST) lost = true;
    error = std::string(operation) + " (VkResult=" + std::to_string(result) + ")";
    return false;
}

uint32_t FrameGeneration::memoryType(uint32_t mask, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}

bool FrameGeneration::prepareRenderPass(std::string& error) {
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    return check(vkCreateRenderPass(device, &info, nullptr, &renderPass), "Render pass LSFG", error);
}

bool FrameGeneration::createImage(SharedImage& target, bool isOutput, uint64_t usage, std::string& error) {
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!isOutput) imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    query.pNext = &external;
    query.format = format;
    query.type = VK_IMAGE_TYPE_2D;
    query.tiling = VK_IMAGE_TILING_OPTIMAL;
    query.usage = imageUsage;
    VkExternalImageFormatProperties externalProperties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 support{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    support.pNext = &externalProperties;
    const char* role = isOutput ? "saída" : "entrada";
    const auto queryResult = vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &support);
    if (!check(queryResult, role, error)) return false;
    if (!(externalProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        error = std::string("O driver recusou a importação Vulkan de ") + role;
        return false;
    }

    AHardwareBuffer_Desc desc{};
    desc.width = extent.width;
    desc.height = extent.height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = usage;
    const int allocation = AHardwareBuffer_allocate(&desc, &target.buffer);
    if (allocation != 0 || !target.buffer) {
        error = std::string("Alocação AHB ") + role + " 640×360: rc=" + std::to_string(allocation) +
            " usage=" + std::to_string(usage) + " isSupported=" + std::to_string(AHardwareBuffer_isSupported(&desc));
        return false;
    }

    auto getProperties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (!getProperties) {
        error = "A importação AHB Vulkan está indisponível.";
        return false;
    }
    VkAndroidHardwareBufferFormatPropertiesANDROID formatInfo{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID properties{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    properties.pNext = &formatInfo;
    if (!check(getProperties(device, target.buffer, &properties), "Propriedades AHB", error)) return false;
    if (formatInfo.format != format) {
        error = std::string("AHB sem formato RGBA8 compatível na ") + role;
        return false;
    }

    VkExternalMemoryImageCreateInfo handles{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    handles.handleTypes = external.handleType;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.pNext = &handles;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = imageUsage;
    if (!check(vkCreateImage(device, &image, nullptr, &target.image), "Criar VkImage AHB", error)) return false;
    // For a dedicated AHB import the requirements come from buffer properties.
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = target.image;
    VkImportAndroidHardwareBufferInfoANDROID import{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import.pNext = &dedicated;
    import.buffer = target.buffer;
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.pNext = &import;
    memory.allocationSize = properties.allocationSize;
    memory.memoryTypeIndex = memoryType(properties.memoryTypeBits, 0);
    if (memory.memoryTypeIndex == UINT32_MAX) {
        error = "AHB sem tipo de memória Vulkan.";
        return false;
    }
    if (!check(vkAllocateMemory(device, &memory, nullptr, &target.memory), "Importar memória AHB", error) ||
        !check(vkBindImageMemory(device, target.image, target.memory, 0), "Vincular VkImage AHB", error)) return false;
    if (isOutput) return true;

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = target.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = colorRange;
    if (!check(vkCreateImageView(device, &view, nullptr, &target.view), "View da entrada AHB", error)) return false;
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = renderPass;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &target.view;
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1;
    return check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target.framebuffer), "Framebuffer AHB", error);
}

bool FrameGeneration::start(const std::string& dll, std::string& error) {
    static Api library;
    api = &library;
    if (!api->ready()) {
        error = "O APK não contém a biblioteca Bloodframe com a API esperada.";
        return false;
    }
    if (dll.empty()) {
        error = "Importe a Lossless.dll na aba Motor.";
        return false;
    }
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(physical, &properties);
    BbLsfgConfig config{};
    config.vk_get_instance_proc_addr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
    config.dll_path = dll.c_str();
    std::memcpy(config.device_uuid, ids.deviceUUID, VK_UUID_SIZE);
    std::memcpy(config.driver_uuid, ids.driverUUID, VK_UUID_SIZE);
    config.flow_scale = .5f;
    config.performance = 1;
    config.generation_count = 1;
    config.shared_format = format;
    // FP32 is the tested initialization path on the user's Adreno.
    BbLsfgInfo transport{};
    char message[1024]{};
    if (api->init(&config, &transport, message, sizeof(message)) != 0) {
        error = std::string("Inicialização LSFG: ") + message;
        return false;
    }
    initialized = true;
    if (!prepareRenderPass(error) ||
        !createImage(inputs[0], false, transport.input_ahb_usage, error) ||
        !createImage(inputs[1], false, transport.input_ahb_usage, error) ||
        !createImage(output, true, transport.output_ahb_usage, error)) return false;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    if (!check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "Pool de comandos AHB", error)) return false;
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!check(vkAllocateCommandBuffers(device, &commandInfo, &commands), "Comandos AHB", error) ||
        !check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Fence AHB", error) ||
        !begin(error)) return false;
    for (VkImage image : {inputs[0].image, inputs[1].image, output.image}) {
        barrier(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED);
        VkClearColorValue color{{0.04f, 0.06f, 0.10f, 1.0f}};
        if (image == output.image) color = {{1.0f, 0.0f, 1.0f, 1.0f}};
        vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &colorRange);
        barrier(commands, image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0, family, VK_QUEUE_FAMILY_EXTERNAL);
    }
    if (!submit(error)) return false;
    AHardwareBuffer* outputs[] = {output.buffer};
    context = api->create(inputs[0].buffer, inputs[1].buffer, outputs, 1,
                          extent.width, extent.height, format, message, sizeof(message));
    if (context < 0) {
        error = std::string("Contexto LSFG: ") + message;
        return false;
    }
    if (api->diagnostics) api->diagnostics(message, sizeof(message));
    description = std::string(message) + " | usage in=" + std::to_string(transport.input_ahb_usage) +
        " out=" + std::to_string(transport.output_ahb_usage);
    __android_log_print(ANDROID_LOG_INFO, "FrameRoomFG", "%s", description.c_str());
    return true;
}

bool FrameGeneration::begin(std::string& error) {
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return check(vkResetCommandBuffer(commands, 0), "Reset de comandos AHB", error) &&
           check(vkBeginCommandBuffer(commands, &info), "Gravar comandos AHB", error);
}

bool FrameGeneration::submit(std::string& error) {
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands;
    if (!check(vkEndCommandBuffer(commands), "Fechar comandos AHB", error) ||
        !check(vkResetFences(device, 1, &fence), "Reset de fence AHB", error) ||
        !check(vkQueueSubmit(queue, 1, &info, fence), "Executar comandos AHB", error)) return false;
    submitted = true;
    if (!check(vkWaitForFences(device, 1, &fence, VK_TRUE, gpuTimeoutNs), "Aguardar Vulkan AHB", error)) return false;
    submitted = false;
    return true;
}

bool FrameGeneration::renderInput(const SceneFrame& scene, uint32_t input, std::string& error) {
    if (!begin(error)) return false;
    auto& target = inputs.at(input);
    barrier(commands, target.image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_QUEUE_FAMILY_EXTERNAL, family);
    VkClearValue background{{{0.04f, 0.06f, 0.10f, 1.0f}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = renderPass;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = extent;
    pass.clearValueCount = 1;
    pass.pClearValues = &background;
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    drawScene(commands, scene, {{0, 0}, extent});
    vkCmdEndRenderPass(commands);
    barrier(commands, target.image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
            family, VK_QUEUE_FAMILY_EXTERNAL);
    return submit(error);
}

bool FrameGeneration::generate(bool warmup, double& elapsedMs, std::string& error) {
    const auto start = std::chrono::steady_clock::now();
    SyncFiles sync;
    char message[768]{};
    // The Vulkan producer was fenced on the host before calling LSFG. Wait on
    // the batch fence before reusing either input or reading the generated image.
    if (api->present(context, -1, warmup ? 0 : 1, &sync.result, message, sizeof(message)) != 0) {
        error = std::string("Geração LSFG: ") + message;
        return false;
    }
    if (sync.result.gpu_synced && sync.result.batch_complete_fd >= 0) {
        pollfd ready{sync.result.batch_complete_fd, POLLIN, 0};
        const int outcome = poll(&ready, 1, static_cast<int>(gpuTimeoutNs / 1'000'000));
        if (outcome == 0) {
            error = "O sync FD da Bloodframe não sinalizou dentro do prazo.";
            return false;
        }
        // If poll is interrupted or returns a fence error, the Vulkan fence
        // below remains the authority on completion or device loss.
    }
    if (api->wait(context, gpuTimeoutNs) != 0) {
        error = "A geração LSFG excedeu o tempo de espera ou perdeu o dispositivo Vulkan.";
        return false;
    }
    elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return true;
}

void FrameGeneration::acquireRead(VkCommandBuffer cmd, VkImage image) const {
    barrier(cmd, image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_READ_BIT, VK_QUEUE_FAMILY_EXTERNAL, family);
}

void FrameGeneration::releaseRead(VkCommandBuffer cmd, VkImage image) const {
    barrier(cmd, image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, 0, family, VK_QUEUE_FAMILY_EXTERNAL);
}

bool FrameGeneration::prepareReadback(std::string& error) {
    if (readback) return true;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = 3 * imageBytes;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!check(vkCreateBuffer(device, &buffer, nullptr, &readback), "Criar buffer de verificação", error)) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, readback, &requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory.memoryTypeIndex == UINT32_MAX) {
        error = "Sem memória para verificar a imagem gerada.";
        return false;
    }
    return check(vkAllocateMemory(device, &memory, nullptr, &readbackMemory), "Memória de verificação", error) &&
           check(vkBindBufferMemory(device, readback, readbackMemory, 0), "Vincular verificação", error);
}

bool FrameGeneration::verify(uint32_t current, bool& usable, std::string& diagnostic) {
    if (!prepareReadback(diagnostic) || !begin(diagnostic)) return false;
    std::array<VkImage, 3> images{original(1u - current), original(current), generated()};
    for (size_t i = 0; i < images.size(); ++i) {
        acquireRead(commands, images[i]);
        VkBufferImageCopy region{};
        region.bufferOffset = i * imageBytes;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(commands, images[i], VK_IMAGE_LAYOUT_GENERAL, readback, 1, &region);
        releaseRead(commands, images[i]);
    }
    VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &toHost, 0, nullptr, 0, nullptr);
    if (!submit(diagnostic)) return false;
    void* mapped = nullptr;
    if (!check(vkMapMemory(device, readbackMemory, 0, 3 * imageBytes, 0, &mapped), "Ler pixels da GPU", diagnostic)) return false;
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    const FrameEvidence evidence = inspectFrames(bytes, bytes + imageBytes, bytes + 2 * imageBytes, pixels);
    vkUnmapMemory(device, readbackMemory);
    usable = evidence.usable(pixels);
    diagnostic = "640×360 | movimento A/B=" + std::to_string(evidence.changedInput) +
        " ΔA=" + std::to_string(evidence.differsFromA) + " ΔB=" + std::to_string(evidence.differsFromB) +
        " contraste=" + std::to_string(evidence.contrast) + " sentinela=" + std::to_string(evidence.unwritten);
    __android_log_print(ANDROID_LOG_INFO, "FrameRoomFG", "pixels %s: %s",
                        usable ? "verificados" : "rejeitados", diagnostic.c_str());
    if (usable) {
        vkDestroyBuffer(device, readback, nullptr);
        vkFreeMemory(device, readbackMemory, nullptr);
        readback = VK_NULL_HANDLE;
        readbackMemory = VK_NULL_HANDLE;
    }
    return true;
}
