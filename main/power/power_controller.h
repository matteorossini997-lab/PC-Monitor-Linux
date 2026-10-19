#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "power_fsm.h"

typedef struct {
    bc_state_t state;
    uint64_t state_changed_ms;
} bc_power_snapshot_t;

esp_err_t bc250_power_init(void);
bool bc250_power_submit(bc_command_t command);
bool bc250_power_handle_serial(const char *line);
bool bc250_power_status_json(char *buffer, size_t size);
/* Read-only coherent snapshot for the UI; false leaves *snapshot unchanged. */
bool bc250_power_snapshot(bc_power_snapshot_t *snapshot);
