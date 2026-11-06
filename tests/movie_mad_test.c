/* Decode consecutive retail movie packets to raw YUV420P. Compare with
 * ffmpeg -frames:v N -pix_fmt yuv420p -f rawvideo on a locally owned disc. */
#include "movie_mad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned le32(const uint8_t *b)
{
    return b[0] | (unsigned)b[1]<<8 | (unsigned)b[2]<<16 | (unsigned)b[3]<<24;
}

int main(int argc, char **argv)
{
    if (argc == 1) return nfsu1_mad_begin(1, (const uint8_t *)"MADk", 4) != 0;
    if (argc != 4) return 1;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    int frames = 0, wanted = atoi(argv[3]);
    if (!in || !out || wanted <= 0) return 1;
    uint8_t header[8];
    while (frames < wanted && fread(header, 1, sizeof header, in) == sizeof header) {
        unsigned size = le32(header+4);
        if (size < 8 || size > 4*1024*1024) return 1;
        if (memcmp(header, "MAD", 3)) {
            if (fseek(in, size-8, SEEK_CUR)) return 1;
            continue;
        }
        uint8_t *chunk = malloc(size);
        if (!chunk || size < 24) return 1;
        memcpy(chunk, header, 8);
        if (fread(chunk+8, 1, size-8, in) != size-8 || !nfsu1_mad_begin(1, chunk, size)) return 1;
        unsigned w = chunk[16] | (unsigned)chunk[17]<<8, h = chunk[18] | (unsigned)chunk[19]<<8;
        unsigned area = w*h, length = area + area/2;
        uint8_t *pixels = malloc(length);
        if (!pixels) return 1;
        uint8_t *planes[] = {pixels, pixels+area, pixels+area+area/4};
        if (!nfsu1_mad_copy(w, h, planes) || fwrite(pixels, 1, length, out) != length) return 1;
        free(pixels); free(chunk); ++frames;
    }
    nfsu1_mad_reset();
    fclose(in);
    return fclose(out) || frames != wanted;
}
