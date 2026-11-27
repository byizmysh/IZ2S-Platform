#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
typedef uint64_t u64;
typedef u64(*IzFunction)(u64,u64,u64,u64,u64,u64,u64,u64);
enum{F_CREATE_GRAPHICS,F_CREATE_COMPUTE};
typedef struct{u64 device,graphics_physical,functions[2];}IzContext;
#define FN(c,n) ((IzFunction)(uintptr_t)(c)->functions[n])
static u64 armNsToTicks(u64 ns){return ns;}
static u64 armGetSystemTick(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (u64)t.tv_sec*1000000000+t.tv_nsec;}
static void sha256CalculateHash(void *out,const void *data,size_t bytes){SHA256(data,bytes,out);}
/* Model the console filesystem: rename cannot replace an existing file. */
static int rename_fault;
static unsigned rename_existing;
static int console_rename(const char *src,const char *dst){
 if(rename_fault==EACCES){errno=EACCES;return -1;}
 if(!access(dst,F_OK)){++rename_existing;errno=EEXIST;return -1;}
 if(rename_fault==EIO){errno=EIO;return -1;}
 return rename(src,dst);
}
#define rename console_rename
#include "menu-pipeline-cache.h"
#undef rename
static void *read_file(const char *name,size_t *bytes){FILE*f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));*bytes=(size_t)ftell(f);rewind(f);void*p=malloc(*bytes);assert(p&&fread(p,1,*bytes,f)==*bytes);assert(!fclose(f));return p;}
static u64 compute(u64 a,u64 b,u64 d,u64 e,u64 f,u64 g,u64 h,u64 i){(void)h;(void)i;return (u64)vkCreateComputePipelines((VkDevice)(uintptr_t)a,(VkPipelineCache)(uintptr_t)b,d,(void*)(uintptr_t)e,(void*)(uintptr_t)f,(void*)(uintptr_t)g);}
static u64 graphics(u64 a,u64 b,u64 d,u64 e,u64 f,u64 g,u64 h,u64 i){(void)h;(void)i;return (u64)vkCreateGraphicsPipelines((VkDevice)(uintptr_t)a,(VkPipelineCache)(uintptr_t)b,d,(void*)(uintptr_t)e,(void*)(uintptr_t)f,(void*)(uintptr_t)g);}
static void write_file(const char *name,const void *data,size_t bytes){
 FILE *f=fopen(name,"wb");assert(f&&fwrite(data,1,bytes,f)==bytes&&!fclose(f));
}
int main(int argc,char **argv){
 assert(argc==2||argc==4);size_t bytes;void*code=read_file(argv[1],&bytes);
 char tmp[]="/tmp/iz2-cache-test-XXXXXX";assert(mkdtemp(tmp)&&!chdir(tmp));assert(!mkdir("sdmc:",0700));assert(!mkdir("sdmc:/iz2s",0700));
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
 VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;assert(vkCreateInstance(&ci,NULL,&instance)==VK_SUCCESS);
 uint32_t count=16;VkPhysicalDevice devices[16];assert(vkEnumeratePhysicalDevices(instance,&count,devices)==VK_SUCCESS&&count);VkPhysicalDevice physical=devices[0];
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;assert(vkCreateDevice(physical,&di,NULL,&device)==VK_SUCCESS);
 IzContext c={.device=(u64)(uintptr_t)device,.graphics_physical=(u64)(uintptr_t)physical,.functions={(u64)(uintptr_t)graphics,(u64)(uintptr_t)compute}};
 VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_ALL};VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout set;assert(!vkCreateDescriptorSetLayout(device,&li,NULL,&set));
 VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set};VkPipelineLayout layout;assert(!vkCreatePipelineLayout(device,&pli,NULL,&layout));
 VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};VkShaderModule shader;assert(!vkCreateShaderModule(device,&si,NULL,&shader));
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=layout};
 VkShaderModule vs=VK_NULL_HANDLE,frag=VK_NULL_HANDLE;VkRenderPass renderpass=VK_NULL_HANDLE;
 VkGraphicsPipelineCreateInfo gi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
 VkPipelineShaderStageCreateInfo stages[2]={0};
 VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
 VkViewport viewport={0,0,1920,1080,0,1};VkRect2D scissor={{0,0},{1920,1080}};
 VkPipelineViewportStateCreateInfo view={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
 VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.lineWidth=1};
 VkPipelineMultisampleStateCreateInfo sample={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
 VkPipelineColorBlendAttachmentState attachment={.colorWriteMask=15};
 VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment};
 if(argc==4){
  size_t n;void *b=read_file(argv[2],&n);si.codeSize=n;si.pCode=b;assert(!vkCreateShaderModule(device,&si,NULL,&vs));free(b);
  b=read_file(argv[3],&n);si.codeSize=n;si.pCode=b;assert(!vkCreateShaderModule(device,&si,NULL,&frag));free(b);
  VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
  VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass};assert(!vkCreateRenderPass(device,&ri,NULL,&renderpass));
  stages[0]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"};
  stages[1]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"};
  gi.stageCount=2;gi.pStages=stages;gi.pVertexInputState=&vertex;gi.pInputAssemblyState=&assembly;gi.pViewportState=&view;gi.pRasterizationState=&raster;gi.pMultisampleState=&sample;gi.pColorBlendState=&blend;gi.layout=layout;gi.renderPass=renderpass;
 }
 if(getenv("IZ2_TEST_FAST_COMPILE")){pi.flags|=VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;gi.flags|=VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;}
 double ms[2],gms[2]={0};unsigned hits[2];

 for(unsigned pass=0;pass<2;pass++){
  iz2_start_begin();iz2_cache_install(&c,code,bytes);assert(iz2_pipeline_cache);assert(iz2_cache_hit==(pass!=0));
  VkPipeline pipeline;u64 start=armGetSystemTick();assert(!FN(&c,F_CREATE_COMPUTE)(c.device,0,1,(u64)(uintptr_t)&pi,0,(u64)(uintptr_t)&pipeline,0,0));ms[pass]=(armGetSystemTick()-start)/1e6;if(argc==4){VkPipeline gp;u64 t=armGetSystemTick();assert(!FN(&c,F_CREATE_GRAPHICS)(c.device,0,1,(u64)(uintptr_t)&gi,0,(u64)(uintptr_t)&gp,0,0));gms[pass]=(armGetSystemTick()-t)/1e6;vkDestroyPipeline(device,gp,NULL);}
  hits[pass]=iz2_pipeline_hits;
  iz2_cache_unhook(&c);assert(c.functions[F_CREATE_COMPUTE]==(u64)(uintptr_t)compute);
  if(!pass){assert(access(IZ2_CACHE_PATH,F_OK));iz2_cache_save();assert(!access(IZ2_CACHE_PATH,F_OK));}
  iz2_menu_cache_retire();assert(!iz2_pipeline_cache&&!iz2_cache_device);
  // This pipeline is still alive when the optional cache is released.
  VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool pool;
  assert(!vkCreateCommandPool(device,&cp,NULL,&pool));
  VkCommandBufferAllocateInfo cb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;
  assert(!vkAllocateCommandBuffers(device,&cb,&command));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};assert(!vkBeginCommandBuffer(command,&begin));
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);assert(!vkEndCommandBuffer(command));
  VkQueue queue;vkGetDeviceQueue(device,0,0,&queue);
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
  assert(!vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));assert(!vkQueueWaitIdle(queue));
  // Repeated retire calls on later launch/return transitions are harmless.
  iz2_menu_cache_retire();assert(!iz2_pipeline_cache&&!iz2_cache_device);
  vkDestroyCommandPool(device,pool,NULL);vkDestroyPipeline(device,pipeline,NULL);
 }
 size_t file_bytes;uint8_t *saved=read_file(IZ2_CACHE_PATH,&file_bytes);Iz2CacheHeader *h=(void*)saved;uint8_t *payload=saved+sizeof *h;
 assert(iz2_cache_valid(h,payload,file_bytes-sizeof *h,&iz2_cache_identity));
 /* A new interface must replace the old cache on a non-overwriting FS. */
 iz2_cache_open(&c,"changed-interface",17);assert(!iz2_cache_hit);
 iz2_cache_save();iz2_cache_close();
 iz2_cache_open(&c,"changed-interface",17);assert(iz2_cache_hit);iz2_cache_close();
 assert(rename_existing>0);
