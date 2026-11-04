// Desktop execution of the actual game SPIR-V. No connection to the console.
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed: %s at %d\n",#x,__LINE__);exit(1);}}while(0)
#define VK(x) do{VkResult r=(x);if(r){fprintf(stderr,"Vulkan %s: %d\n",#x,r);exit(1);}}while(0)
static const size_t words=256;
static void *read_file(const char *name,size_t *length){FILE*f=fopen(name,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0&&n<16*1024*1024);rewind(f);void*p=malloc(n);CHECK(p&&fread(p,1,n,f)==(size_t)n);fclose(f);*length=n;return p;}
static VkPipeline pipeline(VkDevice device,VkPipelineLayout layout,const char *file){
 size_t len;void*code=read_file(file,&len);CHECK(!(len%4));
 VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=len,.pCode=code};VkShaderModule module;VK(vkCreateShaderModule(device,&mi,0,&module));free(code);
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
 .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
 VkPipeline result;VK(vkCreateComputePipelines(device,0,1,&pi,0,&result));vkDestroyShaderModule(device,module,0);return result;
}
int main(int argc,char**argv){
 CHECK(argc==6);unsigned frames=strtoul(argv[5],0,10);CHECK(frames>0&&frames<=3600);
 size_t len;void*initial=read_file(argv[1],&len);CHECK(len>=words*4&&len<=8*1024*1024);size_t inputLen;void*inputs=read_file(argv[4],&inputLen);size_t inputStride=getenv("HB_TOUCH_INPUT")?1024:64;CHECK(inputLen==(size_t)frames*inputStride);
 VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="HBLauncher game compute checks",.apiVersion=VK_API_VERSION_1_0};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};VkInstance instance;VK(vkCreateInstance(&ici,0,&instance));
 uint32_t count=0;VK(vkEnumeratePhysicalDevices(instance,&count,0));CHECK(count>0&&count<=16);VkPhysicalDevice devices[16];VK(vkEnumeratePhysicalDevices(instance,&count,devices));VkPhysicalDevice physical=devices[0];
 for(unsigned i=0;i<count;i++){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(devices[i],&p);if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU||p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){physical=devices[i];break;}}
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);fprintf(stderr,"Device: %s (type %u)\n",props.deviceName,props.deviceType);
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,0);CHECK(count<=64);VkQueueFamilyProperties families[64];vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);unsigned family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;CHECK(family<count);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;VK(vkCreateDevice(physical,&di,0,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=8*1024*1024,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer;VK(vkCreateBuffer(device,&bi,0,&buffer));
 VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(physical,&mp);unsigned type=0;for(;type<mp.memoryTypeCount;type++)if((req.memoryTypeBits&(1u<<type))&&(mp.memoryTypes[type].propertyFlags&6)==6)break;CHECK(type<mp.memoryTypeCount);
 VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VkDeviceMemory memory;VK(vkAllocateMemory(device,&mai,0,&memory));VK(vkBindBufferMemory(device,buffer,memory,0));uint32_t*mapped;VK(vkMapMemory(device,memory,0,bi.size,0,(void**)&mapped));memset(mapped,0,bi.size);memcpy(mapped,initial,len);free(initial);
 VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo sli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout sl;VK(vkCreateDescriptorSetLayout(device,&sli,0,&sl));
 VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};VkPipelineLayout layout;VK(vkCreatePipelineLayout(device,&pli,0,&layout));
 VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};VkDescriptorPool pool;VK(vkCreateDescriptorPool(device,&dpi,0,&pool));
 VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};VkDescriptorSet set;VK(vkAllocateDescriptorSets(device,&sai,&set));
 VkDescriptorBufferInfo dbi={buffer,0,bi.size};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};vkUpdateDescriptorSets(device,1,&write,0,0);
 fprintf(stderr,"Compiling compute pipelines...\n");VkPipeline cpu=pipeline(device,layout,argv[3]);
 VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool cp;VK(vkCreateCommandPool(device,&cpi,0,&cp));
 VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cmd;VK(vkAllocateCommandBuffers(device,&cai,&cmd));
 VkCommandBufferBeginInfo cbi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cmd,&cbi));
 VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,0,0,0);
 vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,0);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cpu);vkCmdDispatch(cmd,1,1,1);
 barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,0,0,0);VK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
 struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
 for(unsigned f=0;f<frames;f++){
  const uint8_t*input=(uint8_t*)inputs+f*inputStride;memcpy(mapped,input,64);if(inputStride==1024)memcpy((uint8_t*)mapped+128,input+128,656);
  VK(vkQueueSubmit(queue,1,&submit,0));VK(vkQueueWaitIdle(queue));
 }
 clock_gettime(CLOCK_MONOTONIC,&end);double elapsed=end.tv_sec-start.tv_sec+(end.tv_nsec-start.tv_nsec)*1e-9;
 size_t outputBytes=getenv("HB_FULL_OUTPUT")?bi.size:words*4;
 FILE*out=fopen(argv[2],"wb");CHECK(out&&fwrite(mapped,1,outputBytes,out)==outputBytes);fclose(out);
 printf("{\"frames\":%u,\"seconds\":%.6f,\"consoleExecuted\":false}\n",frames,elapsed);
 vkDeviceWaitIdle(device);vkDestroyCommandPool(device,cp,0);vkDestroyPipeline(device,cpu,0);vkDestroyDescriptorPool(device,pool,0);vkDestroyPipelineLayout(device,layout,0);vkDestroyDescriptorSetLayout(device,sl,0);vkUnmapMemory(device,memory);vkDestroyBuffer(device,buffer,0);vkFreeMemory(device,memory,0);vkDestroyDevice(device,0);vkDestroyInstance(instance,0);free(inputs);return 0;
}
