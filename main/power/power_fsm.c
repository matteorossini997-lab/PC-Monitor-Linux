#include "power_fsm.h"
#include <string.h>

#define STATE_DEBOUNCE_MS 250u
#define BUTTON_DEBOUNCE_MS 40u
#define OFF_STABLE_MS 3000u
#define LATCH_SETTLE_MS 500u
#define BOOT_TIMEOUT_MS 90000u

static void transition(bc_power_t *p, bc_state_t state, uint64_t now)
{
    p->state = state;
    p->state_changed = now;
}

static void power_off(bc_power_t *p, uint64_t now)
{
    /* Cancel queued start/wake/OS effects: never restart after a forced stop. */
    p->pending = BC_EFFECT_LATCH;
    p->latch_on = false;
    p->latch_known = true;
    p->off_confirmed = false;
    p->stop_after_wake = false;
    p->latch_busy_until = now + LATCH_SETTLE_MS;
    transition(p, BC_OFF, now);
}

void bc_power_init(bc_power_t *p, uint64_t now, bool state_high, bool button_pressed)
{
    memset(p, 0, sizeof(*p));
    p->state = BC_UNKNOWN;
    p->raw_state = state_high;
    p->raw_state_changed = now;
    p->low_since = now;
    p->raw_button = p->button = button_pressed;
    p->button_armed = !button_pressed;
    p->raw_button_changed = now;
    p->state_changed = now;
    p->long_press_ms = 5000;
    p->boot_button_delay_ms = 2000;
    p->action_timeout_ms = 120000;
}

bool bc_power_command(bc_power_t *p, bc_command_t cmd, uint64_t now)
{
    switch (cmd) {
    case BC_CMD_CONFIRM_OFF:
        /* An operator must verify the physical ATX rails are off. Low STATE alone
         * is insufficient; this command is not issued automatically. */
        if (p->state != BC_UNKNOWN || p->latch_known || p->raw_state ||
            now - p->raw_state_changed < STATE_DEBOUNCE_MS) return false;
        p->latch_known = true;
        p->latch_on = false;
        transition(p, BC_OFF, now);
        return true;
    case BC_CMD_START:
        if (p->state == BC_RUNNING || p->state == BC_STARTING) return true;
        if (p->state == BC_SLEEPING || p->state == BC_SUSPENDING || p->state == BC_WAKING)
            return bc_power_command(p, BC_CMD_WAKE, now);
        if (p->state != BC_OFF || !p->latch_known || p->latch_on ||
            p->raw_state || p->state_high || now < p->latch_busy_until) return false;
        p->latch_on = true;
        p->latch_busy_until = now + LATCH_SETTLE_MS;
        p->board_button_sent = false;
        p->pending |= BC_EFFECT_LATCH;
        transition(p, BC_STARTING, now);
        return true;
    case BC_CMD_STOP:
        if (p->state == BC_STOPPING) return true;
        if (p->state == BC_SLEEPING || p->state == BC_SUSPENDING || p->state == BC_WAKING) {
            if (!bc_power_command(p, BC_CMD_WAKE, now)) return false;
            p->stop_after_wake = true;
            return true;
        }
        if (p->state != BC_RUNNING || !p->latch_on) return false;
        p->off_confirmed = false;
        p->pending |= BC_EFFECT_REQUEST_STOP;
        transition(p, BC_STOPPING, now);
        return true;
    case BC_CMD_SLEEP:
        if (p->state == BC_SUSPENDING || p->state == BC_SLEEPING) return true;
        if (p->state != BC_RUNNING || !p->latch_on) return false;
        p->off_confirmed = false;
        p->suspend_announced = false;
        p->pending |= BC_EFFECT_REQUEST_SLEEP;
        transition(p, BC_SUSPENDING, now);
        return true;
    case BC_CMD_WAKE:
        if (p->state == BC_RUNNING || p->state == BC_WAKING) return true;
        /* Pending suspend is not permission to press POWER on a running PC.
         * Rising STATE may also indicate a resume before its serial hook arrives. */
        if (p->state != BC_SLEEPING || !p->latch_on || p->raw_state || p->state_high)
            return false;
        p->pending |= BC_EFFECT_BOARD_BUTTON;
        transition(p, BC_WAKING, now);
        return true;
    case BC_CMD_FORCE_OFF:
        if (!p->latch_known || !p->latch_on || now < p->latch_busy_until) return false;
        power_off(p, now);
        return true;
    default:
        return false;
    }
}

