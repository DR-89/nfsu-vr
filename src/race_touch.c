#include "race_touch.h"
#include <pthread.h>
#include <stdatomic.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t held, pending;
static int visible;
static atomic_int editor_requested;
static atomic_int editing;
static atomic_int hold_enabled, hold_down;
void race_touch_enable_hold(int enabled) {
    atomic_store(&hold_enabled, enabled != 0);
    if (!enabled) atomic_store(&hold_down, 0);
}
void race_touch_set_hold(int down) {
    atomic_store(&hold_down, down && race_touch_hold_enabled());
}
int race_touch_hold_enabled(void) {
    return atomic_load(&hold_enabled) && !race_touch_editing();
}
int race_touch_hold_active(void) {
    return race_touch_hold_enabled() && atomic_load(&hold_down);
}

void race_touch_request_editor(void) { atomic_store(&editor_requested, 1); }
int race_touch_take_editor_request(void) { return atomic_exchange(&editor_requested, 0); }
int race_touch_editing(void) { return atomic_load(&editing); }
void race_touch_set_editing(int enabled)
{
    atomic_store(&editing, enabled != 0);
    race_touch_cancel();
}

void race_touch_set(uint32_t mask)
{
    pthread_mutex_lock(&lock);
    mask &= 1023u;
    if (visible && !race_touch_editing()) {
        pending |= mask & ~held;
        held = mask;
    }
    pthread_mutex_unlock(&lock);
}

void race_touch_cancel(void)
{
    atomic_store(&hold_down, 0);
    pthread_mutex_lock(&lock);
    held = pending = 0;
    pthread_mutex_unlock(&lock);
}

void race_touch_enable(int enabled)
{
    pthread_mutex_lock(&lock);
    enabled = enabled && !race_touch_editing();
    visible = enabled;
    if (!enabled) held = pending = 0;
    pthread_mutex_unlock(&lock);
}

uint32_t race_touch_consume(int racing)
{
    pthread_mutex_lock(&lock);
    racing = racing && !race_touch_editing();
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
