/* Optional adapter: native-only applications do not link the fiber mechanism. */
#include "guest_fiber.h"
#include "guest_dispatcher.h"
static void *current(void) { return xgf_current(); }
static int priority(void *fiber) { return xgf_priority(fiber); }
static int prepare(void *fiber) { return xgf_prepare_wait(fiber); }
static void notify(void *fiber) { xgf_notify(fiber); }
int xgf_use_dispatcher_waits(void)
{
    static const XgdWaitBackend backend = {current, priority, prepare, xgf_park, notify, xgf_yield};
    return xgd_set_wait_backend(&backend);
}
