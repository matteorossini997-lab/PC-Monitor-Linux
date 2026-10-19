#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Platform-independent policy. Only the GPIO adapter may execute these effects. */
typedef enum {
    BC_UNKNOWN, BC_OFF, BC_STARTING, BC_RUNNING, BC_STOPPING,
    BC_SUSPENDING, BC_SLEEPING, BC_WAKING
} bc_state_t;

typedef enum {
    BC_CMD_START, BC_CMD_STOP, BC_CMD_SLEEP, BC_CMD_WAKE,
    BC_CMD_FORCE_OFF, BC_CMD_CONFIRM_OFF
} bc_command_t;

typedef enum {
    BC_OS_RUNNING, BC_OS_SUSPENDING, BC_OS_RESUMED,
    BC_OS_SHUTTING_DOWN, BC_OS_OFF_CONFIRMED
} bc_os_event_t;

enum {
    BC_EFFECT_LATCH = 1u << 0,
    BC_EFFECT_BOARD_BUTTON = 1u << 1,
    BC_EFFECT_REQUEST_STOP = 1u << 2,
    BC_EFFECT_REQUEST_SLEEP = 1u << 3
};

typedef struct {
    bc_state_t state;
    bool latch_known, latch_on, state_high, raw_state;
    bool raw_button, button, button_armed, button_active, long_sent;
    bool board_button_sent, stop_after_wake, off_confirmed;
    uint64_t state_changed, raw_state_changed, low_since;
    uint64_t raw_button_changed, button_down, confirmed_at, latch_busy_until;
    uint32_t pending;
    /* Set by the adapter from menuconfig; defaults are conservative. */
    uint32_t long_press_ms, boot_button_delay_ms, action_timeout_ms;
} bc_power_t;

void bc_power_init(bc_power_t *p, uint64_t now, bool state_high, bool button_pressed);
uint32_t bc_power_step(bc_power_t *p, uint64_t now, bool state_high, bool button_pressed);
bool bc_power_command(bc_power_t *p, bc_command_t cmd, uint64_t now);
void bc_power_os_event(bc_power_t *p, bc_os_event_t event, uint64_t now);
const char *bc_power_state_name(bc_state_t state);
