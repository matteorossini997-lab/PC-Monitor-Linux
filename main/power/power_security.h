#pragma once
#include <stdbool.h>
#include <stdint.h>

bool bc_token_valid(const char *token);
bool bc_authorized(const char *token, const char *header);
bool bc_parse_ack(const char *line, uint32_t *id, bool *accepted);
