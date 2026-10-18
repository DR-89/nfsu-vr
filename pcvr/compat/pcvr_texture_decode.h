#ifndef PCVR_TEXTURE_DECODE_H
#define PCVR_TEXTURE_DECODE_H
/* Preserve the imported PCVR interface; both hosts use the same decoder. */
#include "../../xboxrecomp/src/d3d/d3d8_texture_decode.h"
#define pcvr_texture_decode_supported xbox_texture_decode_supported
#define pcvr_texture_decode xbox_texture_decode
#endif
