#pragma once

#include "power_fsm.h"

#define BC_DISPLAY_OFF_DELAY_MS 10000u

typedef enum {
    BC_DISPLAY_TELEMETRY, BC_DISPLAY_SHUTDOWN, BC_DISPLAY_OFF_ANIMATION,
    BC_DISPLAY_BLANK, BC_DISPLAY_SLEEP, BC_DISPLAY_SUSPENDING,
    BC_DISPLAY_STARTING, BC_DISPLAY_WAKING
} bc_display_mode_t;

/* Read-only presentation policy: never sends GPIO, OS or power commands. */
bc_display_mode_t bc_display_mode(bc_state_t state, uint64_t since, uint64_t now);
