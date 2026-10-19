#include <assert.h>
#include <stdio.h>
#include "power_fsm.h"

static bc_power_t p;

static void init_off(void)
{
    bc_power_init(&p, 0, false, false);
    bc_power_step(&p, 400, false, false);
    assert(bc_power_command(&p, BC_CMD_CONFIRM_OFF, 400));
    assert(p.state == BC_OFF);
}

static void init_running(void)
{
    bc_power_init(&p, 0, true, false);
    assert(bc_power_step(&p, 400, true, false) == 0);
    /* High STATE proves powered hardware, not an awake Linux kernel. */
    assert(p.state == BC_UNKNOWN && p.latch_known && p.latch_on);
    bc_power_os_event(&p, BC_OS_RUNNING, 401);
    assert(p.state == BC_RUNNING && p.latch_known && p.latch_on);
}

static void test_unknown_is_not_off(void)
{
    bc_power_init(&p, 0, false, false);
    assert(bc_power_step(&p, 20000, false, false) == 0);
    assert(p.state == BC_UNKNOWN);
    assert(!bc_power_command(&p, BC_CMD_START, 20000));
    assert(!bc_power_command(&p, BC_CMD_FORCE_OFF, 20000));
}

static void test_start_is_idempotent_and_delays_board_button(void)
{
    init_off();
    assert(bc_power_command(&p, BC_CMD_START, 500));
    assert(bc_power_command(&p, BC_CMD_START, 501));
    assert(bc_power_step(&p, 501, false, false) == BC_EFFECT_LATCH);
    assert(!bc_power_command(&p, BC_CMD_FORCE_OFF, 502));
    assert(bc_power_step(&p, 2499, false, false) == 0);
    assert(bc_power_step(&p, 2500, false, false) == BC_EFFECT_BOARD_BUTTON);
    assert(bc_power_step(&p, 4000, false, false) == 0);
    bc_power_step(&p, 5000, true, false);
    assert(bc_power_step(&p, 5400, true, false) == 0);
    assert(p.state == BC_RUNNING);
    assert(bc_power_command(&p, BC_CMD_START, 5500));
    assert(bc_power_step(&p, 5500, true, false) == 0);
}

static void test_autoboot_does_not_press_power_button(void)
{
    init_off();
    bc_power_command(&p, BC_CMD_START, 500);
    assert(bc_power_step(&p, 500, false, false) == BC_EFFECT_LATCH);
    bc_power_step(&p, 600, true, false);
    bc_power_step(&p, 1000, true, false);
    assert(p.state == BC_RUNNING);
    assert(bc_power_step(&p, 5000, true, false) == 0);
}

static void test_pin_or_host_loss_never_cuts_power(void)
{
    init_running();
    bc_power_step(&p, 500, false, false);
    assert(bc_power_step(&p, 1000, false, false) == 0);
    assert(bc_power_step(&p, 1000000, false, false) == 0);
    assert(p.latch_on);
}

static void test_stop_requires_final_shutdown_confirmation(void)
{
    init_running();
    assert(bc_power_command(&p, BC_CMD_STOP, 500));
    assert(bc_power_step(&p, 500, true, false) == BC_EFFECT_REQUEST_STOP);
    assert(bc_power_command(&p, BC_CMD_STOP, 600));
    bc_power_os_event(&p, BC_OS_SHUTTING_DOWN, 700);
    bc_power_step(&p, 800, false, false);
    assert(bc_power_step(&p, 5000, false, false) == 0);
    assert(p.latch_on);
    bc_power_os_event(&p, BC_OS_OFF_CONFIRMED, 5100);
    assert(bc_power_step(&p, 8099, false, false) == 0);
    assert(bc_power_step(&p, 8100, false, false) == BC_EFFECT_LATCH);
    assert(p.state == BC_OFF && !p.latch_on);
    assert(bc_power_step(&p, 10000, false, false) == 0);
}

static void test_sleep_never_cuts_atx_and_wake_is_idempotent(void)
{
    init_running();
    assert(bc_power_command(&p, BC_CMD_SLEEP, 500));
    assert(bc_power_step(&p, 500, true, false) == BC_EFFECT_REQUEST_SLEEP);
    assert(bc_power_command(&p, BC_CMD_SLEEP, 600));
    bc_power_os_event(&p, BC_OS_SUSPENDING, 700);
    bc_power_step(&p, 800, false, false);
    assert(bc_power_step(&p, 1200, false, false) == 0);
    assert(p.state == BC_SLEEPING && p.latch_on);
    bc_power_os_event(&p, BC_OS_RUNNING, 1300); /* Heartbeat already in flight. */
    assert(p.state == BC_SLEEPING);
    assert(bc_power_step(&p, 100000, false, false) == 0);
    assert(bc_power_command(&p, BC_CMD_WAKE, 100100));
    assert(bc_power_command(&p, BC_CMD_WAKE, 100101));
    assert(bc_power_step(&p, 100101, false, false) == BC_EFFECT_BOARD_BUTTON);
    bc_power_os_event(&p, BC_OS_RUNNING, 100500);
    assert(p.state == BC_RUNNING);
}

