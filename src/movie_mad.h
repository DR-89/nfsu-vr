#pragma once
#include <stdint.h>

/* One decoder per movie thread. The retail player owns all frame timing,
 * presentation and allocation. A failure uses its original macroblock path. */
int nfsu1_mad_begin(uint32_t owner, const uint8_t *packet, unsigned size);
int nfsu1_mad_copy(unsigned width, unsigned height, uint8_t *planes[3]);
void nfsu1_mad_reset(void);
