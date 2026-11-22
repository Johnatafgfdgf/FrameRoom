#include <jni.h>
#include <android/hardware_buffer.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

static bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    for (const auto& extension : extensions)
        if (strcmp(extension.extensionName, name) == 0) return true;
    return false;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_NativeProbe_deviceReport(JNIEnv* env, jclass) {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "FrameRoom GPU Probe";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult status = vkCreateInstance(&create, nullptr, &instance);
    if (status != VK_SUCCESS) {
        std::string error = "Falha vkCreateInstance: " + std::to_string(status);
        return env->NewStringUTF(error.c_str());
    }
    std::string report;
    uint32_t count = 0;
    status = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (status != VK_SUCCESS || count == 0) {
        report = "Vulkan não encontrou GPU. Código: " + std::to_string(status);
    } else {
        std::vector<VkPhysicalDevice> devices(count);
        status = vkEnumeratePhysicalDevices(instance, &count, devices.data());
        if (status != VK_SUCCESS) {
            report = "Falha ao enumerar GPU: " + std::to_string(status);
        } else {
            for (uint32_t i=0; i<count; ++i) {
                VkPhysicalDeviceProperties props = {};
                vkGetPhysicalDeviceProperties(devices[i], &props);
                uint32_t extCount = 0;
                std::vector<VkExtensionProperties> exts;
                if (vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, nullptr) == VK_SUCCESS) {
                    exts.resize(extCount);
                    if (extCount && vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, exts.data()) != VK_SUCCESS) exts.clear();
                }
                report += std::string("GPU: ") + props.deviceName + "\n";
                report += "Vulkan: " + std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." +
                        std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." +
                        std::to_string(VK_VERSION_PATCH(props.apiVersion)) + "\n";
                report += "AHardwareBuffer externo: ";
                report += hasExtension(exts, "VK_ANDROID_external_memory_android_hardware_buffer") ? "sim\n" : "não detectado\n";
                report += "robustness2: ";
                report += hasExtension(exts, "VK_EXT_robustness2") ? "sim\n" : "não detectado\n";
                // Querying features2 requires Vulkan 1.1+ at the instance level. The
                // first prototype intentionally reports only extensions, not FP16.
                report += "FP16: não testado nesta versão\n";
            }
        }
    }
    vkDestroyInstance(instance, nullptr);
    return env->NewStringUTF(report.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_NativeProbe_bufferReport(JNIEnv* env, jclass) {
    AHardwareBuffer_Desc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    AHardwareBuffer* buffer = nullptr;
    int status = AHardwareBuffer_allocate(&desc, &buffer);
    std::string result;
    if (status != 0 || !buffer) {
        result = "AHardwareBuffer (64x64): falha de alocação (código " + std::to_string(status) + ")";
    } else {
        AHardwareBuffer_Desc actual{};
        AHardwareBuffer_describe(buffer, &actual);
        result = "AHardwareBuffer (64x64 RGBA8): alocação OK, stride " + std::to_string(actual.stride)
                 + ". Importação Vulkan e Bloodframe ainda NÃO testadas.";
        AHardwareBuffer_release(buffer);
    }
    return env->NewStringUTF(result.c_str());
}