static void test_stop_while_sleeping_wakes_then_requests_stop(void)
{
    init_running();
    bc_power_os_event(&p, BC_OS_SUSPENDING, 500);
    bc_power_step(&p, 600, false, false);
    bc_power_step(&p, 1000, false, false);
    assert(bc_power_command(&p, BC_CMD_STOP, 1100));
    assert(bc_power_step(&p, 1100, false, false) == BC_EFFECT_BOARD_BUTTON);
    bc_power_os_event(&p, BC_OS_RUNNING, 2000);
    assert(p.state == BC_STOPPING);
    assert(bc_power_step(&p, 2000, true, false) == BC_EFFECT_REQUEST_STOP);
}

static void test_resume_cancels_off_confirmation(void)
{
    init_running();
    bc_power_os_event(&p, BC_OS_OFF_CONFIRMED, 500);
    bc_power_os_event(&p, BC_OS_RUNNING, 600);
    bc_power_step(&p, 700, false, false);
    assert(bc_power_step(&p, 10000, false, false) == 0);
    assert(p.latch_on);
}

static void test_final_hook_resynchronizes_after_esp_reset(void)
{
    bc_power_init(&p, 0, false, false);
    bc_power_os_event(&p, BC_OS_OFF_CONFIRMED, 1000);
    bc_power_step(&p, 1400, false, false);
    assert(bc_power_step(&p, 4000, false, false) == BC_EFFECT_LATCH);
    assert(p.state == BC_OFF);
}

static void test_short_press_stops_without_latch_pulse(void)
{
    init_running();
    bc_power_step(&p, 500, true, true);
    bc_power_step(&p, 550, true, true);
    bc_power_step(&p, 700, true, false);
    assert(bc_power_step(&p, 750, true, false) == BC_EFFECT_REQUEST_STOP);
    assert(p.latch_on);
}

static void test_long_press_forces_off_once_and_release_does_not_restart(void)
{
    init_running();
    bc_power_step(&p, 500, true, true);
    bc_power_step(&p, 550, true, true);
    assert(bc_power_step(&p, 8549, true, true) == 0);
    assert(bc_power_step(&p, 8550, true, true) == BC_EFFECT_LATCH);
    assert(p.state == BC_OFF);
    assert(bc_power_step(&p, 12000, false, true) == 0);
    bc_power_step(&p, 13000, false, false);
    assert(bc_power_step(&p, 13050, false, false) == 0);
    assert(p.state == BC_OFF);
}

static void test_boot_with_button_held_is_ignored_until_released(void)
{
    bc_power_init(&p, 0, true, true);
    bc_power_os_event(&p, BC_OS_RUNNING, 1);
    assert(bc_power_step(&p, 10000, true, true) == 0);
    assert(p.state == BC_RUNNING);
    bc_power_step(&p, 10100, true, false);
    assert(bc_power_step(&p, 10200, true, false) == 0);
    assert(p.state == BC_RUNNING);
}

static void test_timeouts_and_rejections_are_fail_safe(void)
{
    init_off();
    assert(!bc_power_command(&p, BC_CMD_STOP, 500));
    assert(!bc_power_command(&p, BC_CMD_SLEEP, 500));
    assert(!bc_power_command(&p, BC_CMD_WAKE, 500));
    assert(!bc_power_command(&p, (bc_command_t)99, 500));
    bc_power_command(&p, BC_CMD_START, 600);
    bc_power_step(&p, 600, false, false);
    bc_power_step(&p, 3000, false, false);
    assert(bc_power_step(&p, 100000, false, false) == 0);
    assert(p.state == BC_UNKNOWN && p.latch_on);
    assert(!bc_power_command(&p, BC_CMD_START, 100001));
    assert(!bc_power_command(&p, BC_CMD_CONFIRM_OFF, 100001));
    assert(bc_power_command(&p, BC_CMD_FORCE_OFF, 100002));
    assert(bc_power_step(&p, 100002, false, false) == BC_EFFECT_LATCH);
}

