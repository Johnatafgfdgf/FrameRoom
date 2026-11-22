// FrameRoom R0.6: diagnostic ONLY. Generates one LSFG frame from two GPU-filled AHB
// inputs, waits for completion, and reports the call's outcome. It does not present
// generated frames or access any other Android process.
#include <jni.h>
#include <android/hardware_buffer.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "third_party/bb_lsfg.h"

namespace {
constexpr uint32_t WIDTH=640, HEIGHT=360;
using Init=decltype(&bb_lsfg_init);
using Finish=decltype(&bb_lsfg_shutdown);
using Create=decltype(&bb_lsfg_create_context);
using Present=decltype(&bb_lsfg_present);
using Wait=decltype(&bb_lsfg_wait);
using Destroy=decltype(&bb_lsfg_destroy_context);
struct Buffer {
    AHardwareBuffer* value=nullptr;
    ~Buffer(){if(value)AHardwareBuffer_release(value);}
    bool allocate(uint64_t flags,std::string& err,const char* label) {
        AHardwareBuffer_Desc d{};
        d.width=WIDTH;d.height=HEIGHT;d.layers=1;
        d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        d.usage=flags;
        // 1) Android gralloc support may contradict the Vulkan format probe.
        // 2) Report the input/output role and exact usage bits on failure.
        int supported=AHardwareBuffer_isSupported(&d);
        int rc=AHardwareBuffer_allocate(&d,&value);
        if(rc!=0||!value){
            err=std::string(label)+": AHardwareBuffer_allocate falhou: rc="+
                std::to_string(rc)+" usage="+std::to_string(flags)+
                " (isSupported="+std::to_string(supported)+")";
            return false;
        }
        return true;
    }
};
struct Vulkan {
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0;
    ~Vulkan(){
        if(device){vkDeviceWaitIdle(device);vkDestroyDevice(device,nullptr);}
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    bool open(std::string& err){
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName="FrameRoom Synthetic LSFG Test";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
        VkResult rc=vkCreateInstance(&ci,nullptr,&instance);
        if(rc!=VK_SUCCESS){err="vkCreateInstance: "+std::to_string(rc);return false;}
        uint32_t n=0;
        rc=vkEnumeratePhysicalDevices(instance,&n,nullptr);
        if(rc!=VK_SUCCESS||!n){err="Nenhuma GPU Vulkan detectada.";return false;}
        std::vector<VkPhysicalDevice> gpus(n);
        if(vkEnumeratePhysicalDevices(instance,&n,gpus.data())!=VK_SUCCESS){err="Falha ao listar GPUs.";return false;}
        for(VkPhysicalDevice gpu:gpus){
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(gpu,&props);
            if(VK_VERSION_MAJOR(props.apiVersion)<1 ||
              (VK_VERSION_MAJOR(props.apiVersion)==1&&VK_VERSION_MINOR(props.apiVersion)<1))continue;
            uint32_t extCount=0;
            if(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&extCount,nullptr)!=VK_SUCCESS)continue;
            std::vector<VkExtensionProperties> extensions(extCount);
            if(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&extCount,extensions.data())!=VK_SUCCESS)continue;
            auto hasExtension = [&](const char* name) {
                return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
                    return std::strcmp(extension.extensionName, name) == 0;
                });
            };
            if (!hasExtension(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) ||
                !hasExtension(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) continue;
            uint32_t qCount=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&qCount,nullptr);
            std::vector<VkQueueFamilyProperties> qs(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&qCount,qs.data());
            for(uint32_t q=0;q<qCount;++q){
                if(qs[q].queueFlags&VK_QUEUE_GRAPHICS_BIT){physical=gpu;family=q;break;}
            }
            if(physical)break;
        }
        if(!physical){err="GPU sem Vulkan 1.1, fila gráfica ou extensão AHB.";return false;}
        float priority=1.0f;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        const char* extensions[] = {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                                    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;
        dc.enabledExtensionCount=2;dc.ppEnabledExtensionNames=extensions;
        rc=vkCreateDevice(physical,&dc,nullptr,&device);
        if(rc!=VK_SUCCESS){err="vkCreateDevice (AHB): "+std::to_string(rc);return false;}
        vkGetDeviceQueue(device,family,0,&queue);
        return true;
    }
};
bool fill(Vulkan& vk, AHardwareBuffer* ahb, float red, float blue, std::string& err){
    // Import buffer as VkImage and fill with Vulkan GPU commands. A CPU lock is
    // intentionally never used: GPU-only AHB is supported.
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    auto clean=[&]{
        if(pool)vkDestroyCommandPool(vk.device,pool,nullptr);
        if(image)vkDestroyImage(vk.device,image,nullptr);
        if(memory)vkFreeMemory(vk.device,memory,nullptr);
    };
    auto get=reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(vk.device,"vkGetAndroidHardwareBufferPropertiesANDROID"));
    if(!get){err="Extensão Vulkan AHB indisponível no driver.";return false;}
    VkAndroidHardwareBufferFormatPropertiesANDROID fmt{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    props.pNext=&fmt;
    VkResult rc=get(vk.device,ahb,&props);
    if(rc!=VK_SUCCESS||fmt.format!=VK_FORMAT_R8G8B8A8_UNORM){
        err="Formato AHB RGBA8 não pode ser importado ("+std::to_string(rc)+").";return false;
    }
    VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext=&ext;imageInfo.imageType=VK_IMAGE_TYPE_2D;
    imageInfo.format=VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent={WIDTH,HEIGHT,1};imageInfo.mipLevels=1;
    imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    rc=vkCreateImage(vk.device,&imageInfo,nullptr,&image);
    if(rc!=VK_SUCCESS){err="Importação VkImage: "+std::to_string(rc);clean();return false;}
    // Dedicated AHB imports take size and memory types from the buffer itself.
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(vk.physical,&mp);
    uint32_t mask=props.memoryTypeBits;
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<mp.memoryTypeCount;++i)
        if(mask&(1u<<i)){type=i;if(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
    if(type==UINT32_MAX){err="Sem tipo de memória AHB compatível.";clean();return false;}
    VkImportAndroidHardwareBufferInfoANDROID importInfo{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    importInfo.buffer=ahb;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext=&importInfo;dedicated.image=image;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.pNext=&dedicated;alloc.allocationSize=props.allocationSize;
    alloc.memoryTypeIndex=type;
    rc=vkAllocateMemory(vk.device,&alloc,nullptr,&memory);
    if(rc!=VK_SUCCESS){err="vkAllocateMemory AHB: "+std::to_string(rc);clean();return false;}
    rc=vkBindImageMemory(vk.device,image,memory,0);
    if(rc!=VK_SUCCESS){err="vkBindImageMemory: "+std::to_string(rc);clean();return false;}
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex=vk.family;
    rc=vkCreateCommandPool(vk.device,&pi,nullptr,&pool);
    if(rc!=VK_SUCCESS){err="vkCreateCommandPool: "+std::to_string(rc);clean();return false;}
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    rc=vkAllocateCommandBuffers(vk.device,&ai,&cmd);
    if(rc!=VK_SUCCESS){err="vkAllocateCommandBuffers: "+std::to_string(rc);clean();return false;}
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    rc=vkBeginCommandBuffer(cmd,&bi);
    if(rc!=VK_SUCCESS){err="vkBeginCommandBuffer: "+std::to_string(rc);clean();return false;}
    VkImageMemoryBarrier transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    transition.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    transition.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.image=image;transition.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    transition.subresourceRange.levelCount=1;transition.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,nullptr,0,nullptr,1,&transition);
    VkClearColorValue color{};
    color.float32[0]=red;color.float32[1]=0.12f;
    color.float32[2]=blue;color.float32[3]=1.f;
    VkImageSubresourceRange range{};
    range.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;range.levelCount=1;range.layerCount=1;
    vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    VkImageMemoryBarrier release=transition;
    release.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    release.dstAccessMask=0;
    release.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    release.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    release.srcQueueFamilyIndex=vk.family;
    release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,0,nullptr,0,nullptr,1,&release);
    rc=vkEndCommandBuffer(cmd);
    if(rc!=VK_SUCCESS){err="vkEndCommandBuffer: "+std::to_string(rc);clean();return false;}
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount=1;si.pCommandBuffers=&cmd;
    rc=vkQueueSubmit(vk.queue,1,&si,VK_NULL_HANDLE);
    if(rc==VK_SUCCESS)rc=vkQueueWaitIdle(vk.queue);
    if(rc!=VK_SUCCESS){err="vkQueueSubmit/WaitIdle: "+std::to_string(rc);clean();return false;}
    clean();return true;
}
void closeFds(BbLsfgPresent& p){
    for(auto& fd:p.out_ready_fd) if(fd>=0){close(fd);fd=-1;}
    if(p.batch_complete_fd>=0){close(p.batch_complete_fd);p.batch_complete_fd=-1;}
}
std::string run(const std::string& dll){
    if(dll.empty())return "Nenhum arquivo Lossless.dll importado.";
    Vulkan vk;std::string err;
    if(!vk.open(err))return err;
    void* module=dlopen("libbloodframe.so",RTLD_NOW|RTLD_LOCAL);
    if(!module){const char* e=dlerror();return std::string("Bloodframe indisponível: ")+(e?e:"dlopen falhou");}
    auto init=reinterpret_cast<Init>(dlsym(module,"bb_lsfg_init"));
    auto shutdown=reinterpret_cast<Finish>(dlsym(module,"bb_lsfg_shutdown"));
    auto create=reinterpret_cast<Create>(dlsym(module,"bb_lsfg_create_context"));
    auto present=reinterpret_cast<Present>(dlsym(module,"bb_lsfg_present"));
    auto wait=reinterpret_cast<Wait>(dlsym(module,"bb_lsfg_wait"));
    auto destroy=reinterpret_cast<Destroy>(dlsym(module,"bb_lsfg_destroy_context"));
    if(!init||!shutdown||!create||!present||!wait||!destroy){
        dlclose(module);return "Bloodframe sem API C completa.";
    }
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 deviceProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    deviceProperties.pNext=&ids;
    vkGetPhysicalDeviceProperties2(vk.physical,&deviceProperties);
    BbLsfgConfig config{};
    config.vk_get_instance_proc_addr=reinterpret_cast<void*>(vkGetInstanceProcAddr);
    config.dll_path=dll.c_str();
    std::memcpy(config.device_uuid,ids.deviceUUID,VK_UUID_SIZE);
    std::memcpy(config.driver_uuid,ids.driverUUID,VK_UUID_SIZE);
    config.flow_scale=0.5f;config.performance=1;config.generation_count=1;
    config.shared_format=VK_FORMAT_R8G8B8A8_UNORM;config.fp16=0;
    BbLsfgInfo info{};
    char msg[1024]{};
    int result=init(&config,&info,msg,sizeof(msg));
    if(result!=0){std::string why=msg[0]?msg:"Erro interno de inicialização";dlclose(module);return "bb_lsfg_init falhou: "+why;}
    std::string summary;
    {
        Buffer in0,in1,out;
        const uint64_t inFlags=info.input_ahb_usage|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
        const uint64_t outFlags=info.output_ahb_usage;
        if(!in0.allocate(inFlags,err,"entrada 0")||!in1.allocate(inFlags,err,"entrada 1")||
           !out.allocate(outFlags,err,"saída FG"))summary="Alocação AHB: "+err;
        else if(!fill(vk,in0.value,0.15f,0.55f,err)||
                !fill(vk,in1.value,0.65f,0.15f,err))
            summary="Não foi possível escrever frames pela GPU: "+err;
        else {
            AHardwareBuffer* outputs[]={out.value};
            int context=create(in0.value,in1.value,outputs,1,WIDTH,HEIGHT,
                               VK_FORMAT_R8G8B8A8_UNORM,msg,sizeof(msg));
            if(context<0)summary=std::string("bb_lsfg_create_context falhou: ")+msg;
            else {
                BbLsfgPresent prime{};
                std::fill(std::begin(prime.out_ready_fd),std::end(prime.out_ready_fd),-1);
                prime.batch_complete_fd=-1;
                int a=present(context,-1,0,&prime,msg,sizeof(msg));
                // A second present uses in1, then requests one interpolated output.
                if(a==0)a=wait(context,5000000000ULL);
                closeFds(prime);
                BbLsfgPresent batch{};
                std::fill(std::begin(batch.out_ready_fd),std::end(batch.out_ready_fd),-1);
                batch.batch_complete_fd=-1;
                int b=-1;
                if(a==0)b=present(context,-1,1,&batch,msg,sizeof(msg));
                int completed=b==0?wait(context,5000000000ULL):-1;
                closeFds(batch);
                if(a==0&&b==0&&completed==0)
                    summary="Bloodframe retornou SUCESSO ao gerar 1 quadro 2× usando "
                        "duas imagens Vulkan AHB 640×360. A saída ainda NÃO foi "
                        "lida nem exibida: qualidade e latência não verificadas. " +
                        std::string("AHB input_copy=")+(info.input_copy?"1":"0")+
                        " output_copy="+(info.output_copy?"1":"0");
                else summary=std::string("Falha no lote LSFG (prime=")+std::to_string(a)+
                    ", present="+std::to_string(b)+", wait="+
                    std::to_string(completed)+"): "+msg;
                destroy(context);
            }
        }
    }
    shutdown();
    dlclose(module);
    return summary;
}
std::mutex smokeMutex;
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_frameroom_NativeProbe_frameGenBatchReport(JNIEnv* env,jclass,jstring path) {
    if(!path)return env->NewStringUTF("Importe sua Lossless.dll primeiro.");
    const char* raw=env->GetStringUTFChars(path,nullptr);
    if(!raw)return env->NewStringUTF("Falha ao ler caminho da DLL.");
    std::string dll(raw);
    env->ReleaseStringUTFChars(path,raw);
    std::lock_guard<std::mutex> lock(smokeMutex);
    auto response=run(dll);
    return env->NewStringUTF(response.c_str());
}
