#include "menu_touch.h"
#include <math.h>
#include <pthread.h>
#include <string.h>
#include "input_mode.h"

/* Preserve short taps between guest frames. FEng resolves hover AFTER button
 * edges, so each down must first spend one update selecting its real object. */
#define QUEUE_SIZE 64
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static MenuTouchSample queue[QUEUE_SIZE], current;
static unsigned count, hover, release_pending, reset_pending, producer_down;

static void cancel_locked(void)
{
    count = hover = release_pending = 0;
    memset(&current, 0, sizeof(current));
    reset_pending = 1;
    producer_down = 0;
}

void menu_touch_cancel(void)
{
    pthread_mutex_lock(&mutex);
    cancel_locked();
    pthread_mutex_unlock(&mutex);
}

static unsigned mode_revision;
static void sync_mode_locked(void)
{
    InputMode mode = input_mode_get();
    if (mode.revision != mode_revision) {
        cancel_locked();
        mode_revision = mode.revision;
    }
}

void menu_touch_event(enum MenuTouchEvent event, float x, float y, int width, int height)
{
    pthread_mutex_lock(&mutex);
    sync_mode_locked();
    if (!input_mode_sensor_enabled() || (event != MENU_TOUCH_DOWN && !producer_down)) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    if (!isfinite(x) || !isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 ||
        width <= 0 || height <= 0) {
        cancel_locked();
    } else {
        MenuTouchSample sample = { x, y, width, height, event != MENU_TOUCH_UP, 1, 0, 0 };
        /* Moves may coalesce, but never overwrite a down or up edge. */
        if (event == MENU_TOUCH_MOVE && count && queue[count - 1].down == 2)
            count--;
        if (count == QUEUE_SIZE) cancel_locked();
        else {
            sample.down = event == MENU_TOUCH_MOVE ? 2 : sample.down;
            queue[count++] = sample;
            producer_down = event != MENU_TOUCH_UP;
        }
    }
    pthread_mutex_unlock(&mutex);
}

MenuTouchSample menu_touch_sample(void)
{
    pthread_mutex_lock(&mutex);
    sync_mode_locked();
    current.prepare = 0;
    if (release_pending) {
        current.active = 0;
        release_pending = 0;
    }
    if (hover) {
        current.down = 1;
        hover = 0;
    } else if (count) {
        MenuTouchSample next = queue[0];
        memmove(queue, queue + 1, --count * sizeof(queue[0]));
        if (next.down == 1) {
            next.down = 0;
            next.prepare = 1;
            hover = 1;
        } else if (next.down == 2) next.down = 1;
        else release_pending = 1;
        current = next;
    }
    MenuTouchSample result = current;
    result.reset = reset_pending;
    reset_pending = 0;
    pthread_mutex_unlock(&mutex);
    return result;
}

int menu_touch_position(const MenuTouchSample *sample, int wide, float menu_size,
                        short *x, short *y)
{
    if (!sample->active || sample->width <= 0 || sample->height <= 0 ||
        !isfinite(menu_size) || menu_size <= 0) return 0;
    /* Same fit and integer letterbox rounding as nv2a_vk's 640x480 present.
     * FEng descriptors use a centred 640x480 canvas, before Menu Size. */
    float logical_width = wide ? 480.0f * 16.0f / 9.0f : 640.0f;
    float scale = fminf(sample->width / logical_width, sample->height / 480.0f);
    int width = (int)(logical_width * scale), height = (int)(480.0f * scale);
    if (width <= 0 || height <= 0) return 0;
    int left = (sample->width - width) / 2, top = (sample->height - height) / 2;
    float px = sample->x * sample->width - left;
    float py = sample->y * sample->height - top;
    if (px < 0 || px >= width || py < 0 || py >= height) return 0;
    float fx = (px * 640.0f / width - 320.0f) / menu_size;
    float fy = (py * 480.0f / height - 240.0f) / menu_size;
    if (fabsf(fx) > 32767 || fabsf(fy) > 32767) return 0;
    *x = (short)lroundf(fx);
    *y = (short)lroundf(fy);
    return 1;
}
