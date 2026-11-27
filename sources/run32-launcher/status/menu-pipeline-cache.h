#ifndef IZ2_MENU_PIPELINE_CACHE_H
#define IZ2_MENU_PIPELINE_CACHE_H
#include <vulkan/vulkan.h>
#include <sys/stat.h>

/* Optional acceleration only. Runtime/package validation is unchanged. */
#define IZ2_CACHE_LIMIT (16u*1024u*1024u)
#define IZ2_CACHE_PATH "sdmc:/iz2s/cache/menu-vulkan-v3.bin"
#define IZ2_CACHE_LEGACY_PATH "sdmc:/iz2s/cache/menu-vulkan.bin"
#define IZ2_CACHE_MAGIC "IZVK0003"
#define IZ2_CACHE_LEGACY_MAGIC "IZVK0002"
enum { IZ2_CACHE_SHADER_COUNT = 6 };
typedef struct {const void *data; size_t bytes;} Iz2CacheShader;
typedef struct {
    uint8_t magic[8], shader[32], uuid[16], digest[32];
    uint32_t vendor, device, driver, bytes;
} Iz2CacheHeader;
_Static_assert(sizeof(Iz2CacheHeader)==104,"Cache header layout");
static VkDevice iz2_cache_device;
static VkPipelineCache iz2_pipeline_cache;
static PFN_vkDestroyPipelineCache iz2_cache_destroy;
static PFN_vkGetPipelineCacheData iz2_cache_data;
static Iz2CacheHeader iz2_cache_identity;
static bool iz2_cache_hit,iz2_cache_saved,iz2_cache_from_tmp,iz2_cache_legacy_hit;
static const char *iz2_cache_write;
static int iz2_cache_errno;
static unsigned iz2_pipeline_hits,iz2_pipeline_requests;
static IzFunction iz2_graphics_original,iz2_compute_original;
static u64 iz2_start_tick,iz2_first_frame_tick;
static unsigned iz2_phase_count;
static struct {const char *name;u64 tick;} iz2_phases[16];
static bool iz2_start_reported;
static void iz2_cache_close(void);
static void iz2_start_mark(const char *name){
    if(iz2_phase_count<16)iz2_phases[iz2_phase_count++]=(typeof(iz2_phases[0])){name,armGetSystemTick()};
}
static void iz2_start_begin(void){
    iz2_start_tick=armGetSystemTick();iz2_phase_count=0;iz2_first_frame_tick=0;iz2_start_reported=false;
}
static void iz2_cache_shader_key(uint8_t key[32],const Iz2CacheShader shaders[IZ2_CACHE_SHADER_COUNT])
{
    // Each role has its own slot, including absent optional shaders.
    uint8_t parts[IZ2_CACHE_SHADER_COUNT][32] = {0};
    for (size_t i = 0; i < IZ2_CACHE_SHADER_COUNT; ++i)
        if (shaders[i].bytes) sha256CalculateHash(parts[i], shaders[i].data, shaders[i].bytes);
    sha256CalculateHash(key, parts, sizeof parts);
}
static uint32_t iz2_cache_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool iz2_cache_header_matches(const Iz2CacheHeader *h,size_t bytes,const Iz2CacheHeader *expected){
    return bytes>=32&&bytes<=IZ2_CACHE_LIMIT&&h->bytes==bytes&&!memcmp(h->magic,expected->magic,8)&&
           !memcmp(h->shader,expected->shader,32)&&!memcmp(h->uuid,expected->uuid,16)&&
           h->vendor==expected->vendor&&h->device==expected->device&&h->driver==expected->driver;
}
static bool iz2_cache_vulkan_matches(const uint8_t *data,const Iz2CacheHeader *expected){
    return iz2_cache_le32(data)==32&&iz2_cache_le32(data+4)==VK_PIPELINE_CACHE_HEADER_VERSION_ONE&&
           iz2_cache_le32(data+8)==expected->vendor&&iz2_cache_le32(data+12)==expected->device&&
           !memcmp(data+16,expected->uuid,16);
}
static bool iz2_cache_valid(const Iz2CacheHeader *h,const uint8_t *data,size_t bytes,const Iz2CacheHeader *expected){
    if(!iz2_cache_header_matches(h,bytes,expected)||!iz2_cache_vulkan_matches(data,expected))return false;
    uint8_t digest[32];sha256CalculateHash(digest,data,bytes);
    return !memcmp(digest,h->digest,32);
}
static void iz2_cache_open(IzContext *c,const uint8_t key[32],const void *image,size_t image_bytes){
    iz2_cache_hit=iz2_cache_saved=false;iz2_pipeline_cache=VK_NULL_HANDLE;
    iz2_cache_from_tmp=iz2_cache_legacy_hit=false;iz2_cache_write="pending";iz2_cache_errno=0;
    iz2_pipeline_hits=iz2_pipeline_requests=0;
    iz2_cache_device=(VkDevice)(uintptr_t)c->device;
    PFN_vkCreatePipelineCache create=(PFN_vkCreatePipelineCache)vkGetDeviceProcAddr(iz2_cache_device,"vkCreatePipelineCache");
    iz2_cache_destroy=(PFN_vkDestroyPipelineCache)vkGetDeviceProcAddr(iz2_cache_device,"vkDestroyPipelineCache");
    iz2_cache_data=(PFN_vkGetPipelineCacheData)vkGetDeviceProcAddr(iz2_cache_device,"vkGetPipelineCacheData");
    if(!create||!iz2_cache_destroy||!iz2_cache_data)return;
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties((VkPhysicalDevice)(uintptr_t)c->graphics_physical,&properties);
    memset(&iz2_cache_identity,0,sizeof iz2_cache_identity);
    memcpy(iz2_cache_identity.magic,IZ2_CACHE_MAGIC,8);
    memcpy(iz2_cache_identity.shader,key,32);
    memcpy(iz2_cache_identity.uuid,properties.pipelineCacheUUID,16);
    iz2_cache_identity.vendor=properties.vendorID;iz2_cache_identity.device=properties.deviceID;iz2_cache_identity.driver=properties.driverVersion;
    void *data=NULL;size_t bytes=0;
    Iz2CacheHeader legacy=iz2_cache_identity;
    bool legacy_key_ready=false;
    const char *paths[]={IZ2_CACHE_PATH,IZ2_CACHE_PATH ".tmp",
                         IZ2_CACHE_LEGACY_PATH,IZ2_CACHE_LEGACY_PATH ".tmp"};
    for(unsigned candidate=0;candidate<(image?4u:2u)&&!bytes;candidate++){
        FILE *f=fopen(paths[candidate],"rb");if(!f)continue;
        free(data);data=NULL;
        Iz2CacheHeader h;struct stat st;
        const Iz2CacheHeader *expected=&iz2_cache_identity;
        if(!fstat(fileno(f),&st)&&S_ISREG(st.st_mode)&&st.st_size>=(off_t)(sizeof h+32)&&
           st.st_size<=(off_t)(sizeof h+IZ2_CACHE_LIMIT)&&fread(&h,1,sizeof h,f)==sizeof h){
            if(candidate>=2){
                // A legacy cache is valid only for the exact menu image used
                // to build it. A successful warm launch can migrate it.
                if(!legacy_key_ready){
                    memcpy(legacy.magic,IZ2_CACHE_LEGACY_MAGIC,8);
                    sha256CalculateHash(legacy.shader,image,image_bytes);
                    legacy_key_ready=true;
                }
                expected=&legacy;
            }
            if(iz2_cache_header_matches(&h,(size_t)st.st_size-sizeof h,expected)){
                data=malloc(h.bytes);
                if(data&&fread(data,1,h.bytes,f)==h.bytes&&fgetc(f)==EOF&&!ferror(f)&&
                   iz2_cache_valid(&h,data,h.bytes,expected))bytes=h.bytes;
            }
        }
        if(fclose(f))bytes=0;
        if(bytes){
            iz2_cache_from_tmp=(candidate%2)==1;
            iz2_cache_legacy_hit=expected==&legacy;
        }
    }
    VkPipelineCacheCreateInfo info={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,.initialDataSize=bytes,.pInitialData=bytes?data:NULL};
    VkResult rc=create(iz2_cache_device,&info,NULL,&iz2_pipeline_cache);
    iz2_cache_hit=rc==VK_SUCCESS&&bytes>0;
    if(!iz2_cache_hit)iz2_cache_from_tmp=iz2_cache_legacy_hit=false;
    if(rc!=VK_SUCCESS&&bytes){info.initialDataSize=0;info.pInitialData=NULL;rc=create(iz2_cache_device,&info,NULL,&iz2_pipeline_cache);}
    if(rc!=VK_SUCCESS)iz2_pipeline_cache=VK_NULL_HANDLE;
    free(data);
}
static void iz2_pipeline_feedback(const VkPipelineCreationFeedback *feedback){
    ++iz2_pipeline_requests;
    if((feedback->flags&VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)&&
       (feedback->flags&VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT))++iz2_pipeline_hits;
}
static u64 iz2_graphics_cached(u64 a,u64 b,u64 d,u64 e,u64 f,u64 g,u64 h,u64 i){
    if(d!=1)return iz2_graphics_original(a,b,d,e,f,g,h,i);
    VkGraphicsPipelineCreateInfo info=*(const VkGraphicsPipelineCreateInfo*)(uintptr_t)e;
    VkPipelineCreationFeedback feedback={0};
    VkPipelineCreationFeedbackCreateInfo report={.sType=VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,.pNext=info.pNext,.pPipelineCreationFeedback=&feedback};
    info.pNext=&report;
    /* Menu UI: favour bounded startup compilation; application pipelines are untouched. */
    info.flags|=VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
    u64 rc=iz2_graphics_original(a,b?b:(u64)(uintptr_t)iz2_pipeline_cache,d,(u64)(uintptr_t)&info,f,g,h,i);
    iz2_pipeline_feedback(&feedback);iz2_start_mark("graphics_pipeline");return rc;
}
static u64 iz2_compute_cached(u64 a,u64 b,u64 d,u64 e,u64 f,u64 g,u64 h,u64 i){
    if(d!=1)return iz2_compute_original(a,b,d,e,f,g,h,i);
    VkComputePipelineCreateInfo info=*(const VkComputePipelineCreateInfo*)(uintptr_t)e;
    VkPipelineCreationFeedback feedback={0};
    VkPipelineCreationFeedbackCreateInfo report={.sType=VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,.pNext=info.pNext,.pPipelineCreationFeedback=&feedback};
    info.pNext=&report;
    /* Menu UI: favour bounded startup compilation; application pipelines are untouched. */
    info.flags|=VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
    u64 rc=iz2_compute_original(a,b?b:(u64)(uintptr_t)iz2_pipeline_cache,d,(u64)(uintptr_t)&info,f,g,h,i);
    iz2_pipeline_feedback(&feedback);iz2_start_mark("compute_pipeline");return rc;
}
static void iz2_cache_install(IzContext *c,const uint8_t key[32],const void *image,size_t bytes){
    iz2_cache_open(c,key,image,bytes);iz2_start_mark("pipeline_cache");
    iz2_graphics_original=FN(c,F_CREATE_GRAPHICS);iz2_compute_original=FN(c,F_CREATE_COMPUTE);
    c->functions[F_CREATE_GRAPHICS]=(u64)(uintptr_t)iz2_graphics_cached;c->functions[F_CREATE_COMPUTE]=(u64)(uintptr_t)iz2_compute_cached;
}
static void iz2_cache_unhook(IzContext *c){
    c->functions[F_CREATE_GRAPHICS]=(u64)(uintptr_t)iz2_graphics_original;c->functions[F_CREATE_COMPUTE]=(u64)(uintptr_t)iz2_compute_original;
}
static bool iz2_cache_publish(void){
    if(!rename(IZ2_CACHE_PATH ".tmp",IZ2_CACHE_PATH))return true;
    /* Switch FS rename does not replace an existing destination. Keep the
       complete temporary cache until publication succeeds. If interrupted
       after unlink, cache_open validates and recovers that temporary file. */
    if(errno!=EEXIST)return false;
    if(unlink(IZ2_CACHE_PATH)&&errno!=ENOENT)return false;
    return !rename(IZ2_CACHE_PATH ".tmp",IZ2_CACHE_PATH);
}
static void iz2_cache_save(void){
    if(iz2_cache_saved)return;iz2_cache_saved=true;
    iz2_cache_write="failed";iz2_cache_errno=0;
    if(!iz2_pipeline_cache){iz2_cache_write="unavailable";return;}
    if(iz2_cache_hit&&iz2_pipeline_requests&&iz2_pipeline_hits==iz2_pipeline_requests){
        if(!iz2_cache_legacy_hit && iz2_cache_from_tmp){
            if(iz2_cache_publish())iz2_cache_write="recovered";
            else iz2_cache_errno=errno;
            return;
        }
        if(!iz2_cache_legacy_hit){iz2_cache_write="reused";return;}
        // Preserve the old warm cache if an app opens before the idle save.
        if(!iz2_start_reported){iz2_cache_write="reused";return;}
    }
    size_t bytes=0;if(iz2_cache_data(iz2_cache_device,iz2_pipeline_cache,&bytes,NULL)!=VK_SUCCESS||bytes<32||bytes>IZ2_CACHE_LIMIT)return;
    const size_t capacity=bytes;
    void *data=malloc(capacity);if(!data)return;
    if(iz2_cache_data(iz2_cache_device,iz2_pipeline_cache,&bytes,data)==VK_SUCCESS&&
       bytes>=32&&bytes<=capacity&&iz2_cache_vulkan_matches(data,&iz2_cache_identity)){
        Iz2CacheHeader header=iz2_cache_identity;header.bytes=(uint32_t)bytes;sha256CalculateHash(header.digest,data,bytes);
        if(!mkdir("sdmc:/iz2s/cache",0777)||errno==EEXIST){
            FILE *f=fopen(IZ2_CACHE_PATH ".tmp","wb");
            if(f){bool ok=fwrite(&header,1,sizeof header,f)==sizeof header&&fwrite(data,1,bytes,f)==bytes;
                if(ok&&(fflush(f)||fsync(fileno(f))))ok=false;
                int error=ok?0:errno;
                if(fclose(f)){ok=false;error=errno;}
                if(ok){if(iz2_cache_publish())iz2_cache_write=iz2_cache_legacy_hit?"migrated":"saved";else iz2_cache_errno=errno;}
                else{iz2_cache_errno=error;unlink(IZ2_CACHE_PATH ".tmp");}
            }else iz2_cache_errno=errno;
        }
    }
    free(data);
}
/* Stop owning driver cache memory before entering any application. */
void iz2_menu_cache_retire(void){
    iz2_cache_save();
    iz2_cache_close();
}
void iz2_startup_catalog_ready(void){iz2_start_mark("catalog");}
void iz2_startup_artwork_ready(void){iz2_start_mark("catalog_artwork_ready");}
void iz2_startup_frame(u64 frames){
    if(!frames)return;
    if(!iz2_first_frame_tick){
        iz2_first_frame_tick=armGetSystemTick();
        iz2_start_mark("first_present");
        fputs("IZLAUNCHER_FIRST_PRESENT\n",stderr);
    }
    if(iz2_start_reported||armGetSystemTick()-iz2_first_frame_tick<armNsToTicks(UINT64_C(2000000000)))return;
    iz2_start_reported=true;
    /* One delayed report/cache save, never on the first-frame critical path. */
    iz2_menu_cache_retire();
    FILE *f=fopen("sdmc:/iz2s/launcher-startup.json","w");
    if(f){
        fprintf(f,"{\"scope\":\"native_menu\",\"cacheAccepted\":%s,\"stages\":[",iz2_cache_hit?"true":"false");
        for(unsigned n=0;n<iz2_phase_count;n++)fprintf(f,"%s{\"name\":\"%s\",\"elapsedMs\":%.3f}",n?",":"",iz2_phases[n].name,(double)(iz2_phases[n].tick-iz2_start_tick)*1000.0/(double)armNsToTicks(UINT64_C(1000000000)));
        fprintf(f,"],\"pipelineRequests\":%u,\"pipelineCacheHits\":%u,\"cacheRead\":\"%s\",\"cacheWrite\":\"%s\",\"cacheErrno\":%d}\n",iz2_pipeline_requests,iz2_pipeline_hits,iz2_cache_hit?(iz2_cache_from_tmp?"temporary":"primary"):"none",iz2_cache_write,iz2_cache_errno);fclose(f);
    }
}
static void iz2_cache_close(void){
    if(iz2_pipeline_cache&&iz2_cache_destroy)iz2_cache_destroy(iz2_cache_device,iz2_pipeline_cache,NULL);
    iz2_pipeline_cache=VK_NULL_HANDLE;iz2_cache_device=VK_NULL_HANDLE;
}
#endif
