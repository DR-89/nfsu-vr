/* Early submits adapted from Anton Artemov's upstream 99ead072.
 * Included by nv2a_vk.c after ready(). Renderer thread only.
 * Each submit uses a fresh command buffer. Ring memory, descriptor pools,
 * framebuffers and garbage survive until the final same-queue fence (or
 * flush_frame_wait) completes ALL earlier submissions. */
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

/* Submit what is recorded and wait for it, then keep recording in the same
 * frame (ring full). */
static void flush_frame_wait(void)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    end_rendering();
    s_rt = NULL;
    s_rt_depth = NULL;
    vkEndCommandBuffer(s_cb);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cb;
    uint64_t perf_submit = nfsu_perf_begin();
    vkQueueSubmit(s_queue, 1, &si, s_f->fence);
    nfsu_perf_end(NFSU_PERF_VK_SUBMIT, perf_submit);
    uint64_t perf_fence = nfsu_perf_begin();
    vkWaitForFences(s_dev, 1, &s_f->fence, VK_TRUE, UINT64_MAX);
    nfsu_perf_end(NFSU_PERF_VK_FENCE, perf_fence);
    vkResetFences(s_dev, 1, &s_f->fence);
    garbage_free(s_f);
    s_f->ring_off = 0;
    s_ring_gen++;
    vkResetCommandPool(s_dev, s_f->pool, 0);
    s_f->ncb = 0;
    s_cb = s_f->cbs[0];
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s_cb, &bi);
    s_cb_gen++;
    s_dyn_valid = 0;
    s_kick_draws = 0;
}

static int s_kick_n = -1;

static int kick_due(int mid_pass)
{
    if (s_kick_n < 0) {
        const char *e = getenv("RECOMP_VK_KICK");
#ifdef __ANDROID__
        char property[PROP_VALUE_MAX];
        if (!e && __system_property_get("debug.nfsu.vk_kick", property) > 0)
            e = property;
#endif
        s_kick_n = 300;
        if (e && *e) {
            char *end;
            long n = strtol(e, &end, 10);
            if (!*end && n >= 0 && n <= 1000000)
                s_kick_n = (int)n;
        }
        LOGE("early submits: threshold %d draws (0 = off)\n", s_kick_n);
    }
    if (!s_kick_n || s_f->ncb + 1 >= VK_CBS)
        return 0;
    /* Ending a pass on a tile GPU can be expensive: prefer natural pass
     * boundaries, and tolerate twice as many draws in a continuing pass. */
    return s_kick_draws >= (uint32_t)s_kick_n * (mid_pass ? 2u : 1u);
}

static void kick_submit(void)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    end_rendering();
    s_rt = NULL;
    s_rt_depth = NULL;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cb;
    uint64_t perf_submit = nfsu_perf_begin();
    if (vkEndCommandBuffer(s_cb) != VK_SUCCESS
        || vkQueueSubmit(s_queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
        LOGE("early submit failed\n");
        abort();
    }
    nfsu_perf_end(NFSU_PERF_VK_SUBMIT, perf_submit);
    s_cb = s_f->cbs[++s_f->ncb];
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(s_cb, &bi) != VK_SUCCESS) {
        LOGE("early command buffer begin failed\n");
        abort();
    }
    s_cb_gen++;
    s_dyn_valid = 0;
    s_kick_draws = 0;
    s_st_kicks++;
}