#ifndef IZ2_CACHE_REPRO_ONLY
 /* A complete temporary cache survives publication failure or power loss. */
 h->shader[0]^=1;write_file(IZ2_CACHE_PATH,saved,file_bytes);h->shader[0]^=1;
 write_file(IZ2_CACHE_PATH ".tmp",saved,file_bytes);
 iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit&&iz2_cache_from_tmp);
 iz2_pipeline_requests=iz2_pipeline_hits=1;iz2_cache_save();assert(!strcmp(iz2_cache_write,"recovered"));iz2_cache_close();
 assert(access(IZ2_CACHE_PATH ".tmp",F_OK));
 iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit&&!iz2_cache_from_tmp);iz2_cache_close();
 /* Permission failure must preserve both caches and report the errno. */
 h->shader[0]^=1;write_file(IZ2_CACHE_PATH,saved,file_bytes);h->shader[0]^=1;
 write_file(IZ2_CACHE_PATH ".tmp",saved,file_bytes);
 iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit&&iz2_cache_from_tmp);
 iz2_pipeline_requests=iz2_pipeline_hits=1;rename_fault=EACCES;iz2_cache_save();
 assert(iz2_cache_errno==EACCES&&!strcmp(iz2_cache_write,"failed"));iz2_cache_close();
 assert(!access(IZ2_CACHE_PATH,F_OK)&&!access(IZ2_CACHE_PATH ".tmp",F_OK));
 /* Failure between unlink and rename: next startup accepts only valid tmp. */
 rename_fault=EIO;iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit&&iz2_cache_from_tmp);
 iz2_pipeline_requests=iz2_pipeline_hits=1;iz2_cache_save();assert(iz2_cache_errno==EIO);iz2_cache_close();
 assert(access(IZ2_CACHE_PATH,F_OK)&&!access(IZ2_CACHE_PATH ".tmp",F_OK));
 rename_fault=0;iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit&&iz2_cache_from_tmp);
 iz2_pipeline_requests=iz2_pipeline_hits=1;iz2_cache_save();assert(!strcmp(iz2_cache_write,"recovered"));iz2_cache_close();
 /* A corrupt temporary file must not bypass any compatibility checks. */
 assert(!unlink(IZ2_CACHE_PATH));payload[h->bytes-1]^=1;write_file(IZ2_CACHE_PATH ".tmp",saved,file_bytes);payload[h->bytes-1]^=1;
 iz2_cache_open(&c,code,bytes);assert(!iz2_cache_hit);iz2_cache_close();assert(!unlink(IZ2_CACHE_PATH ".tmp"));
 write_file(IZ2_CACHE_PATH,saved,file_bytes);
 iz2_cache_open(&c,code,bytes);assert(iz2_cache_hit);iz2_cache_close();
