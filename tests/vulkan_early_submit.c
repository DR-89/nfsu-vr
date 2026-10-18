/* cc -O1 -Ithird_party/SDL/src/video/khronos tests/vulkan_early_submit.c -o /tmp/vulkan_early_submit */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#define VK_CBS 8
static struct {
    VkCommandBuffer cbs[VK_CBS];
    uint32_t ncb;
    VkDeviceSize ring_off;
    VkCommandPool pool;
    VkFence fence;
    unsigned descriptors, framebuffers, garbage;
} frame, *s_f = &frame;
static VkCommandBuffer s_cb;
static VkQueue s_queue;
static uint32_t s_cb_gen, s_ring_gen, s_kick_draws, s_st_kicks;
static int s_dyn_valid, in_render;
static void *s_rt, *s_rt_depth;
static unsigned submits, ended, begun, waited, final_submits;
static int recording[VK_CBS];
static void end_rendering(void) { in_render = 0; }
static unsigned index_of(VkCommandBuffer buffer)
{
    for (unsigned i = 0; i < VK_CBS; i++) if (frame.cbs[i] == buffer) return i;
    abort();
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer)
{
    unsigned i = index_of(buffer);
    assert(!in_render && recording[i]);
    recording[i] = 0;
    ended++;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info)
{
    unsigned i = index_of(buffer);
    assert(!recording[i] && i == frame.ncb);
    assert(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    recording[i] = 1;
    begun++;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence)
{
    assert(queue == s_queue && count == 1);
    if (fence) { assert(fence == frame.fence); final_submits++; }
    else submits++;
    assert(info->commandBufferCount == 1 && *info->pCommandBuffers == s_cb);
    assert(!recording[index_of(s_cb)]);
    assert(!info->waitSemaphoreCount && !info->signalSemaphoreCount);
    return VK_SUCCESS;
}
static void garbage_free(void *unused)
{
    (void)unused;
    assert(waited);
    frame.descriptors = frame.framebuffers = frame.garbage = 0;
}
static VkDevice s_dev;
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout)
{
    (void)d;
    assert(count == 1 && *fences == frame.fence && all && timeout == UINT64_MAX);
    assert(final_submits == 1);
    waited++;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice d, uint32_t count, const VkFence *fences)
{
    (void)d;
    assert(waited && count == 1 && *fences == frame.fence);
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    (void)d; (void)flags;
    assert(waited && pool == frame.pool);
    assert(!frame.garbage && !frame.descriptors && !frame.framebuffers);
    memset(recording, 0, sizeof(recording));
    return VK_SUCCESS;
}
#define LOGE(...) ((void)0)
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_submit.h"

static void setting(const char *value, unsigned expected)
{
    assert(!setenv("RECOMP_VK_KICK", value, 1));
    s_kick_n = -1;
    s_kick_draws = 0;
    assert(!kick_due(0));
    assert(s_kick_n == (int)expected);
}
int main(void)
{
    for (unsigned i = 0; i < VK_CBS; i++) frame.cbs[i] = (VkCommandBuffer)(uintptr_t)(i + 1);
    s_cb = frame.cbs[0];
    recording[0] = 1;
    frame.ring_off = 123456;
    frame.descriptors = 17; frame.framebuffers = 19; frame.garbage = 23;
    setting("0", 0);
    s_kick_draws = 100000;
    assert(!kick_due(0) && !kick_due(1));
    setting("-1", 300);
    setting("invalid", 300);
    setting("999999999999999999999999", 300);
    setting("1000000", 1000000);
    setting("300", 300);
    s_kick_draws = 299;
    assert(!kick_due(0));
    s_kick_draws = 300;
    assert(kick_due(0) && !kick_due(1));
    s_kick_draws = 599;
    assert(!kick_due(1));
    s_kick_draws = 600;
    assert(kick_due(1));
    for (unsigned i = 0; i < VK_CBS - 1; i++) {
        s_kick_draws = 600;
        assert(kick_due(1));
        in_render = s_dyn_valid = 1;
        s_rt = s_rt_depth = &frame;
        kick_submit();
        assert(!in_render && !s_rt && !s_rt_depth && !s_dyn_valid);
        assert(frame.ncb == i + 1 && s_cb == frame.cbs[i + 1]);
        assert(s_cb_gen == i + 1 && !s_kick_draws);
        assert(frame.ring_off == 123456);
        assert(frame.descriptors == 17 && frame.framebuffers == 19 && frame.garbage == 23);
    }
    s_kick_draws = 1000000;
    assert(!kick_due(0) && !kick_due(1)); /* reserve final buffer for flip */
    assert(submits == 7 && ended == 7 && begun == 7 && s_st_kicks == 7);
    frame.fence = (VkFence)(uintptr_t)100;
    frame.pool = (VkCommandPool)(uintptr_t)101;
    flush_frame_wait();
    assert(final_submits == 1 && waited == 1);
    assert(!frame.ncb && s_cb == frame.cbs[0] && recording[0]);
    assert(!frame.ring_off && s_ring_gen == 1 && s_cb_gen == 8);
    assert(!s_kick_draws && !s_dyn_valid);
    puts("Vulkan early submit and ring flush: PASS");
    return 0;
}
