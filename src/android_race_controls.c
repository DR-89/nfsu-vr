/* Publish UI mode and prepare the virtual SDL controller before the guest
 * samples a USB report. Both physical and touch input use guest port zero. */
#include <jni.h>
#include "race_touch.h"
#include "feng_touch.h"
#include "android_input.h"
#include "../xboxrecomp/src/usb/usb_gamepad.h"
#include "../xboxrecomp/src/kernel/xbox_memory_layout.h"

int __real_usb_gamepad_report(int dev, uint8_t *out, int max);

static int touch_enabled(void)
{
    return !android_input_has_controller() && !feng_touch_race_menu_active() &&
        xbox_GetMemoryBase() &&
        *(volatile uint32_t *)(uintptr_t)(xbox_GetMemoryOffset() + 0x00283434u) == 6;
}

void android_race_controls_poll(void)
{
    android_input_poll();
    int enabled = touch_enabled();
    race_touch_enable(enabled);
    if (!enabled) android_input_set_touch(0);
}

static void prepare_report(int dev)
{
    if (dev != 0) return;
    android_input_poll();
    android_input_set_touch(race_touch_consume(touch_enabled()));
}

int __wrap_usb_gamepad_report(int dev, uint8_t *out, int max)
{
    if (max >= 20) prepare_report(dev);
    return __real_usb_gamepad_report(dev, out, max);
}

int __real_usb_gamepad_control(int dev, const UsbSetup *setup, uint8_t *out, int max);
int __wrap_usb_gamepad_control(int dev, const UsbSetup *setup, uint8_t *out, int max)
{
    /* The backend calls report in the same translation unit: prepare the
     * SDL controller before either endpoint samples it. Descriptors do not. */
    if ((setup->bmRequestType & 0xE0u) == 0xA0u && setup->bRequest == 1 &&
        max >= 20 && setup->wLength >= 20) prepare_report(dev);
    return __real_usb_gamepad_control(dev, setup, out, max);
}

JNIEXPORT jboolean JNICALL Java_org_nfsu_android_RaceControlsView_nativeVisible(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return race_touch_visible();
}

JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeSetButtons(
        JNIEnv *env, jclass cls, jint mask)
{
    (void)env; (void)cls;
    race_touch_set((uint32_t)mask);
}

JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeCancel(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    race_touch_cancel();
    android_input_set_touch(0);
}
