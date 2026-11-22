#include "ahb_offscreen.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

namespace {
std::string fail(const char* step, VkResult code) {
    return std::string(step) + " falhou: VkResult=" + std::to_string(static_cast<int>(code));
}
}

bool AhbOffscreen::create(VkPhysicalDevice physical, VkDevice device,
                          VkExtent2D dimensions, VkFormat format, std::string& error) {
    if (ready() || buffer_) { error = "AHB já inicializado."; return false; }
    if (format != VK_FORMAT_R8G8B8A8_UNORM || dimensions.width == 0 || dimensions.height == 0) {
        error = "O teste AHB exige swapchain RGBA8 e dimensões não nulas.";
        return false;
    }
    dimensions_ = dimensions;
    AHardwareBuffer_Desc description{};
    description.width = dimensions.width;
    description.height = dimensions.height;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                        AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    int allocationResult = AHardwareBuffer_allocate(&description, &buffer_);
    if (allocationResult != 0 || !buffer_) {
        error = "AHardwareBuffer_allocate falhou: código " + std::to_string(allocationResult);
        destroy(device);
        return false;
    }

    auto getProperties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (!getProperties) {
        error = "vkGetAndroidHardwareBufferPropertiesANDROID ausente no driver.";
        destroy(device);
        return false;
    }
    VkAndroidHardwareBufferFormatPropertiesANDROID ahbFormat{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ahbProperties{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    ahbProperties.pNext = &ahbFormat;
    VkResult result = getProperties(device, buffer_, &ahbProperties);
    if (result != VK_SUCCESS) { error = fail("Consultar propriedades AHB", result); destroy(device); return false; }
    if (ahbFormat.format != format) {
        error = "AHardwareBuffer tem formato Vulkan incompatível com RGBA8 (formato " +
                std::to_string(static_cast<int>(ahbFormat.format)) + ").";
        destroy(device);
        return false;
    }

    // Explicitly query external-memory import support for our exact image usages.
    VkPhysicalDeviceExternalImageFormatInfo externalQuery{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    externalQuery.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkPhysicalDeviceImageFormatInfo2 imageQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    imageQuery.pNext = &externalQuery;
    imageQuery.format = format;
    imageQuery.type = VK_IMAGE_TYPE_2D;
    imageQuery.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageQuery.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    VkExternalImageFormatProperties externalProperties{
        VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 imageProperties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    imageProperties.pNext = &externalProperties;
    result = vkGetPhysicalDeviceImageFormatProperties2(physical, &imageQuery, &imageProperties);
    if (result != VK_SUCCESS || !(externalProperties.externalMemoryProperties.externalMemoryFeatures &
                                  VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        error = "Driver não confirmou importação Vulkan AHB RGBA8 para transferências (VkResult=" +
                std::to_string(static_cast<int>(result)) + ").";
        destroy(device);
        return false;
    }

    VkExternalMemoryImageCreateInfo externalImage{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    externalImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.pNext = &externalImage;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {dimensions.width, dimensions.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = imageQuery.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    result = vkCreateImage(device, &info, nullptr, &image_);
    if (result != VK_SUCCESS) { error = fail("vkCreateImage (AHB)", result); destroy(device); return false; }

    // AHB images cannot be queried with vkGetImageMemoryRequirements before binding.
    uint32_t memoryTypes = ahbProperties.memoryTypeBits;
    if (!memoryTypes) {
        error = "Nenhum tipo de memória Vulkan compatível com o AHB.";
        destroy(device);
        return false;
    }
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    uint32_t chosenIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((memoryTypes & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            chosenIndex = i; break;
        }
    }
    if (chosenIndex == UINT32_MAX) {
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if (memoryTypes & (1u << i)) { chosenIndex = i; break; }
    }
    if (chosenIndex == UINT32_MAX) {
        error = "Não foi possível escolher memória para AHB.";
        destroy(device);
        return false;
    }
    VkImportAndroidHardwareBufferInfoANDROID import{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import.buffer = buffer_;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext = &import;
    dedicated.image = image_;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &dedicated;
    allocation.allocationSize = ahbProperties.allocationSize;
    allocation.memoryTypeIndex = chosenIndex;
    result = vkAllocateMemory(device, &allocation, nullptr, &memory_);
    if (result != VK_SUCCESS) { error = fail("vkAllocateMemory (import AHB)", result); destroy(device); return false; }
    result = vkBindImageMemory(device, image_, memory_, 0);
    if (result != VK_SUCCESS) { error = fail("vkBindImageMemory (AHB)", result); destroy(device); return false; }
    // A small, host-filled staging buffer draws a moving purple bar on the AHB.
    // This creates real temporal motion for later frame-generation integration tests.
    barWidth_ = std::min(112u, dimensions.width);
    barHeight_ = std::min(84u, dimensions.height);
    VkDeviceSize bytes = static_cast<VkDeviceSize>(barWidth_) * barHeight_ * 4;
    VkBufferCreateInfo bar{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bar.size = bytes;
    bar.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bar.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(device, &bar, nullptr, &barBuffer_);
    if (result != VK_SUCCESS) { error = fail("vkCreateBuffer (barra)", result); destroy(device); return false; }
    VkMemoryRequirements barRequirements{};
    vkGetBufferMemoryRequirements(device, barBuffer_, &barRequirements);
    uint32_t barIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((barRequirements.memoryTypeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            barIndex = i; break;
        }
    }
    if (barIndex == UINT32_MAX) {
        error = "Driver não fornece staging buffer HOST_VISIBLE + HOST_COHERENT.";
        destroy(device);
        return false;
    }
    VkMemoryAllocateInfo barAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    barAllocation.allocationSize = barRequirements.size;
    barAllocation.memoryTypeIndex = barIndex;
    result = vkAllocateMemory(device, &barAllocation, nullptr, &barMemory_);
    if (result != VK_SUCCESS) { error = fail("vkAllocateMemory (barra)", result); destroy(device); return false; }
    result = vkBindBufferMemory(device, barBuffer_, barMemory_, 0);
    if (result != VK_SUCCESS) { error = fail("vkBindBufferMemory (barra)", result); destroy(device); return false; }
    void* mapped = nullptr;
    result = vkMapMemory(device, barMemory_, 0, bytes, 0, &mapped);
    if (result != VK_SUCCESS || !mapped) {
        error = fail("vkMapMemory (barra)", result);
        destroy(device);
        return false;
    }
    auto* rgba = static_cast<uint8_t*>(mapped);
    for (VkDeviceSize k = 0; k < bytes; k += 4) {
        rgba[k] = 175;
        rgba[k + 1] = 110;
        rgba[k + 2] = 255;
        rgba[k + 3] = 255;
    }
    vkUnmapMemory(device, barMemory_);
    firstUse_ = true;
    return true;
}

void AhbOffscreen::record(VkCommandBuffer command, VkImage swapchainImage, uint64_t frameIndex) {
    // The AHB stays in the same Vulkan queue and never leaves this process in R0.3.
    VkImageMemoryBarrier input{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    input.srcAccessMask = firstUse_ ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    input.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    input.oldLayout = firstUse_ ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    input.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    input.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    input.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    input.image = image_;
    input.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    input.subresourceRange.levelCount = 1;
    input.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, firstUse_ ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &input);

    float phase = static_cast<float>((std::sin(frameIndex * 0.055) + 1.0) * 0.5);
    VkClearColorValue tint{};
    tint.float32[0] = 0.08f + phase * 0.49f;
    tint.float32[1] = 0.05f + (1.0f - phase) * 0.08f;
    tint.float32[2] = 0.26f + (1.0f - phase) * 0.43f;
    tint.float32[3] = 1.0f;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(command, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &tint, 1, &range);
    VkImageMemoryBarrier afterClear = input;
    afterClear.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    afterClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    afterClear.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    afterClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &afterClear);
    VkBufferImageCopy barCopy{};
    barCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barCopy.imageSubresource.layerCount = 1;
    uint32_t travel = dimensions_.width - barWidth_ + 1;
    barCopy.imageOffset = {static_cast<int32_t>((frameIndex * 6) % travel),
                           static_cast<int32_t>(dimensions_.height / 3), 0};
    if (static_cast<uint32_t>(barCopy.imageOffset.y) + barHeight_ > dimensions_.height)
        barCopy.imageOffset.y = static_cast<int32_t>(dimensions_.height - barHeight_);
    barCopy.imageExtent = {barWidth_, barHeight_, 1};
    vkCmdCopyBufferToImage(command, barBuffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &barCopy);

    VkImageMemoryBarrier toRead = input;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageMemoryBarrier toSwap = input;
    toSwap.srcAccessMask = 0;
    toSwap.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSwap.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Acquired image contents are discarded.
    toSwap.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSwap.image = swapchainImage;
    VkImageMemoryBarrier start[2] = {toRead, toSwap};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, start);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {dimensions_.width, dimensions_.height, 1};
    vkCmdCopyImage(command, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier present = toSwap;
    present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    present.dstAccessMask = 0;
    present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &present);
    firstUse_ = false;
}

void AhbOffscreen::destroy(VkDevice device) {
    if (device && barBuffer_) vkDestroyBuffer(device, barBuffer_, nullptr);
    if (device && barMemory_) vkFreeMemory(device, barMemory_, nullptr);
    barBuffer_ = VK_NULL_HANDLE;
    barMemory_ = VK_NULL_HANDLE;
    barWidth_ = barHeight_ = 0;
    if (device && image_) vkDestroyImage(device, image_, nullptr);
    if (device && memory_) vkFreeMemory(device, memory_, nullptr);
    if (buffer_) AHardwareBuffer_release(buffer_);
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    buffer_ = nullptr;
    firstUse_ = true;
}
