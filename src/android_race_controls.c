#include "../xboxrecomp/include/nfsu_log.h"
#include "input_mode.h"
/* Publish UI mode and prepare the virtual SDL controller before the guest
 * samples a USB report. Both physical and touch input use guest port zero. */
#include <jni.h>
#include "race_touch.h"
#include "menu_touch.h"
#include "feng_touch.h"
#include "android_input.h"
#include "android_input_trace.h"
#include <stdio.h>
#include "../xboxrecomp/src/usb/usb_gamepad.h"
#include "../xboxrecomp/src/kernel/xbox_memory_layout.h"

int __real_usb_gamepad_report(int dev, uint8_t *out, int max);

static int screen_hold_enabled(void)
{
    return input_mode_sensor_enabled() && !feng_touch_race_menu_active() &&
        xbox_GetMemoryBase() &&
        *(volatile uint32_t *)(uintptr_t)(xbox_GetMemoryOffset() + 0x00283434u) == 6;
}

static int touch_enabled(void)
{
    return input_mode_get().overlay_enabled && screen_hold_enabled();
}

void android_race_controls_poll(void)
{
    android_input_poll();
    race_touch_enable_hold(screen_hold_enabled());
    int enabled = touch_enabled();
    race_touch_enable(enabled);
    if (!enabled) android_input_set_touch(0);
}

static void prepare_report(int dev)
{
    if (dev != 0) return;
    android_input_poll();
    race_touch_enable_hold(screen_hold_enabled());
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
    if (android_input_trace_enabled()) {
        static int previous = -1;
        if (previous != mask) {
            previous = mask;
            NFS_LOG_TRACE("input", "[INPUT_TRACE] t=%llu submit mask=%03X visible=%d editing=%d"
                    " available=%03X\n", android_input_trace_time(), (unsigned)mask,
                    race_touch_visible(), race_touch_editing(), race_touch_button_mask());
        }
    }
}

JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeCancel(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (android_input_trace_enabled())
        NFS_LOG_TRACE("input", "[INPUT_TRACE] t=%llu cancel\n", android_input_trace_time());
    race_touch_cancel();
    android_input_set_touch(0);
}

JNIEXPORT jboolean JNICALL Java_org_nfsu_android_RaceControlsView_nativeInputTraceEnabled(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return android_input_trace_enabled();
}

JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeTraceInput(
        JNIEnv *env, jclass cls, jstring message)
{
    (void)cls;
    if (!android_input_trace_enabled()) return;
    const char *text = (*env)->GetStringUTFChars(env, message, NULL);
    if (!text) return;
    NFS_LOG_TRACE("input", "[INPUT_TRACE] t=%llu touch %s\n", android_input_trace_time(), text);
    (*env)->ReleaseStringUTFChars(env, message, text);
}

JNIEXPORT jboolean JNICALL Java_org_nfsu_android_RaceControlsView_nativeTakeEditorRequest(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return race_touch_take_editor_request();
}

JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeSetEditing(
        JNIEnv *env, jclass cls, jboolean editing)
{
    (void)env; (void)cls;
    race_touch_set_editing(editing);
    menu_touch_cancel();
    android_input_set_touch(0);
}

JNIEXPORT jboolean JNICALL Java_org_nfsu_android_RaceControlsView_nativeHoldEnabled(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return race_touch_hold_enabled();
}
JNIEXPORT void JNICALL Java_org_nfsu_android_RaceControlsView_nativeSetHold(
        JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    race_touch_set_hold(down);
}

JNIEXPORT jint JNICALL Java_org_nfsu_android_RaceControlsView_nativeButtonMask(
        JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)race_touch_button_mask();
}
