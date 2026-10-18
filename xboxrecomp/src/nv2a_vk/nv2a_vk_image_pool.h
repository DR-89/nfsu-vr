/* Adapted from Anton Artemov, upstream 99ead072 (2026-10-15).
 * Included by nv2a_vk.c after mem_type; accessed on the renderer thread.
 * Only optimal-tiled images share these blocks, never buffers/linear images. */
/* ── Image memory pool (RECOMP_VK_TEX_POOL) ───────────────────────────
 *
 * Every image had an allocation of its own: a vkAllocateMemory when a
 * texture or surface first appeared and a vkFreeMemory when it went, each
 * a round trip through the kernel on Horizon -- a burst of them when a race
 * loads made the hitches. Images up to 16 MB now take a range of a 32 MB
 * device-local block (up to 8 blocks, first fit); the range goes back
 * when the image is destroyed (garbage_free, after its frame's fence).
 * Larger images, and everything once the blocks are full, allocate as
 * before. RECOMP_VK_TEX_POOL=0 off. */
#ifdef __ANDROID__
#include "../../include/nfsu_log.h"
#include <sys/system_properties.h>
#endif

#define POOL_BLOCK      (32ull << 20)
#define POOL_MAX_BLOCKS 8
#define POOL_MAX_ALLOC  (16ull << 20)
#define POOL_HASH       8192

typedef struct { VkDeviceSize off, size; } PoolRange;
typedef struct {
    VkDeviceMemory mem;
    uint32_t       type;
    PoolRange     *free;             /* by offset, never adjacent */
    uint32_t       nfree, cap, live;
} PoolBlock;
static PoolBlock s_pool[POOL_MAX_BLOCKS];
static uint32_t s_pool_hits, s_pool_fallbacks, s_pool_allocations;
static VkDeviceSize s_pool_reserved, s_pool_used;
static int       s_pool_on = -1;
/* Pooled images: which range they hold. Linear probing. */
static struct { VkImage img; uint32_t block; VkDeviceSize off, size; } s_pool_map[POOL_HASH];

static uint32_t pool_slot(VkImage img)
{
    uint64_t k = (uint64_t)(uintptr_t)img;
    k ^= k >> 29;
    k *= 0xbf58476d1ce4e5b9ull;
    k ^= k >> 32;
    return (uint32_t)k & (POOL_HASH - 1);
}

/* Reserve enough range nodes before binding, including future releases.
 * A failed host allocation leaves the image on the dedicated path. */
static int pool_reserve(PoolBlock *b, uint32_t count)
{
    if (count <= b->cap) return 1;
    uint32_t cap = count < 32 ? 32 : count * 2;
    PoolRange *next = realloc(b->free, cap * sizeof(*next));
    if (!next) return 0;
    b->free = next;
    b->cap = cap;
    return 1;
}

static void pool_put(PoolBlock *b, VkDeviceSize off, VkDeviceSize size)
{
    uint32_t i = 0;
    while (i < b->nfree && b->free[i].off < off)
        i++;
    if (i > 0 && b->free[i - 1].off + b->free[i - 1].size == off) {
        b->free[i - 1].size += size;                /* joins the one before */
        if (i < b->nfree && off + size == b->free[i].off) {
            b->free[i - 1].size += b->free[i].size;
            memmove(&b->free[i], &b->free[i + 1], (b->nfree - i - 1) * sizeof *b->free);
            b->nfree--;
        }
        return;
    }
    if (i < b->nfree && off + size == b->free[i].off) {
        b->free[i].off = off;                       /* joins the one after */
        b->free[i].size += size;
        return;
    }
    memmove(&b->free[i + 1], &b->free[i], (b->nfree - i) * sizeof *b->free);
    b->free[i].off = off;
    b->free[i].size = size;
    b->nfree++;
}

static int pool_take(PoolBlock *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *at)
{
    uint32_t i;
    for (i = 0; i < b->nfree; i++) {
        VkDeviceSize o = (b->free[i].off + align - 1) & ~(align - 1);
        VkDeviceSize end = b->free[i].off + b->free[i].size, lo = b->free[i].off;
        if (o + size > end)
            continue;
        memmove(&b->free[i], &b->free[i + 1], (b->nfree - i - 1) * sizeof *b->free);
        b->nfree--;
        if (o > lo)
            pool_put(b, lo, o - lo);
        if (o + size < end)
            pool_put(b, o + size, end - (o + size));
        *at = o;
        return 1;
    }
    return 0;
}

