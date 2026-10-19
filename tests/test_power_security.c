#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "power_security.h"

int main(void)
{
    const char *token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char header[80];
    snprintf(header, sizeof(header), "Bearer %s", token);
    assert(bc_token_valid(token));
    assert(!bc_token_valid(NULL));
    assert(!bc_token_valid("short"));
    assert(!bc_token_valid("g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(bc_authorized(token, header));
    header[20] ^= 1;
    assert(!bc_authorized(token, header));
    assert(!bc_authorized(token, "Bearer short"));
    assert(!bc_authorized(token, NULL));
    assert(!bc_authorized(NULL, header));
    snprintf(header, sizeof(header), "Basic  %s", token);
    assert(!bc_authorized(token, header));
    uint32_t id = 0;
    bool accepted = false;
    assert(bc_parse_ack("PWR_ACK:42:OK", &id, &accepted) && id == 42 && accepted);
    assert(bc_parse_ack("PWR_ACK:4294967295:ERROR", &id, &accepted) && id == UINT32_MAX && !accepted);
    const char *invalid[] = {"PWR_ACK:-1:OK", "PWR_ACK:4294967296:OK", "PWR_ACK::OK",
        "PWR_ACK:1:OK extra", "PWR_ACK:1:OK\n", "PWR_ACK:1:BOGUS", "PWR_ACK:x:OK",
        "PWR_ACK:999999999999999999999:OK", "PWR_ACK:1OK", "wrong"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!bc_parse_ack(invalid[i], &id, &accepted));
    assert(!bc_parse_ack(NULL, &id, &accepted));
    assert(!bc_parse_ack("PWR_ACK:1:OK", NULL, &accepted));
    assert(!bc_parse_ack("PWR_ACK:1:OK", &id, NULL));
    puts("PASS: strict bearer authorization and bounded serial ACK parser");
    return 0;
}
