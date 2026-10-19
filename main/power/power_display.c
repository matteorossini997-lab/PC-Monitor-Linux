#include "power_display.h"

bc_display_mode_t bc_display_mode(bc_state_t state, uint64_t since, uint64_t now)
{
    switch (state) {
    case BC_OFF:
        return now >= since && now - since >= BC_DISPLAY_OFF_DELAY_MS ?
            BC_DISPLAY_BLANK : BC_DISPLAY_OFF_ANIMATION;
    case BC_STOPPING: return BC_DISPLAY_SHUTDOWN;
    case BC_SLEEPING: return BC_DISPLAY_SLEEP;
    case BC_SUSPENDING: return BC_DISPLAY_SUSPENDING;
    case BC_STARTING: return BC_DISPLAY_STARTING;
    case BC_WAKING: return BC_DISPLAY_WAKING;
    default: return BC_DISPLAY_TELEMETRY;
    }
}