/* Bind img to a pooled range; 0: allocate it as before. */
static int pool_bind(VkImage img, const VkMemoryRequirements *rq)
{
    uint32_t type, i, h;
    VkDeviceSize at;
    if (s_pool_on < 0) {
        const char *e = getenv("RECOMP_VK_TEX_POOL");
#ifdef __ANDROID__
        char property[PROP_VALUE_MAX];
        if (!e && __system_property_get("debug.nfsu.vk_tex_pool", property) > 0)
            e = property;
#endif
        s_pool_on = !(e && *e == '0');
    }
    if (!s_pool_on || rq->size > POOL_MAX_ALLOC)
        return 0;
    type = mem_type(rq->memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
        return 0;
    h = pool_slot(img);
    for (i = 0; s_pool_map[h].img && i < POOL_HASH; i++)
        h = (h + 1) & (POOL_HASH - 1);
    if (s_pool_map[h].img)
        return 0;                                   /* map full */
    /* Prefer an existing block even if an earlier slot was released. */
    for (i = 0; i < POOL_MAX_BLOCKS; i++) {
        PoolBlock *b = &s_pool[i];
        if (b->mem && b->type == type
            && pool_reserve(b, b->nfree + b->live + 2)
            && pool_take(b, rq->size, rq->alignment, &at))
            break;
    }
    if (i == POOL_MAX_BLOCKS) {
        for (i = 0; i < POOL_MAX_BLOCKS && s_pool[i].mem; i++) {}
        if (i == POOL_MAX_BLOCKS) return 0;
        uint32_t heap = s_memprops.memoryTypes[type].heapIndex;
        /* At most 256 MiB, and at most one eighth of this memory heap. */
        VkDeviceSize heap_reserved = 0;
        for (uint32_t j = 0; j < POOL_MAX_BLOCKS; j++)
            if (s_pool[j].mem && s_memprops.memoryTypes[s_pool[j].type].heapIndex == heap)
                heap_reserved += POOL_BLOCK;
        if (heap_reserved + POOL_BLOCK > s_memprops.memoryHeaps[heap].size / 8)
            return 0;
        PoolBlock *b = &s_pool[i];
        if (!pool_reserve(b, 32)) return 0;
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = POOL_BLOCK;
        ai.memoryTypeIndex = type;
        if (vkAllocateMemory(s_dev, &ai, NULL, &b->mem) != VK_SUCCESS) {
            free(b->free);
            memset(b, 0, sizeof(*b));
            return 0;
        }
        s_pool_allocations++;
        b->type = type;
        pool_put(b, 0, POOL_BLOCK);
        s_pool_reserved += POOL_BLOCK;
        NFS_LOG_INFO("graphics", "  [VK] " "texture pool: reserved %u MiB\n", (unsigned)(s_pool_reserved >> 20));
        if (!pool_take(b, rq->size, rq->alignment, &at)) return 0;
    }
    {
        PoolBlock *b = &s_pool[i];
        if (vkBindImageMemory(s_dev, img, b->mem, at) != VK_SUCCESS) {
            pool_put(b, at, rq->size);
            if (!b->live) {
                vkFreeMemory(s_dev, b->mem, NULL);
                s_pool_reserved -= POOL_BLOCK;
                free(b->free);
                memset(b, 0, sizeof(*b));
            }
            return 0;
        }
        b->live++;
        s_pool_hits++;
        s_pool_used += rq->size;
        s_pool_map[h].img = img;
        s_pool_map[h].block = i;
        s_pool_map[h].off = at;
        s_pool_map[h].size = rq->size;
        return 1;
    }
    return 0;
}

/* img is being destroyed: its range, if it has one, goes back. */
static void pool_release(VkImage img)
{
    uint32_t h = pool_slot(img), n, j;
    for (n = 0; n < POOL_HASH && s_pool_map[h].img != img; n++) {
        if (!s_pool_map[h].img)
            return;
        h = (h + 1) & (POOL_HASH - 1);
    }
    if (n == POOL_HASH)
        return;
    PoolBlock *b = &s_pool[s_pool_map[h].block];
    pool_put(b, s_pool_map[h].off, s_pool_map[h].size);
    s_pool_used -= s_pool_map[h].size;
    if (--b->live == 0) {
        /* Called after image destruction and its frame fence, never while
         * the driver can still access this block. */
        vkFreeMemory(s_dev, b->mem, NULL);
        s_pool_reserved -= POOL_BLOCK;
        free(b->free);
        memset(b, 0, sizeof(*b));
    }
    s_pool_map[h].img = VK_NULL_HANDLE;
    /* Close the gap: move up later entries of the run that hash at or
     * before it. */
    for (j = (h + 1) & (POOL_HASH - 1); s_pool_map[j].img; j = (j + 1) & (POOL_HASH - 1)) {
        uint32_t k = pool_slot(s_pool_map[j].img);
        if ((j > h) ? (k <= h || k > j) : (k <= h && k > j)) {
            s_pool_map[h] = s_pool_map[j];
            s_pool_map[j].img = VK_NULL_HANDLE;
            h = j;
        }
    }
}


static void pool_report(void)
{
    NFS_LOG_WARN("graphics", "[texpool] %s: %u pooled / %u block allocations, %u standalone, reserved %u MiB, used %.1f MiB\n",
        s_pool_on == 1 ? "on" : "off", s_pool_hits, s_pool_allocations, s_pool_fallbacks,
        (unsigned)(s_pool_reserved >> 20), s_pool_used / 1048576.0);
}
