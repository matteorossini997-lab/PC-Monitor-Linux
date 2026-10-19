#include "power_security.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

bool bc_token_valid(const char *token)
{
    if (!token || strlen(token) != 64) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!isxdigit((unsigned char)token[i])) return false;
    return true;
}

bool bc_authorized(const char *token, const char *header)
{
    if (!bc_token_valid(token) || !header || strlen(header) != 71 ||
        memcmp(header, "Bearer ", 7) != 0) return false;
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < 64; ++i) difference |= (unsigned char)header[i + 7] ^ (unsigned char)token[i];
    return difference == 0;
}

bool bc_parse_ack(const char *line, uint32_t *id, bool *accepted)
{
    if (!line || !id || !accepted || strncmp(line, "PWR_ACK:", 8) != 0) return false;
    const char *cursor = line + 8;
    uint32_t number = 0;
    size_t digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        unsigned digit = (unsigned)(*cursor - '0');
        if (++digits > 10 || number > (UINT32_MAX - digit) / 10) return false;
        number = number * 10 + digit;
        ++cursor;
    }
    if (!digits || *cursor++ != ':') return false;
    bool ok = strcmp(cursor, "OK") == 0;
    if (!ok && strcmp(cursor, "ERROR") != 0) return false;
    *id = number;
    *accepted = ok;
    return true;
}
