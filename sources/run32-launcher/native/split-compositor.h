#ifndef IZ_SPLIT_COMPOSITOR_H
#define IZ_SPLIT_COMPOSITOR_H

#include <vulkan/vulkan.h>
#include "split-layout.h"
#include "split-composite-shaders.h"
#include "split-font.h"

enum { SPLIT_FRAME_WIDTH = 1920, SPLIT_FRAME_HEIGHT = 1080 };
enum { SPLIT_DYNAMIC_STATUS_WORDS = 23, SPLIT_STATUS_WORDS = 32 };
#define SPLIT_FRAME_FORMAT VK_FORMAT_R8G8B8A8_UNORM

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet descriptor;
} IzSplitTarget;

typedef struct {
    IzSplitTarget targets[2];
    VkSampler sampler;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkDescriptorPool pool;
    VkPipeline pipeline;
    VkBuffer status_buffer;
    VkDeviceMemory status_memory;
    uint32_t *status;
    uint32_t last_status[SPLIT_DYNAMIC_STATUS_WORDS];
} IzSplitCompositor;

#define SPLIT_STATUS_BYTES (SPLIT_STATUS_WORDS * sizeof(uint32_t) + sizeof split_font_words)
_Static_assert(SPLIT_STATUS_BYTES <= 16384, "Split UI must fit the Vulkan minimum uniform range");

static void split_target_destroy(VkDevice device, IzSplitTarget *target)
{
    if (target->view) vkDestroyImageView(device, target->view, NULL);
    if (target->image) vkDestroyImage(device, target->image, NULL);
    if (target->memory) vkFreeMemory(device, target->memory, NULL);
    *target = (IzSplitTarget){0};
}

static void split_compositor_destroy(VkDevice device, IzSplitCompositor *compositor)
{
    if (compositor->pipeline) vkDestroyPipeline(device, compositor->pipeline, NULL);
    if (compositor->pool) vkDestroyDescriptorPool(device, compositor->pool, NULL);
    if (compositor->layout) vkDestroyPipelineLayout(device, compositor->layout, NULL);
    if (compositor->set_layout)
        vkDestroyDescriptorSetLayout(device, compositor->set_layout, NULL);
    if (compositor->sampler) vkDestroySampler(device, compositor->sampler, NULL);
    if (compositor->status) vkUnmapMemory(device, compositor->status_memory);
    if (compositor->status_buffer) vkDestroyBuffer(device, compositor->status_buffer, NULL);
    if (compositor->status_memory) vkFreeMemory(device, compositor->status_memory, NULL);
    for (unsigned slot = 0; slot < 2; slot++)
        split_target_destroy(device, &compositor->targets[slot]);
    memset(compositor, 0, sizeof *compositor);
}

static VkResult split_status_create(VkDevice device, IzSplitCompositor *compositor,
                                    const VkPhysicalDeviceMemoryProperties *memory)
{
    const VkBufferCreateInfo buffer = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = SPLIT_STATUS_BYTES, .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };
    VkResult result = vkCreateBuffer(device, &buffer, NULL, &compositor->status_buffer);
    if (result) return result;

    VkMemoryRequirements needed;
    vkGetBufferMemoryRequirements(device, compositor->status_buffer, &needed);
    const VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = 0;
    while (type < memory->memoryTypeCount &&
           (!(needed.memoryTypeBits & (UINT32_C(1) << type)) ||
            (memory->memoryTypes[type].propertyFlags & flags) != flags))
        type++;
    if (type == memory->memoryTypeCount) return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needed.size, .memoryTypeIndex = type,
    };
    result = vkAllocateMemory(device, &alloc, NULL, &compositor->status_memory);
    if (!result) result = vkBindBufferMemory(device, compositor->status_buffer,
                                              compositor->status_memory, 0);
    void *mapped = NULL;
    if (!result) result = vkMapMemory(device, compositor->status_memory, 0,
                                       buffer.size, 0, &mapped);
    if (!result) {
        compositor->status = mapped;
        memset(mapped, 0, SPLIT_STATUS_WORDS * sizeof(uint32_t));
        memset(compositor->last_status, 0, sizeof compositor->last_status);
        memcpy(compositor->status + SPLIT_STATUS_WORDS, split_font_words, sizeof split_font_words);
    }
    return result;
}

