/* Vulkan 1.1 path for Android drivers without dynamic rendering, push
 * descriptors or extended dynamic state. Included after Frame/s_cb. */
static struct { VkFormat format; int depth, clear; VkRenderPass pass; } s_android_passes[8];

static VkRenderPass android_render_pass(VkFormat format, int depth, int clear)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        if (s_android_passes[i].pass && s_android_passes[i].format == format
            && s_android_passes[i].depth == depth && s_android_passes[i].clear == clear)
            return s_android_passes[i].pass;
        if (!s_android_passes[i].pass) break;
    }
    if (i == 8) { LOGE("render pass cache full\n"); abort(); }
    VkAttachmentDescription attachments[2] = {0};
    attachments[0].format = format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = attachments[0].finalLayout = clear
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    attachments[1].format = s_depth_fmt;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = attachments[1].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color = {0, attachments[0].finalLayout};
    VkAttachmentReference z = {1, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    if (depth) subpass.pDepthStencilAttachment = &z;
    VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = depth ? 2 : 1;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    VkRenderPass pass;
    if (vkCreateRenderPass(s_dev, &info, NULL, &pass) != VK_SUCCESS) {
        LOGE("render pass creation failed\n"); abort();
    }
    s_android_passes[i].format = format;
    s_android_passes[i].depth = depth;
    s_android_passes[i].clear = clear;
    s_android_passes[i].pass = pass;
    return pass;
}

static void android_frame_reset(Frame *frame)
{
    for (uint32_t i = 0; i < frame->nframebuffers; i++)
        vkDestroyFramebuffer(s_dev, frame->framebuffers[i], NULL);
    frame->nframebuffers = 0;
    if (frame->descriptors) vkResetDescriptorPool(s_dev, frame->descriptors, 0);
}

static void VKAPI_PTR android_begin_rendering(VkCommandBuffer command, const VkRenderingInfo *info)
{
    int clear = info->pColorAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkRenderPass pass = android_render_pass(clear ? s_sc_format : VK_FORMAT_B8G8R8A8_UNORM,
        info->pDepthAttachment != NULL, clear);
    VkImageView views[2] = {info->pColorAttachments[0].imageView, VK_NULL_HANDLE};
    if (info->pDepthAttachment) views[1] = info->pDepthAttachment->imageView;
    VkFramebufferCreateInfo create = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    create.renderPass = pass;
    create.attachmentCount = info->pDepthAttachment ? 2 : 1;
    create.pAttachments = views;
    create.width = info->renderArea.extent.width;
    create.height = info->renderArea.extent.height;
    create.layers = 1;
    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(s_dev, &create, NULL, &framebuffer) != VK_SUCCESS) {
        LOGE("framebuffer creation failed\n"); abort();
    }
    if (s_f->nframebuffers == s_f->framebuffer_cap) {
        uint32_t cap = s_f->framebuffer_cap ? s_f->framebuffer_cap * 2 : 64;
        VkFramebuffer *next = realloc(s_f->framebuffers, cap * sizeof(*next));
        if (!next) abort();
        s_f->framebuffers = next;
        s_f->framebuffer_cap = cap;
    }
    s_f->framebuffers[s_f->nframebuffers++] = framebuffer;
    VkClearValue values[2] = {info->pColorAttachments[0].clearValue, {0}};
    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea = info->renderArea;
    begin.clearValueCount = clear ? 1 : 0;
    begin.pClearValues = values;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

static void VKAPI_PTR android_end_rendering(VkCommandBuffer command)
{
    vkCmdEndRenderPass(command);
}

static void VKAPI_PTR android_push_descriptors(VkCommandBuffer command, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t index, uint32_t count, const VkWriteDescriptorSet *writes)
{
    if (!s_f->descriptors) {
        VkDescriptorPoolSize sizes[2] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * 8192},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * 8192}
        };
        VkDescriptorPoolCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 8192;
        info.poolSizeCount = 2;
        info.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(s_dev, &info, NULL, &s_f->descriptors) != VK_SUCCESS) abort();
    }
    VkDescriptorSetLayout dsl = count == 7 ? s_dsl : s_pp_dsl;
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = s_f->descriptors;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &dsl;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(s_dev, &allocate, &set) != VK_SUCCESS) {
        LOGE("descriptor pool exhausted\n"); abort();
    }
    VkWriteDescriptorSet updated[7];
    if (count > 7) abort();
    for (uint32_t i = 0; i < count; i++) {
        updated[i] = writes[i];
        updated[i].dstSet = set;
    }
    vkUpdateDescriptorSets(s_dev, count, updated, 0, NULL);
    vkCmdBindDescriptorSets(command, point, layout, index, 1, &set, 0, NULL);
}
