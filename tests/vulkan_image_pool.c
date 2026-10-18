/* Mock driver tests for the real allocator, no Vulkan device required.
 * cc -O1 -g -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos
 * tests/vulkan_image_pool.c -o /tmp/vulkan_image_pool
 */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VkDevice s_dev;
static VkPhysicalDeviceMemoryProperties s_memprops;
static int fail_allocate, fail_bind, fail_metadata;
static unsigned allocations, frees;
static VkDeviceMemory last_mem;
static VkDeviceSize last_offset;
static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < s_memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (s_memprops.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *callbacks, VkDeviceMemory *memory)
{
    (void)d; (void)info; (void)callbacks;
    if (fail_allocate) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *memory = (VkDeviceMemory)(uintptr_t)++allocations;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory memory,
    const VkAllocationCallbacks *callbacks)
{
    (void)d; (void)callbacks;
    assert(memory);
    frees++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d, VkImage image,
    VkDeviceMemory memory, VkDeviceSize offset)
{
    (void)d; (void)image;
    last_mem = memory;
    last_offset = offset;
    return fail_bind ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
static void *metadata_realloc(void *old, size_t size)
{
    return fail_metadata ? NULL : realloc(old, size);
}
#define realloc metadata_realloc
#define LOGE(...) ((void)0)
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_image_pool.h"
#undef realloc

static VkImage image(uint32_t n) { return (VkImage)(uintptr_t)n; }
static VkMemoryRequirements requirement(VkDeviceSize size, VkDeviceSize align)
{
    VkMemoryRequirements r = {size, align, 1};
    return r;
}
static void check_empty(void)
{
    assert(!s_pool_used && !s_pool_reserved);
    for (unsigned i = 0; i < POOL_MAX_BLOCKS; i++) assert(!s_pool[i].mem && !s_pool[i].free);
    for (unsigned i = 0; i < POOL_HASH; i++) assert(!s_pool_map[i].img);
    assert(allocations == frees);
}
static void check_alignment_and_merge(void)
{
    VkMemoryRequirements small = requirement(7, 1), aligned = requirement(12, 256);
    assert(pool_bind(image(1), &small));
    assert(last_offset == 0);
    VkDeviceMemory memory = last_mem;
    assert(pool_bind(image(2), &aligned));
    assert(last_mem == memory && last_offset == 256);
    assert(pool_bind(image(3), &small));
    assert(last_offset == 7); /* first-fit uses the alignment gap */
    pool_release(image(2));
    pool_release(image(1));
    pool_release(image(3));
    check_empty();
}
static void check_failures(void)
{
    VkMemoryRequirements r = requirement(1024, 256);
    fail_metadata = 1;
    assert(!pool_bind(image(1), &r));
    fail_metadata = 0;
    check_empty();
    fail_allocate = 1;
    assert(!pool_bind(image(1), &r));
    fail_allocate = 0;
    check_empty();
    fail_bind = 1;
    assert(!pool_bind(image(1), &r));
    fail_bind = 0;
    check_empty();
    assert(pool_bind(image(1), &r));
    VkDeviceSize used = s_pool_used;
    fail_bind = 1;
    assert(!pool_bind(image(2), &r));
    assert(s_pool_used == used);
    fail_bind = 0;
    pool_release(image(1));
    check_empty();
    r.memoryTypeBits = 2;
    assert(!pool_bind(image(1), &r)); /* no compatible device-local type */
    r = requirement(POOL_MAX_ALLOC + 1, 256);
    assert(!pool_bind(image(1), &r));
    s_pool_on = 0;
    r = requirement(1024, 256);
    assert(!pool_bind(image(1), &r));
    s_pool_on = 1;
    check_empty();
}
static void check_capacity(void)
{
    VkMemoryRequirements r = requirement(POOL_MAX_ALLOC, 256);
    for (unsigned i = 1; i <= POOL_MAX_BLOCKS * 2; i++) assert(pool_bind(image(i), &r));
    assert(s_pool_reserved == POOL_MAX_BLOCKS * POOL_BLOCK);
    assert(!pool_bind(image(100), &r));
    for (unsigned i = 1; i <= POOL_MAX_BLOCKS * 2; i++) pool_release(image(i));
    check_empty();
    s_memprops.memoryHeaps[0].size = POOL_BLOCK * 8;
    assert(pool_bind(image(1), &r));
    assert(pool_bind(image(2), &r));
    assert(!pool_bind(image(3), &r)); /* heap fraction limit */
    pool_release(image(1)); pool_release(image(2));
    check_empty();
    s_memprops.memoryHeaps[0].size = 8ull << 30;
}
static void check_hash_and_fragmentation(void)
{
    /* Fill the hash table, so collision chains also cross its last slot. */
    VkMemoryRequirements r = requirement(37, 64);
    for (unsigned i = 1; i <= POOL_HASH; i++) assert(pool_bind(image(i), &r));
    assert(!pool_bind(image(POOL_HASH + 1), &r));
    for (unsigned i = 1; i <= POOL_HASH; i += 2) pool_release(image(i));
    for (unsigned i = 1; i <= POOL_HASH; i += 2) assert(pool_bind(image(i + POOL_HASH), &r));
    for (unsigned i = 2; i <= POOL_HASH; i += 2) pool_release(image(i));
    for (unsigned i = 1; i <= POOL_HASH; i += 2) pool_release(image(i + POOL_HASH));
    pool_release(image(0xFFFF)); /* unknown handles do not change the pool */
    check_empty();
}
int main(void)
{
    s_pool_on = 1;
    s_memprops.memoryTypeCount = s_memprops.memoryHeapCount = 1;
    s_memprops.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    s_memprops.memoryHeaps[0].size = 8ull << 30;
    check_alignment_and_merge();
    check_failures();
    check_capacity();
    check_hash_and_fragmentation();
    pool_report();
    puts("Vulkan image pool: PASS");
    return 0;
}