static void test_start_refuses_state_that_contradicts_confirmed_off(void)
{
    init_off();
    bc_power_step(&p, 1000, true, false);
    assert(!bc_power_command(&p, BC_CMD_START, 1001));
    assert(bc_power_step(&p, 1400, true, false) == 0);
}

static void test_stop_timeout_and_sleep_cancel_never_cut(void)
{
    init_running();
    bc_power_command(&p, BC_CMD_STOP, 500);
    bc_power_step(&p, 500, true, false);
    assert(bc_power_step(&p, 130000, true, false) == 0);
    assert(p.latch_on);
    bc_power_os_event(&p, BC_OS_SUSPENDING, 130001);
    bc_power_os_event(&p, BC_OS_RUNNING, 130002);
    /* A periodic heartbeat in flight must not cancel suspend intent. */
    assert(p.state == BC_SUSPENDING);
    bc_power_os_event(&p, BC_OS_RESUMED, 130003);
    assert(p.state == BC_RUNNING);
    assert(bc_power_command(&p, BC_CMD_WAKE, 130003));
    assert(bc_power_step(&p, 130003, true, false) == 0);
    assert(!bc_power_command(&p, BC_CMD_CONFIRM_OFF, 130004));
}

static void test_pending_suspend_is_not_permission_to_pulse_power(void)
{
    init_running();
    assert(bc_power_command(&p, BC_CMD_SLEEP, 500));
    assert(bc_power_step(&p, 500, true, false) == BC_EFFECT_REQUEST_SLEEP);
    assert(!bc_power_command(&p, BC_CMD_WAKE, 501));
    assert(!bc_power_command(&p, BC_CMD_STOP, 502));
    assert(!bc_power_command(&p, BC_CMD_START, 503));
    /* A low pin without the OS suspend hook still is not proof of sleep. */
    bc_power_step(&p, 1000, false, false);
    assert(bc_power_step(&p, 1400, false, false) == 0);
    assert(p.state == BC_SUSPENDING);
    assert(!bc_power_command(&p, BC_CMD_WAKE, 1401));
    bc_power_os_event(&p, BC_OS_SUSPENDING, 1500);
    assert(bc_power_step(&p, 1501, false, false) == 0);
    assert(p.state == BC_SLEEPING);
    assert(bc_power_command(&p, BC_CMD_WAKE, 1502));
    assert(bc_power_step(&p, 1502, false, false) == BC_EFFECT_BOARD_BUTTON);
}

static void test_rising_state_blocks_wake_before_resume_notification(void)
{
    init_running();
    bc_power_os_event(&p, BC_OS_SUSPENDING, 500);
    bc_power_step(&p, 600, false, false);
    bc_power_step(&p, 1000, false, false);
    assert(p.state == BC_SLEEPING);
    bc_power_step(&p, 1100, true, false);
    assert(!bc_power_command(&p, BC_CMD_WAKE, 1101));
    assert(!bc_power_command(&p, BC_CMD_STOP, 1102));
    assert(bc_power_step(&p, 1400, true, false) == 0);
    bc_power_os_event(&p, BC_OS_RESUMED, 1500);
    assert(p.state == BC_RUNNING && p.latch_on);
}

int main(void)
{
    test_unknown_is_not_off();
    test_start_is_idempotent_and_delays_board_button();
    test_autoboot_does_not_press_power_button();
    test_pin_or_host_loss_never_cuts_power();
    test_stop_requires_final_shutdown_confirmation();
    test_sleep_never_cuts_atx_and_wake_is_idempotent();
    test_stop_while_sleeping_wakes_then_requests_stop();
    test_resume_cancels_off_confirmation();
    test_final_hook_resynchronizes_after_esp_reset();
    test_short_press_stops_without_latch_pulse();
    test_long_press_forces_off_once_and_release_does_not_restart();
    test_boot_with_button_held_is_ignored_until_released();
    test_timeouts_and_rejections_are_fail_safe();
    test_start_refuses_state_that_contradicts_confirmed_off();
    test_stop_timeout_and_sleep_cancel_never_cut();
    test_pending_suspend_is_not_permission_to_pulse_power();
    test_rising_state_blocks_wake_before_resume_notification();
    assert(bc_power_state_name(BC_OFF)[0] == 'o');
    assert(bc_power_state_name((bc_state_t)99)[0] == 'u');
    puts("PASS: 17 BC250 power-state scenarios; no real hardware actions");
    return 0;
}
