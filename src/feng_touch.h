#ifndef NFSU_FENG_TOUCH_H
#define NFSU_FENG_TOUCH_H
#include <stdint.h>

/* Guest input hooks. Only the guest thread calls these. */
void feng_touch_hit(void);
void feng_touch_dispatch(void);
void feng_touch_update(void);
void feng_touch_poll(void);
/* Atomic host-visible mode; guest memory is inspected only by guest hooks. */
int feng_touch_race_menu_active(void);

/* Render hooks share FEng's final cached geometry, in its 640x480 canvas.
 * draw_begin/end support nested object draws. draw_cache returns nonzero
 * when the old Help glyph should be omitted after recording its bounds. */
void feng_touch_frame_begin(void);
void feng_touch_frame_end(void);
uint32_t feng_touch_draw_begin(uint32_t object);
void feng_touch_draw_end(uint32_t previous);
int feng_touch_draw_cache(uint32_t cache);
#endif
