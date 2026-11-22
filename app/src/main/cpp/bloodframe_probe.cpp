// FrameRoom R0.5 real Bloodframe initialization test; no generated frames are presented.
#include <jni.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <mutex>
#include <cstring>
#include <string>
#include <vector>
#include "third_party/bb_lsfg.h"

namespace {
std::mutex guard;
std::string probe(const std::string& path) {
    void* module=dlopen("libbloodframe.so",RTLD_NOW|RTLD_LOCAL);
    if(!module) return std::string("Bloodframe indisponível: ")+(dlerror()?dlerror():"dlopen falhou");
    auto init=reinterpret_cast<decltype(&bb_lsfg_init)>(dlsym(module,"bb_lsfg_init"));
    auto shutdown=reinterpret_cast<decltype(&bb_lsfg_shutdown)>(dlsym(module,"bb_lsfg_shutdown"));
    auto diag=reinterpret_cast<decltype(&bb_lsfg_diagnostics)>(dlsym(module,"bb_lsfg_diagnostics"));
    auto shaders=reinterpret_cast<decltype(&bb_lsfg_shader_set)>(dlsym(module,"bb_lsfg_shader_set"));
    if(!init||!shutdown||!diag||!shaders){dlclose(module);return "API Bloodframe incompleta.";}
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="FrameRoom LSFG diagnostic";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo=&app;
    VkInstance instance=VK_NULL_HANDLE;
    VkResult v=vkCreateInstance(&create,nullptr,&instance);
    if(v!=VK_SUCCESS){dlclose(module);return "Falha Vulkan 1.1: "+std::to_string(v);}
    std::string result;
    uint32_t count=0;
    v=vkEnumeratePhysicalDevices(instance,&count,nullptr);
    if(v!=VK_SUCCESS||!count) result="Nenhuma GPU Vulkan disponível.";
    else {
        std::vector<VkPhysicalDevice> devices(count);
        v=vkEnumeratePhysicalDevices(instance,&count,devices.data());
        if(v!=VK_SUCCESS)result="Falha ao consultar GPUs Vulkan.";
        else {
            VkPhysicalDevice selected=VK_NULL_HANDLE;
            for(auto gpu:devices) {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(gpu,&props);
                if(VK_VERSION_MAJOR(props.apiVersion)>1 ||
                   (VK_VERSION_MAJOR(props.apiVersion)==1&&VK_VERSION_MINOR(props.apiVersion)>=1)) {
                    selected=gpu;break;
                }
            }
            if(!selected)result="Driver não oferece Vulkan 1.1.";
            else {
                VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                properties.pNext=&ids;
                vkGetPhysicalDeviceProperties2(selected,&properties);
                BbLsfgConfig config{};
                config.vk_get_instance_proc_addr=reinterpret_cast<void*>(vkGetInstanceProcAddr);
                config.dll_path=path.c_str();
                std::memcpy(config.device_uuid,ids.deviceUUID,16);
                std::memcpy(config.driver_uuid,ids.driverUUID,16);
                config.flow_scale=0.5f;config.performance=1;
                config.generation_count=1;config.shared_format=VK_FORMAT_R8G8B8A8_UNORM;
                config.hdr=0;config.fp16=0;
                BbLsfgInfo info{};
                char err[1024]{};
                const int status=init(&config,&info,err,sizeof(err));
                if(status==0) {
                    char infoText[1024]{},shaderText[256]{};
                    int shader=shaders(shaderText,sizeof(shaderText));
                    diag(infoText,sizeof(infoText));
                    result=std::string("LSFG inicializado na GPU ")+properties.properties.deviceName+
                           "\nShaders: "+std::to_string(shader)+" ("+shaderText+")"+
                           "\n"+infoText+
                           "\nAHB input usage: "+std::to_string(info.input_ahb_usage)+
                           "\nAHB output usage: "+std::to_string(info.output_ahb_usage)+
                           "\nSync FD: "+std::string(info.sync_fd_ok?"sim":"não")+
                           "\nAtenção: este teste ainda não gera nem apresenta quadros.";
                    shutdown();
                } else result=std::string("Falha LSFG na GPU ")+properties.properties.deviceName+
                              ": "+(err[0]?err:"motivo não informado");
            }
        }
    }
    vkDestroyInstance(instance,nullptr);
    dlclose(module);
    return result;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_NativeProbe_bloodframeReport(JNIEnv* env,jclass,jstring path) {
    if(!path)return env->NewStringUTF("Importe a DLL primeiro.");
    const char* raw=env->GetStringUTFChars(path,nullptr);
    if(!raw)return env->NewStringUTF("Não foi possível ler o caminho.");
    std::string p(raw);env->ReleaseStringUTFChars(path,raw);
    std::lock_guard<std::mutex> lock(guard);
    auto result=probe(p);
    return env->NewStringUTF(result.c_str());
}
