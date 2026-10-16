#include "race_touch.h"
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t held, pending;
static int visible;

void race_touch_set(uint32_t mask)
{
    pthread_mutex_lock(&lock);
    mask &= 511u;
    if (visible) {
        pending |= mask & ~held;
        held = mask;
    }
    pthread_mutex_unlock(&lock);
}

void race_touch_cancel(void)
{
    pthread_mutex_lock(&lock);
    held = pending = 0;
    pthread_mutex_unlock(&lock);
}

void race_touch_enable(int enabled)
{
    pthread_mutex_lock(&lock);
    visible = enabled;
    if (!enabled) held = pending = 0;
    pthread_mutex_unlock(&lock);
}

uint32_t race_touch_consume(int racing)
{
    pthread_mutex_lock(&lock);
    visible = racing;
    if (!racing) held = pending = 0;
    uint32_t mask = held | pending;
    pending = 0; /* Short taps survive until at least one input report. */
    pthread_mutex_unlock(&lock);
    return mask;
}

int race_touch_visible(void)
{
    pthread_mutex_lock(&lock);
    int result = visible;
    pthread_mutex_unlock(&lock);
    return result;
}
