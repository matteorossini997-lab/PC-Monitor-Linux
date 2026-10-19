#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "power_fsm.h"

esp_err_t bc250_power_init(void);
bool bc250_power_submit(bc_command_t command);
bool bc250_power_handle_serial(const char *line);
bool bc250_power_status_json(char *buffer, size_t size);
