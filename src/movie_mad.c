#include "movie_mad.h"
#include <libavcodec/avcodec.h>
#include <string.h>
#ifdef _MSC_VER
#define MAD_TLS __declspec(thread)
#else
#define MAD_TLS _Thread_local
#endif

static MAD_TLS AVCodecContext *decoder;
static MAD_TLS AVFrame *picture;
static MAD_TLS uint32_t stream;

void nfsu1_mad_reset(void)
{
    av_frame_free(&picture);
    avcodec_free_context(&decoder);
    stream = 0;
}

int nfsu1_mad_begin(uint32_t owner, const uint8_t *data, unsigned size)
{
    if (!data || size < 26 || size > 4 * 1024 * 1024) goto failed;
    int key = !memcmp(data, "MADk", 4);
    if (!key && (owner != stream || !decoder ||
                 (memcmp(data, "MADm", 4) && memcmp(data, "MADe", 4)))) goto failed;
    unsigned w = data[16] | (unsigned)data[17] << 8, h = data[18] | (unsigned)data[19] << 8;
    if (!w || !h || ((w | h) & 15) || w > 1024 || h > 1024) goto failed;
    if (key && owner != stream) nfsu1_mad_reset();
    if (!decoder) {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MAD);
        if (!codec || !(decoder = avcodec_alloc_context3(codec)) ||
            avcodec_open2(decoder, codec, NULL) < 0 || !(picture = av_frame_alloc())) goto failed;
        stream = owner;
    }
    AVPacket *packet = av_packet_alloc();
    if (!packet) goto failed;
    int status = av_new_packet(packet, (int)size);
    if (status >= 0) {
        memcpy(packet->data, data, size);
        status = avcodec_send_packet(decoder, packet);
    }
    av_packet_free(&packet);
    if (status < 0 || avcodec_receive_frame(decoder, picture) < 0 ||
        picture->format != AV_PIX_FMT_YUV420P || picture->width != (int)w || picture->height != (int)h)
        goto failed;
    return 1;
failed:
    nfsu1_mad_reset();
    return 0;
}

int nfsu1_mad_copy(unsigned width, unsigned height, uint8_t *planes[3])
{
    if (!picture || !picture->data[0] || width != picture->width || height != picture->height) return 0;
    for (int i = 0; i < 3; ++i) {
        unsigned w = i ? width / 2 : width, h = i ? height / 2 : height;
        for (unsigned y = 0; y < h; ++y)
            memcpy(planes[i] + y * w, picture->data[i] + y * picture->linesize[i], w);
    }
    return 1;
}