#endif

 payload[file_bytes-sizeof *h-1]^=1;assert(!iz2_cache_valid(h,payload,file_bytes-sizeof *h,&iz2_cache_identity));payload[file_bytes-sizeof *h-1]^=1;
 h->uuid[0]^=1;assert(!iz2_cache_valid(h,payload,h->bytes,&iz2_cache_identity));h->uuid[0]^=1;
 h->driver^=1;assert(!iz2_cache_valid(h,payload,h->bytes,&iz2_cache_identity));h->driver^=1;
 h->shader[0]^=1;assert(!iz2_cache_valid(h,payload,h->bytes,&iz2_cache_identity));h->shader[0]^=1;
 assert(!iz2_cache_valid(h,payload,h->bytes-1,&iz2_cache_identity));assert(!iz2_cache_valid(h,payload,IZ2_CACHE_LIMIT+1,&iz2_cache_identity));
 FILE*f=fopen(IZ2_CACHE_PATH,"wb");assert(f&&fwrite(saved,1,17,f)==17&&!fclose(f));iz2_cache_open(&c,code,bytes);assert(iz2_pipeline_cache&&!iz2_cache_hit);iz2_cache_close();
 printf("{\"passed\":true,\"filesystem\":\"non-overwriting rename\",\"replacementAndRecoveryTested\":true,\"backend\":\"host Vulkan\",\"coldMs\":%.3f,\"warmMs\":%.3f,\"graphicsMs\":[%.3f,%.3f],\"reportedHits\":[%u,%u],\"cacheBytes\":%zu,\"rejected\":[\"corrupt digest\",\"different GPU UUID\",\"different driver\",\"different shader\",\"truncated\",\"oversized\"]}\n",ms[0],ms[1],gms[0],gms[1],hits[0],hits[1],file_bytes);
 if(renderpass)vkDestroyRenderPass(device,renderpass,NULL);if(vs)vkDestroyShaderModule(device,vs,NULL);if(frag)vkDestroyShaderModule(device,frag,NULL);
 free(saved);free(code);vkDestroyShaderModule(device,shader,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
}