static bool split_status_update(IzSplitCompositor *compositor, unsigned focus, unsigned sound,
                                 const IzSplitUi *ui, unsigned visible, const IzSplitLayout *layout)
{
    uint32_t status[SPLIT_DYNAMIC_STATUS_WORDS] = {
        focus, sound, visible, ui->floating,
        [20] = ui->selected, [21] = ui->menu, [22] = ui->pip_slot,
    };
    memcpy(status + 4, layout->frame, sizeof layout->frame);
    memcpy(status + 12, layout->content, sizeof layout->content);
    if (!memcmp(status, compositor->last_status, sizeof status)) return false;
    /* The preceding present_at waited for the queue before this CPU write. */
    memcpy(compositor->status, status, sizeof status);
    memcpy(compositor->last_status, status, sizeof status);
    return true;
}

static void split_status_name(IzSplitCompositor *compositor, unsigned slot, const char *name)
{
    uint32_t *packed = compositor->status + 24 + slot * 4;
    for (unsigned i = 0; i < 16 && name[i]; i++) {
        unsigned ch = (unsigned char)name[i];
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        packed[i / 4] |= (uint32_t)ch << (8 * (i % 4));
    }
}

static VkResult split_target_create(VkDevice device, IzSplitTarget *target,
                                    const VkPhysicalDeviceMemoryProperties *memory)
{
    const VkImageCreateInfo image = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = SPLIT_FRAME_FORMAT,
        .extent = { SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(device, &image, NULL, &target->image);
    if (result) return result;

    VkMemoryRequirements needed;
    vkGetImageMemoryRequirements(device, target->image, &needed);
    uint32_t type = 0;
    while (type < memory->memoryTypeCount) {
        if ((needed.memoryTypeBits & (UINT32_C(1) << type)) &&
            (memory->memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            break;
        type++;
    }
    if (type == memory->memoryTypeCount) return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needed.size,
        .memoryTypeIndex = type,
    };
    result = vkAllocateMemory(device, &alloc, NULL, &target->memory);
    if (result) return result;
    result = vkBindImageMemory(device, target->image, target->memory, 0);
    if (result) return result;
    const VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = SPLIT_FRAME_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    return vkCreateImageView(device, &view, NULL, &target->view);
}

static VkResult split_compositor_pipeline(VkDevice device, IzSplitCompositor *compositor)
{
    VkShaderModule stages[2] = {0};
    const VkShaderModuleCreateInfo shaders[] = {
        { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = sizeof split_composite_vert, .pCode = split_composite_vert },
        { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = sizeof split_composite_frag, .pCode = split_composite_frag },
    };
    VkResult result = vkCreateShaderModule(device, &shaders[0], NULL, &stages[0]);
    if (!result) result = vkCreateShaderModule(device, &shaders[1], NULL, &stages[1]);
    if (!result) {
        const VkPipelineShaderStageCreateInfo stage[] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = stages[0], .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = stages[1], .pName = "main" },
        };
        const VkPipelineVertexInputStateCreateInfo vertex = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };
        const VkPipelineInputAssemblyStateCreateInfo assembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        const VkPipelineViewportStateCreateInfo viewport = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .scissorCount = 1,
        };
        const VkPipelineRasterizationStateCreateInfo raster = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1,
        };
        const VkPipelineMultisampleStateCreateInfo sample = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        const VkPipelineColorBlendAttachmentState attachment = { .colorWriteMask = 15 };
        const VkPipelineColorBlendStateCreateInfo color = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &attachment,
        };
        const VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        const VkPipelineDynamicStateCreateInfo dynamic = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2, .pDynamicStates = states,
        };
        const VkFormat format = SPLIT_FRAME_FORMAT;
        const VkPipelineRenderingCreateInfo rendering = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1, .pColorAttachmentFormats = &format,
        };
        const VkGraphicsPipelineCreateInfo pipeline = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering, .stageCount = 2, .pStages = stage,
            .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
            .pViewportState = &viewport, .pRasterizationState = &raster,
            .pMultisampleState = &sample, .pColorBlendState = &color,
            .pDynamicState = &dynamic, .layout = compositor->layout,
            .basePipelineIndex = -1,
        };
        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline,
                                           NULL, &compositor->pipeline);
    }
    for (unsigned i = 0; i < 2; i++)
        if (stages[i]) vkDestroyShaderModule(device, stages[i], NULL);
    return result;
}