void bc_power_os_event(bc_power_t *p, bc_os_event_t event, uint64_t now)
{
    if (event < BC_OS_RUNNING || event > BC_OS_OFF_CONFIRMED) return;
    /* Do not let serial data buffered before a forced cut undo that cut. */
    if (p->state == BC_OFF && now - p->state_changed < OFF_STABLE_MS) return;
    p->latch_known = p->latch_on = true;
    p->off_confirmed = false;
    switch (event) {
    case BC_OS_RUNNING:
    case BC_OS_RESUMED:
        if (event == BC_OS_RESUMED) p->suspend_announced = false;
        if (p->stop_after_wake) {
            p->stop_after_wake = false;
            transition(p, BC_STOPPING, now);
            p->pending |= BC_EFFECT_REQUEST_STOP;
        } else if (p->state != BC_STOPPING && p->state != BC_SUSPENDING && p->state != BC_SLEEPING) {
            /* Ordinary heartbeat must not cancel a request before its hook runs. */
            transition(p, BC_RUNNING, now);
        } else if (event == BC_OS_RESUMED) {
            transition(p, BC_RUNNING, now);
        }
        break;
    case BC_OS_SUSPENDING:
        p->suspend_announced = true;
        p->stop_after_wake = false;
        transition(p, BC_SUSPENDING, now);
        break;
    case BC_OS_SHUTTING_DOWN:
        transition(p, BC_STOPPING, now);
        break;
    case BC_OS_OFF_CONFIRMED:
        /* Sent ONLY by the final systemd-shutdown hook after processes/filesystems
         * have been shut down. Never infer this from a timeout or missing data. */
        p->off_confirmed = true;
        p->confirmed_at = now;
        transition(p, BC_STOPPING, now);
        break;
    }
}

static void button_step(bc_power_t *p, uint64_t now, bool pressed)
{
    if (pressed != p->raw_button) {
        p->raw_button = pressed;
        p->raw_button_changed = now;
    }
    if (now - p->raw_button_changed >= BUTTON_DEBOUNCE_MS && p->button != pressed) {
        p->button = pressed;
        if (!pressed) {
            if (p->button_active && !p->long_sent) {
                bc_command_t cmd = p->state == BC_OFF ? BC_CMD_START :
                    (p->state == BC_SLEEPING || p->state == BC_SUSPENDING) ? BC_CMD_WAKE : BC_CMD_STOP;
                (void)bc_power_command(p, cmd, now);
            }
            p->button_armed = true;
            p->button_active = false;
        } else if (p->button_armed) {
            p->button_active = true;
            p->long_sent = false;
            p->button_down = now;
        }
    }
    if (p->button_active && p->button && !p->long_sent &&
        now - p->button_down >= p->long_press_ms) {
        p->long_sent = true; /* Suppress short action even when FORCE_OFF is denied. */
        (void)bc_power_command(p, BC_CMD_FORCE_OFF, now);
    }
}

uint32_t bc_power_step(bc_power_t *p, uint64_t now, bool state_high, bool button_pressed)
{
    if (state_high != p->raw_state) {
        p->raw_state = state_high;
        p->raw_state_changed = now;
        if (!state_high) p->low_since = now;
    }
    if (now - p->raw_state_changed >= STATE_DEBOUNCE_MS) p->state_high = state_high;

    if (p->state == BC_UNKNOWN && p->state_high) {
        /* Cannot distinguish an awake PC from a sleeping PC with HIGH STATE. */
        p->latch_known = p->latch_on = true;
    }
    if (p->state == BC_STARTING && p->state_high) {
        p->latch_known = p->latch_on = true;
        transition(p, BC_RUNNING, now);
    }
    if (p->state == BC_STARTING) {
        if (!p->board_button_sent && now - p->state_changed >= p->boot_button_delay_ms) {
            p->pending |= BC_EFFECT_BOARD_BUTTON;
            p->board_button_sent = true;
        }
        if (now - p->state_changed >= BOOT_TIMEOUT_MS) transition(p, BC_UNKNOWN, now);
    }
    if (p->state == BC_SUSPENDING && p->suspend_announced &&
        !p->raw_state && !p->state_high) transition(p, BC_SLEEPING, now);
    if (p->off_confirmed && !p->raw_state && !p->state_high &&
        now - p->low_since >= OFF_STABLE_MS && now - p->confirmed_at >= OFF_STABLE_MS &&
        now >= p->latch_busy_until) power_off(p, now);

    if (!p->off_confirmed && (p->state == BC_STOPPING || p->state == BC_WAKING) &&
        now - p->state_changed >= p->action_timeout_ms) {
        p->stop_after_wake = false;
        transition(p, BC_UNKNOWN, now); /* Keep ATX ON; no automatic forced cut. */
    }
    button_step(p, now, button_pressed);
    uint32_t effects = p->pending;
    p->pending = 0;
    return effects;
}

const char *bc_power_state_name(bc_state_t state)
{
    static const char *const names[] = {
        "unknown", "off", "starting", "running", "stopping", "suspending", "sleeping", "waking"
    };
    return state >= BC_UNKNOWN && state <= BC_WAKING ? names[state] : "unknown";
}
