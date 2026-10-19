#include <assert.h>
#include <stdio.h>
#include "power_display.h"

static void test_off_deadline(void)
{
    assert(bc_display_mode(BC_OFF, 2000, 2000) == BC_DISPLAY_OFF_ANIMATION);
    assert(bc_display_mode(BC_OFF, 2000, 11999) == BC_DISPLAY_OFF_ANIMATION);
    assert(bc_display_mode(BC_OFF, 2000, 12000) == BC_DISPLAY_BLANK);
    assert(bc_display_mode(BC_OFF, 2000, 999999) == BC_DISPLAY_BLANK);
    /* Do not unsigned-underflow if the snapshot is newer than the UI timestamp. */
    assert(bc_display_mode(BC_OFF, 2000, 1999) == BC_DISPLAY_OFF_ANIMATION);
    /* Millisecond uptime crossing 32 bits must not restart the countdown. */
    uint64_t since = UINT32_MAX - 5000ULL;
    assert(bc_display_mode(BC_OFF, since, since + 9999) == BC_DISPLAY_OFF_ANIMATION);
    assert(bc_display_mode(BC_OFF, since, since + 10000) == BC_DISPLAY_BLANK);
}

static void test_only_off_blanks(void)
{
    assert(bc_display_mode(BC_UNKNOWN, 0, 999999) == BC_DISPLAY_TELEMETRY);
    assert(bc_display_mode(BC_RUNNING, 0, 999999) == BC_DISPLAY_TELEMETRY);
    assert(bc_display_mode(BC_STOPPING, 0, 999999) == BC_DISPLAY_SHUTDOWN);
    assert(bc_display_mode(BC_SUSPENDING, 0, 999999) == BC_DISPLAY_SUSPENDING);
    assert(bc_display_mode(BC_SLEEPING, 0, 999999) == BC_DISPLAY_SLEEP);
    assert(bc_display_mode(BC_STARTING, 0, 999999) == BC_DISPLAY_STARTING);
    assert(bc_display_mode(BC_WAKING, 0, 999999) == BC_DISPLAY_WAKING);
    assert(bc_display_mode((bc_state_t)99, 0, 999999) == BC_DISPLAY_TELEMETRY);
}

static void test_shutdown_lifecycle(void)
{
    bc_power_t p;
    bc_power_init(&p, 0, true, false);
    bc_power_os_event(&p, BC_OS_RUNNING, 100);
    assert(bc_power_command(&p, BC_CMD_STOP, 500));
    assert(bc_power_step(&p, 500, true, false) == BC_EFFECT_REQUEST_STOP);
    assert(bc_display_mode(p.state, p.state_changed, 20000) == BC_DISPLAY_SHUTDOWN);
    bc_power_os_event(&p, BC_OS_OFF_CONFIRMED, 21000);
    (void)bc_power_step(&p, 21000, false, false);
    assert(bc_display_mode(p.state, p.state_changed, 23999) == BC_DISPLAY_SHUTDOWN);
    assert(bc_power_step(&p, 24000, false, false) == BC_EFFECT_LATCH);
    assert(bc_display_mode(p.state, p.state_changed, 33999) == BC_DISPLAY_OFF_ANIMATION);
    assert(bc_display_mode(p.state, p.state_changed, 34000) == BC_DISPLAY_BLANK);
    assert(bc_power_command(&p, BC_CMD_START, 35000));
    assert(bc_display_mode(p.state, p.state_changed, 35000) == BC_DISPLAY_STARTING);
}

static void test_sleep_and_resume(void)
{
    bc_power_t p;
    bc_power_init(&p, 0, true, false);
    bc_power_os_event(&p, BC_OS_RUNNING, 100);
    assert(bc_power_command(&p, BC_CMD_SLEEP, 500));
    (void)bc_power_step(&p, 500, true, false);
    assert(bc_display_mode(p.state, p.state_changed, 15000) == BC_DISPLAY_SUSPENDING);
    bc_power_os_event(&p, BC_OS_SUSPENDING, 16000);
    (void)bc_power_step(&p, 16000, false, false);
    (void)bc_power_step(&p, 16250, false, false);
    assert(bc_display_mode(p.state, p.state_changed, 999999) == BC_DISPLAY_SLEEP);
    assert(bc_power_command(&p, BC_CMD_WAKE, 1000000));
    assert(bc_display_mode(p.state, p.state_changed, 1000000) == BC_DISPLAY_WAKING);
    bc_power_os_event(&p, BC_OS_RESUMED, 1001000);
    assert(bc_display_mode(p.state, p.state_changed, 1001000) == BC_DISPLAY_TELEMETRY);
}

static void test_no_telemetry_is_not_off(void)
{
    bc_power_t p;
    bc_power_init(&p, 0, false, false);
    (void)bc_power_step(&p, 900000, false, false);
    assert(bc_display_mode(p.state, p.state_changed, 900000) == BC_DISPLAY_TELEMETRY);
    bc_power_os_event(&p, BC_OS_RUNNING, 900001);
    (void)bc_power_step(&p, 950000, false, false);
    assert(bc_display_mode(p.state, p.state_changed, 950000) == BC_DISPLAY_TELEMETRY);
    /* Explicit physical OFF confirmation and forced OFF use the same UI deadline. */
    assert(bc_power_command(&p, BC_CMD_FORCE_OFF, 960000));
    assert(bc_display_mode(p.state, p.state_changed, 969999) == BC_DISPLAY_OFF_ANIMATION);
    assert(bc_display_mode(p.state, p.state_changed, 970000) == BC_DISPLAY_BLANK);
    bc_power_init(&p, 1000000, false, false);
    assert(bc_power_command(&p, BC_CMD_CONFIRM_OFF, 1001000));
    assert(bc_display_mode(p.state, p.state_changed, 1011000) == BC_DISPLAY_BLANK);
}

int main(void)
{
    test_off_deadline();
    test_only_off_blanks();
    test_shutdown_lifecycle();
    test_sleep_and_resume();
    test_no_telemetry_is_not_off();
    puts("PASS: 5 display-policy scenarios (OFF 9999/10000ms, shutdown, sleep, wake, stale data)");
}
