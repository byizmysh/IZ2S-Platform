#include "PS2VM.h"
#include "AppConfig.h"
#include "PS2VM_Preferences.h"
#include "PH_Generic.h"
#ifdef PS2_HAS_VULKAN
#include "gs/GSH_Vulkan/GSH_VulkanOffscreen.h"
#include "gs/GSH_Vulkan/GSH_VulkanDeviceInfo.h"
#include "vulkan/Loader.h"
#endif
#include "gs/GSH_Null.h"
#include "bitmap/BMP.h"
#include "StdStream.h"
#include "code_model.h"
#include "code_runtime.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

fs::path CAppConfig::GetBasePath() const {
    const char *directory=std::getenv("PS2_WORK_DIRECTORY");
    if(!directory || !*directory)throw std::runtime_error("PS2_WORK_DIRECTORY must point inside the project");
    fs::create_directories(directory); return fs::path(directory);
}
#ifdef PS2_HAS_VULKAN
class CaptureGS : public CGSH_VulkanOffscreen {
public:
    std::string DeviceName() const {
        VkPhysicalDeviceProperties p={};
        m_instance.vkGetPhysicalDeviceProperties(m_context->physicalDevice,&p);
        return p.deviceName;
    }
};
static std::string vulkan_preflight() {
    auto instance=CGSH_Vulkan::CreateInstance(false);
    uint32_t count=0;
    if(instance.vkEnumeratePhysicalDevices(instance,&count,nullptr)!=VK_SUCCESS || !count)
        throw std::runtime_error("No Vulkan device");
    std::vector<VkPhysicalDevice> devices(count);
    if(instance.vkEnumeratePhysicalDevices(instance,&count,devices.data())!=VK_SUCCESS)throw std::runtime_error("Cannot enumerate Vulkan devices");
    auto device=devices[0];
    VkPhysicalDeviceProperties properties={};
    instance.vkGetPhysicalDeviceProperties(device,&properties);
    if(properties.apiVersion<VK_API_VERSION_1_2)throw std::runtime_error("Play desktop GS requires Vulkan 1.2");
    count=0;
    if(instance.vkEnumerateDeviceExtensionProperties(device,nullptr,&count,nullptr)!=VK_SUCCESS)throw std::runtime_error("Cannot enumerate Vulkan extensions");
    std::vector<VkExtensionProperties> extensions(count);
    if(instance.vkEnumerateDeviceExtensionProperties(device,nullptr,&count,extensions.data())!=VK_SUCCESS)throw std::runtime_error("Cannot enumerate Vulkan extensions");
    for(auto required:{VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,VK_KHR_SWAPCHAIN_EXTENSION_NAME}) {
        bool found=false; for(const auto& e:extensions)if(std::string(e.extensionName)==required)found=true;
        if(!found)throw std::runtime_error(std::string("Vulkan device lacks ")+required);
    }
    auto getFeatures=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(Framework::Vulkan::CLoader::GetInstance().vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2"));
    if(!getFeatures)throw std::runtime_error("Missing vkGetPhysicalDeviceFeatures2");
    VkPhysicalDevice16BitStorageFeatures bits16={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceVulkan12Features v12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,&bits16};
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT,&v12};
    VkPhysicalDeviceFeatures2 f={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&interlock}; getFeatures(device,&f);
    if(!f.features.fragmentStoresAndAtomics || !f.features.shaderInt16 || !interlock.fragmentShaderPixelInterlock ||
       !v12.shaderInt8 || !v12.storageBuffer8BitAccess || !v12.uniformAndStorageBuffer8BitAccess ||
       !bits16.storageBuffer16BitAccess || !bits16.uniformAndStorageBuffer16BitAccess)
        throw std::runtime_error("Vulkan device lacks required Play GS shader features");
    count=0; instance.vkGetPhysicalDeviceQueueFamilyProperties(device,&count,nullptr);
    std::vector<VkQueueFamilyProperties> queues(count); instance.vkGetPhysicalDeviceQueueFamilyProperties(device,&count,queues.data());
    if(queues.empty() || !(queues[0].queueFlags&VK_QUEUE_GRAPHICS_BIT))throw std::runtime_error("Upstream GS requires graphics queue family zero");
    GSH_Vulkan::VULKAN_DEVICE selected={}; selected.deviceId=properties.deviceID; selected.vendorId=properties.vendorID;
    GSH_Vulkan::CDeviceInfo::GetInstance().SetSelectedDevice(selected);
    return properties.deviceName;
}
#endif
struct VmLifetime {
    CPS2VM vm;
    bool initialized=false;
    void initialize() { vm.Initialize(); initialized=true; }
    void close() {
        if(!initialized)return;
        vm.Pause(); vm.DestroySoundHandler(); vm.DestroyPadHandler(); vm.DestroyGSHandler(); vm.Destroy();
        vm.m_iop.reset(); vm.m_ee.reset(); initialized=false;
    }
    ~VmLifetime() { close(); }
};
class CaptureAudio : public CSoundHandler {
public:
    explicit CaptureAudio(const std::string& path):stream(path,std::ios::binary) {
        if(!stream)throw std::runtime_error("Cannot create PCM output");
    }
    void Reset() override {}
    void Write(int16 *samples,unsigned int count,unsigned int rate) override {
        if(sampleRate && rate!=sampleRate)throw std::runtime_error("Unexpected PS2 audio rate change");
        sampleRate=rate; stream.write(reinterpret_cast<char*>(samples),count*sizeof(int16));
        if(!stream)throw std::runtime_error("PCM write failed");
        sampleCount+=count;
    }
    bool HasFreeBuffers() override { return true; }
    void RecycleBuffers() override {}
    std::ofstream stream;
    uint64_t sampleCount=0;
    uint32_t sampleRate=0;
};
int main(int argc,char **argv) {
    if(argc!=5) { std::fprintf(stderr,"Usage: ps2_runner FILE.elf|FILE.iso FRAMES OUTPUT_DIRECTORY vulkan|null\n"); return 2; }
    try {
        auto input=fs::absolute(argv[1]); auto output=fs::absolute(argv[3]); fs::create_directories(output);
        auto framesLimit=std::stoul(argv[2]);
        if(!framesLimit || framesLimit>36000)throw std::runtime_error("Frames must be 1..36000");
        const bool vulkan=std::string(argv[4])=="vulkan";
        if(!vulkan && std::string(argv[4])!="null")throw std::runtime_error("Unknown renderer");
        const auto out=output.string();
        setenv("PS2_WORK_DIRECTORY",(out+"/state").c_str(),1);
#ifndef PS2_HAS_VULKAN
        if(vulkan)throw std::runtime_error("This ARM64 CPU model has no Vulkan renderer");
#else
        if(vulkan)(void)vulkan_preflight();
#endif
#ifdef PS2_PAGED_JIT
        ps2_model_open(64u*1024*1024);
#endif
        VmLifetime lifetime; auto& vm=lifetime.vm;
        lifetime.initialize();
        CAppConfig::GetInstance().SetPreferenceBoolean(PREF_PS2_LIMIT_FRAMERATE,false);
        vm.ReloadFrameRateLimit();
#ifdef PS2_HAS_VULKAN
        vm.CreateGSHandler(vulkan ? CGSHandler::FactoryFunction([](){return new CaptureGS();}) : CGSH_Null::GetFactoryFunction());
#else
        vm.CreateGSHandler(CGSH_Null::GetFactoryFunction());
#endif
        vm.CreatePadHandler(CPH_Generic::GetFactoryFunction());
        vm.CreateSoundHandler([&out](){return new CaptureAudio(out+"/audio.s16le");});
        std::atomic<uint64_t> frames{0}; std::atomic<bool> exited{false};
        auto fc=vm.OnNewFrame.Connect([&](){ if(++frames>=framesLimit)vm.PauseAsync(); });
        auto ec=vm.m_ee->m_os->OnRequestExit.Connect([&](){ exited=true; vm.PauseAsync(); });
        if(input.extension()==".elf")vm.m_ee->m_os->BootFromFile(input);
        else { CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_CDROM0_PATH,input); vm.CDROM0_SyncPath(); if(!vm.m_cdrom0)throw std::runtime_error("Cannot mount disc image"); vm.m_ee->m_os->BootFromCDROM(); }
        const auto start=std::chrono::steady_clock::now(); vm.Resume();
        while(frames<framesLimit && !exited) {
            if(std::chrono::steady_clock::now()-start>std::chrono::seconds(120))throw std::runtime_error("PS2 runner timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        vm.Pause();
        auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        auto audio=static_cast<CaptureAudio*>(vm.GetSoundHandler());
        auto audioSamples=audio->sampleCount; auto audioRate=audio->sampleRate;
        std::string device="disabled";
        uint32 width=0,height=0;
#ifdef PS2_HAS_VULKAN
        if(vulkan) {
            auto gs=static_cast<CaptureGS*>(vm.GetGSHandler());
            auto display=gs->GetCurrentDisplayInfo();
            const auto& circuit=display.layers[0].enabled?display.layers[0]:display.layers[1];
            /* Read the emulated GS framebuffer, not a host-generated illustration. */
            CGSHandler::FRAME frameReg={};
            frameReg.nPtr=circuit.bufPtr/8192; frameReg.nWidth=circuit.bufWidth/64;
            frameReg.nPsm=circuit.psm;
            auto bitmap=gs->GetFramebuffer(static_cast<uint64>(frameReg));
            width=bitmap.GetWidth(); height=bitmap.GetHeight();
            /* The GS debugger returns BGRA. Framework's BMP writer consumes RGBA. */
            auto pixels=bitmap.GetPixels();
            for(size_t i=0;i<bitmap.GetPixelsSize();i+=4)std::swap(pixels[i],pixels[i+2]);
            Framework::CStdStream stream((out+"/framebuffer.bmp").c_str(),"wb");
            Framework::CBMP::WriteBitmap(bitmap,stream);
            device=gs->DeviceName();
        }
#endif
        uint32_t marker=0,iterations=0;
        std::memcpy(&marker,vm.m_ee->m_ram+0x120000,4);
        std::memcpy(&iterations,vm.m_ee->m_ram+0x120004,4);
        lifetime.close();
        std::ofstream report(out+"/run.json");
        report << "{\"schema\":1,\"consoleExecuted\":false,\"frames\":" << frames.load()
               << ",\"seconds\":" << elapsed << ",\"framebufferWidth\":" << width << ",\"framebufferHeight\":" << height
               << ",\"audioSamples\":" << audioSamples << ",\"audioRate\":" << audioRate
               << ",\"eeMarker\":" << marker << ",\"eeIterations\":" << iterations << ",\"graphicsDevice\":" << std::quoted(device);
#ifdef PS2_PAGED_JIT
        auto a=ps2_code_arena();
        report << ",\"jitBackend\":\"paged-rx-rw-model\",\"jitAllocations\":" << a->allocations
               << ",\"jitPublications\":" << a->publications << ",\"jitPeakPages\":" << a->peak_pages
               << ",\"jitLivePages\":" << a->live_pages;
        ps2_model_close();
#else
        report << ",\"jitBackend\":\"upstream\"";
#endif
        report << "}\n";
        std::printf("PS2 run completed: %llu frames in %.3fs; graphics device: %s\n",(unsigned long long)frames.load(),elapsed,device.c_str());
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"PS2: %s\n",e.what()); return 1; }
}
