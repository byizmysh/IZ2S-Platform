/* Run the production compositor on Vulkan and verify its image on the CPU. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef struct {
    uintptr_t device;
    uintptr_t graphics_physical;
} IzContext;
#include "../../native/split-compositor.h"

static uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits,
                            VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t type = 0; type < properties.memoryTypeCount; type++)
        if ((bits & (1u << type)) &&
            (properties.memoryTypes[type].propertyFlags & flags) == flags)
            return type;
    assert(!"required Vulkan memory type");
    return 0;
}

static void transition(VkCommandBuffer commands, VkImage image,
                       VkImageLayout before, VkImageLayout after,
                       VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                       VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage, .srcAccessMask = src_access,
        .dstStageMask = dst_stage, .dstAccessMask = dst_access,
        .oldLayout = before, .newLayout = after,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
}

static void fill_target(VkCommandBuffer commands, const IzSplitTarget *target,
                        unsigned channel)
{
    transition(commands, target->image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    split_begin_render(commands, target->view);
    VkClearAttachment attachment = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT };
    attachment.clearValue.color.float32[channel] = 1;
    attachment.clearValue.color.float32[3] = 1;
    const VkClearRect rect = {
        .rect = { { 0, 0 }, { SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT / 2 } },
        .layerCount = 1,
    };
    vkCmdClearAttachments(commands, 1, &attachment, 1, &rect);
    vkCmdEndRendering(commands);
    transition(commands, target->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

static void expect_pixel(const uint8_t *pixels, unsigned x, unsigned y,
                         uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t *pixel = pixels + 4 * (y * SPLIT_FRAME_WIDTH + x);
    assert(pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == 255);
}

static void check_windows(const uint8_t *pixels, unsigned visible, unsigned focus,
                           unsigned sound, const IzSplitUi *ui)
{
    IzSplitLayout layout=split_layout(ui,visible);
    for(unsigned slot=0;slot<2;slot++) {
        unsigned pane=1u<<slot;
        if(!(visible&pane))continue;
        IzSplitRect r=layout.content[slot],f=layout.frame[slot];
        unsigned x=(unsigned)(r.x+r.w/4),y=(unsigned)(r.y+r.h/4);
        if(split_hit_pane(ui,&layout,visible,x,y)==pane)
            expect_pixel(pixels,x,y,slot==0?255:0,slot==1?255:0,0);
        if(!ui->menu) {
            x=(unsigned)(r.x+1);y=(unsigned)(r.y+r.h/4);
            if(split_hit_pane(ui,&layout,visible,x,y)==pane)
                expect_pixel(pixels,x,y,slot==0?255:0,slot==1?255:0,0);
            continue;
        }
        x=(unsigned)(f.x+f.w-22);y=(unsigned)(f.y+16);
        if(split_hit_pane(ui,&layout,visible,x,y)==pane) {
            const uint8_t *p=pixels+4*(y*SPLIT_FRAME_WIDTH+x);
            if(sound==pane)assert(p[1]>210&&p[2]>155);
            else assert(p[1]<200&&p[2]<200);
        }
        x=(unsigned)(f.x+1);y=(unsigned)(f.y+40);
        if(split_hit_pane(ui,&layout,visible,x,y)==pane) {
            const uint8_t *p=pixels+4*(y*SPLIT_FRAME_WIDTH+x);
            if(focus==pane)assert(p[2]>150&&p[2]>p[1]&&p[1]>p[0]);
            else assert(p[2]<80);
        }
    }
}

static void check_readback_windows(const uint8_t *pixels, unsigned visible, const IzSplitUi *ui,
                                  unsigned readback_sources)
{
    const IzSplitLayout layout = split_layout(ui, visible);
    for (unsigned slot = 0; slot < 2; slot++) {
        const unsigned pane = 1u << slot;
        if (!(visible & pane)) continue;
        const IzSplitRect r = layout.content[slot];
        unsigned correct = 0, samples = 0;
        // The native fixture fills only the upper half of its target.
        for (unsigned row = 1; row < 4; row++) {
            for (unsigned column = 1; column < 8; column++) {
                const unsigned x = (unsigned)(r.x + r.w * column / 8);
                const unsigned y = (unsigned)(r.y + r.h * row / 8);
                if (split_hit_pane(ui, &layout, visible, x, y) != pane) continue;
                const uint8_t *pixel = pixels + 4 * (y * SPLIT_FRAME_WIDTH + x);
                if (readback_sources & pane) {
                    correct += slot == 0 ? pixel[2] > pixel[0] + 40 : pixel[0] > pixel[2] + 40;
                } else {
                    correct += pixel[slot] == 255 && pixel[1 - slot] == 0 && pixel[2] == 0;
                }
                samples++;
            }
        }
        assert(samples > 0 && correct > samples / 2);
    }
}

static void write_preview(const char *path, const uint8_t *pixels)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT);
    for (unsigned i = 0; i < SPLIT_FRAME_WIDTH * SPLIT_FRAME_HEIGHT; i++) {
        assert(fwrite(pixels + i * 4, 1, 3, file) == 3);
    }
    assert(!fclose(file));
}

int main(int argc, char **argv)
{
    assert(argc == 1 || argc == 4);
    const bool from_readback = argc == 4;
    unsigned readback_sources = 0;
    if (from_readback) {
        for (unsigned slot = 0; slot < 2; slot++) {
            if (strcmp(argv[1 + slot], "-")) readback_sources |= 1u << slot;
        }
        assert(readback_sources);
    }
    VkInstance instance;
    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
    };
    assert(!vkCreateInstance(&instance_info, NULL, &instance));
    VkPhysicalDevice physical;
    uint32_t devices = 1;
    VkResult found = vkEnumeratePhysicalDevices(instance, &devices, &physical);
    assert((found == VK_SUCCESS || found == VK_INCOMPLETE) && devices == 1);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    VkQueueFamilyProperties queues[64];
    uint32_t queue_count = 64, queue_index = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues);
    while (queue_index < queue_count && !(queues[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        queue_index++;
    assert(queue_index < queue_count);
    const float priority = 1;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queue_index,
        .queueCount = 1, .pQueuePriorities = &priority,
    };
    const VkPhysicalDeviceVulkan13Features features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    assert(!vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, queue_index, 0, &queue);
    IzContext context = { (uintptr_t)device, (uintptr_t)physical };
    IzSplitCompositor compositor = {0};
    assert(!split_compositor_create_sources(&context, &compositor, readback_sources));

    VkImage final_image;
    const VkImageCreateInfo final_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = SPLIT_FRAME_FORMAT, .extent = { SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };
    assert(!vkCreateImage(device, &final_info, NULL, &final_image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, final_image, &requirements);
    VkMemoryAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory final_memory;
    assert(!vkAllocateMemory(device, &alloc, NULL, &final_memory));
    assert(!vkBindImageMemory(device, final_image, final_memory, 0));
    const VkImageViewCreateInfo final_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = final_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = SPLIT_FRAME_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkImageView final_view;
    assert(!vkCreateImageView(device, &final_view_info, NULL, &final_view));

    const VkBufferCreateInfo readback_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = SPLIT_FRAME_WIDTH * SPLIT_FRAME_HEIGHT * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VkBuffer readback;
    assert(!vkCreateBuffer(device, &readback_info, NULL, &readback));
    vkGetBufferMemoryRequirements(device, readback, &requirements);
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory readback_memory;
    assert(!vkAllocateMemory(device, &alloc, NULL, &readback_memory));
    assert(!vkBindBufferMemory(device, readback, readback_memory, 0));

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    const VkDeviceSize frame_bytes = SPLIT_READBACK_WIDTH * SPLIT_READBACK_HEIGHT * 4;
    if (from_readback) {
        const VkBufferCreateInfo source_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = 2 * frame_bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        assert(!vkCreateBuffer(device, &source_info, NULL, &staging));
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(!vkAllocateMemory(device, &alloc, NULL, &staging_memory));
        assert(!vkBindBufferMemory(device, staging, staging_memory, 0));
        void *mapped;
        assert(!vkMapMemory(device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        for (unsigned slot = 0; slot < 2; slot++) {
            if (!(readback_sources & (1u << slot))) continue;
            FILE *file = fopen(argv[1 + slot], "rb");
            assert(file);
            assert(fread((uint8_t *)mapped + slot * frame_bytes, 1, frame_bytes, file) == frame_bytes);
            assert(fgetc(file) == EOF && !ferror(file));
            assert(!fclose(file));
        }
        vkUnmapMemory(device, staging_memory);
    }

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = queue_index,
    };
    VkCommandPool pool;
    assert(!vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo command_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    assert(!vkAllocateCommandBuffers(device, &command_alloc, &command));
    const VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    split_status_name(&compositor, 0, readback_sources & 1 ? "NRO Blue" : "IZtwiNS2");
    split_status_name(&compositor, 1, readback_sources & 2 ? "NRO Orange" : "IZPSX emulator");
    for (unsigned visible = 1; visible <= 3; visible++) {
        assert(!vkBeginCommandBuffer(command, &begin));
        for (unsigned slot = 0; slot < 2; slot++) {
            if (!(visible & (1u << slot))) continue;
            if (readback_sources & (1u << slot)) {
                split_upload_readback(command, &compositor.targets[slot], staging, slot * frame_bytes);
            } else {
                fill_target(command, &compositor.targets[slot], slot);
            }
        }
        transition(command, final_image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        split_begin_render(command, final_view);
        split_compose(command, &compositor, visible);
        vkCmdEndRendering(command);
        transition(command, final_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        const VkBufferImageCopy copy = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT, 1 },
        };
        vkCmdCopyImageToBuffer(command, final_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback, 1, &copy);
        assert(!vkEndCommandBuffer(command));
        const VkSubmitInfo submission = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command,
        };
        void *mapped;
        assert(!vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        for (unsigned frame = 0; frame < 16; frame++) {
            const unsigned focus = visible == 3 ? 1 + (frame / 3) % 2 : visible;
            const unsigned sound = frame % 3 == 0 ? 0 : (visible == 3 ? frame % 3 : visible);
            IzSplitUi ui={.menu=frame%2,.floating=frame>=4,.swapped=frame%2,.pip_slot=(frame/4)%2,
                .corner=frame%4,.size=frame%5,.selected=frame%8};
            split_status_update(&compositor, focus, sound, &ui, visible);
            assert(!vkQueueSubmit(queue, 1, &submission, VK_NULL_HANDLE));
            assert(!vkQueueWaitIdle(queue));
            if (from_readback) check_readback_windows(mapped, visible, &ui, readback_sources);
            else check_windows(mapped, visible, focus, sound, &ui);
            if(ui.menu) {
                const uint8_t *tile=(uint8_t*)mapped+4*(1020*SPLIT_FRAME_WIDTH+272);
                assert(tile[2]>tile[0] && tile[0]>10);
            } else if (visible==SPLIT_BOTH && !ui.floating) {
                expect_pixel(mapped,100,1020,0,0,0);
            }
            if (from_readback && visible == SPLIT_BOTH && frame == 1) {
                write_preview(argv[3], mapped);
            }
            if(!from_readback && visible==3&&(frame==1||frame==7||frame==8)) {
                char name[64];snprintf(name,sizeof name,"/tmp/run33-ui-%s.ppm",frame==1?"split":"floating");
                if(frame==8)snprintf(name,sizeof name,"/tmp/run33-ui-hidden.ppm");
                write_preview(name, mapped);
            }
        }
        vkUnmapMemory(device, readback_memory);
        assert(!vkResetCommandPool(device, pool, 0));
    }
    vkDestroyCommandPool(device, pool, NULL);
    if (staging) vkDestroyBuffer(device, staging, NULL);
    if (staging_memory) vkFreeMemory(device, staging_memory, NULL);
    vkDestroyBuffer(device, readback, NULL);
    vkFreeMemory(device, readback_memory, NULL);
    vkDestroyImageView(device, final_view, NULL);
    vkDestroyImage(device, final_image, NULL);
    vkFreeMemory(device, final_memory, NULL);
    split_compositor_destroy(device, &compositor);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("PASS: 48 Vulkan frames with %s, split/PiP, swaps and corners (%s)\n",
           from_readback ? "independent NRO readbacks" : "native target fixtures", properties.deviceName);
}