static VkResult split_compositor_create(IzContext *c, IzSplitCompositor *compositor)
{
    VkDevice device = (VkDevice)(uintptr_t)c->device;
    VkPhysicalDevice physical = (VkPhysicalDevice)(uintptr_t)c->graphics_physical;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(physical, SPLIT_FRAME_FORMAT, &format);
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((format.optimalTilingFeatures & needed) != needed)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkResult result = split_status_create(device, compositor, &memory);
    if (result) return result;
    for (unsigned slot = 0; slot < 2; slot++) {
        result = split_target_create(device, &compositor->targets[slot], &memory);
        if (result) return result;
    }

    const VkFilter filter = (format.optimalTilingFeatures &
                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ?
        VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter, .minFilter = filter,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    result = vkCreateSampler(device, &sampler, NULL, &compositor->sampler);
    if (result) return result;
    const VkDescriptorSetLayoutBinding bindings[] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    const VkDescriptorSetLayoutCreateInfo set = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings,
    };
    result = vkCreateDescriptorSetLayout(device, &set, NULL, &compositor->set_layout);
    if (result) return result;
    const VkPushConstantRange mode = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .size = sizeof(uint32_t),
    };
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &compositor->set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &mode,
    };
    result = vkCreatePipelineLayout(device, &layout, NULL, &compositor->layout);
    if (result) return result;
    const VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 2, .pPoolSizes = sizes,
    };
    result = vkCreateDescriptorPool(device, &pool, NULL, &compositor->pool);
    if (result) return result;
    for (unsigned slot = 0; slot < 2; slot++) {
        IzSplitTarget *target = &compositor->targets[slot];
        const VkDescriptorSetAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = compositor->pool,
            .descriptorSetCount = 1, .pSetLayouts = &compositor->set_layout,
        };
        result = vkAllocateDescriptorSets(device, &alloc, &target->descriptor);
        if (result) return result;
        const VkDescriptorImageInfo texture = {
            .sampler = compositor->sampler, .imageView = target->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkDescriptorBufferInfo status = {
            .buffer = compositor->status_buffer, .range = SPLIT_STATUS_BYTES,
        };
        const VkWriteDescriptorSet writes[] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = target->descriptor, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .pImageInfo = &texture },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = target->descriptor, .dstBinding = 1, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &status },
        };
        vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    }
    return split_compositor_pipeline(device, compositor);
}

static void split_begin_render(VkCommandBuffer command, VkImageView view)
{
    const VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = { 0, 0, 0, 1 } } },
    };
    const VkRenderingInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT } },
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color,
    };
    vkCmdBeginRendering(command, &info);
}

static void split_draw_region(VkCommandBuffer command, VkPipelineLayout layout,
                              const VkRect2D *region, uint32_t mode)
{
    const VkViewport viewport = {
        (float)region->offset.x, (float)region->offset.y,
        (float)region->extent.width, (float)region->extent.height, 0, 1,
    };
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, region);
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof mode, &mode);
    vkCmdDraw(command, 3, 1, 0, 0);
}

static void split_compose(VkCommandBuffer command, const IzSplitCompositor *compositor,
                          unsigned visible)
{
    const VkRect2D region = { {0, 0}, {SPLIT_FRAME_WIDTH, SPLIT_FRAME_HEIGHT} };
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, compositor->pipeline);
    for (unsigned slot = 0; slot < 2; slot++) {
        if (!(visible & (1u << slot))) continue;
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            compositor->layout, 0, 1, &compositor->targets[slot].descriptor, 0, NULL);
        split_draw_region(command, compositor->layout, &region, slot + 1);
    }
    if (visible) split_draw_region(command, compositor->layout, &region, 3);
}

#endif
