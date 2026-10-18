#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* The native port reads an extracted folder. Advertise hard-disk media in
 * the in-memory XBE certificate so XAPI selects file-backed startup instead
 * of requesting physical DVD security sectors from the synthetic drive.
 * No instruction bytes or files on disk are changed. */
static inline int nfsu1_host_image(void *bytes, size_t length)
{
    uint8_t *data = (uint8_t *)bytes;
    uint32_t base, cert, size, title, flags, media = 1;
    if (length < 0x178 || memcmp(data, "XBEH", 4)) return 0;
    memcpy(&base, data + 0x104, 4);
    memcpy(&cert, data + 0x118, 4);
    if (cert < base || (uint64_t)(cert - base) + 0x1dc > length) return 0;
    uint8_t *c = data + cert - base;
    memcpy(&size, c, 4); memcpy(&title, c + 8, 4);
    if (size < 0x1dc || (uint64_t)(cert - base) + size > length || title != 0x45410047u) return 0;
    memcpy(c + 0x9c, &media, 4);
    memcpy(&flags, c + 0x1d8, 4); flags &= ~1u;
    memcpy(c + 0x1d8, &flags, 4);
    return 1;
}
