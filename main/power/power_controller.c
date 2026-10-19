#include "power_controller.h"
#include "power_security.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "drivers/usb_serial_comm.h"

#if CONFIG_BC250_ENABLE
static const char *TAG = "BC250";
typedef enum { MESSAGE_COMMAND, MESSAGE_OS, MESSAGE_ACK } message_type_t;
typedef struct { message_type_t type; int value; uint32_t id; } message_t;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_status_mutex;
static char s_status[256] = "{\"state\":\"unknown\"}";
static bc_power_snapshot_t s_snapshot = { .state = BC_UNKNOWN };

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static bool free_gpio(int pin)
{
    /* Board-specific allowlist, not a generic ESP32-S3 pin claim. */
    return pin == 2 || pin == 4 || pin == 5 || (pin >= 13 && pin <= 18) ||
           pin == 21 || (pin >= 33 && pin <= 39) || pin == 41 || pin == 42;
}

static bool enqueue(message_t msg)
{
    return s_queue && xQueueSend(s_queue, &msg, 0) == pdTRUE;
}

bool bc250_power_submit(bc_command_t command)
{
    /* FORCE_OFF is deliberately physical-button-only, not a remote command. */
    if (command == BC_CMD_FORCE_OFF) return false;
    return enqueue((message_t){ .type = MESSAGE_COMMAND, .value = command });
}

bool bc250_power_status_json(char *buffer, size_t size)
{
    if (!s_status_mutex || xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    int n = snprintf(buffer, size, "%s", s_status);
    xSemaphoreGive(s_status_mutex);
    return n >= 0 && (size_t)n < size;
}

bool bc250_power_snapshot(bc_power_snapshot_t *snapshot)
{
    if (!snapshot || !s_status_mutex ||
        xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    *snapshot = s_snapshot;
    xSemaphoreGive(s_status_mutex);
    return true;
}

bool bc250_power_handle_serial(const char *line)
{
    static const struct { const char *line; bc_os_event_t event; } events[] = {
        {"PWR_OS:RUNNING", BC_OS_RUNNING}, {"PWR_OS:RESUMED", BC_OS_RESUMED},
        {"PWR_OS:SUSPENDING", BC_OS_SUSPENDING},
        {"PWR_OS:SHUTTING_DOWN", BC_OS_SHUTTING_DOWN},
        {"PWR_OS:OFF_CONFIRMED", BC_OS_OFF_CONFIRMED}
    };
    if (strcmp(line, "PWR_GET") == 0) {
        char status[256];
        if (bc250_power_status_json(status, sizeof(status)))
            usb_serial_sendf("PWR_STATUS:%s\n", status);
        return true;
    }
    if (strcmp(line, "PWR_CONFIRM:OFF") == 0) {
        (void)bc250_power_submit(BC_CMD_CONFIRM_OFF);
        return true;
    }
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        if (strcmp(line, events[i].line) == 0) {
            if (!enqueue((message_t){ .type = MESSAGE_OS, .value = events[i].event }))
                usb_serial_send("PWR_ERROR:QUEUE_FULL\n");
            return true;
        }
    }
    if (strncmp(line, "PWR_ACK:", 8) == 0) {
        uint32_t id;
        bool accepted;
        if (bc_parse_ack(line, &id, &accepted)) {
            (void)enqueue((message_t){ .type = MESSAGE_ACK, .id = id,
                .value = accepted });
        }
        return true;
    }
    return strncmp(line, "PWR_", 4) == 0;
}

static void control_task(void *arg)
{
    (void)arg;
    bc_power_t power;
    uint64_t now = now_ms();
    bc_power_init(&power, now, gpio_get_level(CONFIG_BC250_STATE_GPIO),
                  !gpio_get_level(CONFIG_BC250_BUTTON_GPIO));
    power.long_press_ms = CONFIG_BC250_LONG_PRESS_MS;
    power.boot_button_delay_ms = CONFIG_BC250_BOOT_BUTTON_DELAY_MS;
    uint64_t latch_end = 0, board_end = 0, ack_deadline = 0;
    uint32_t sequence = esp_random(), awaiting_ack = 0;
    const char *last_error = "none";

    for (;;) {
        now = now_ms();
        if (latch_end && now >= latch_end) {
            gpio_set_level(CONFIG_BC250_LATCH_GPIO, 0);
            latch_end = 0;
        }
        if (board_end && now >= board_end) {
            gpio_set_level(CONFIG_BC250_BOARD_BUTTON_GPIO, 0);
            board_end = 0;
        }
        message_t message;
        if (xQueueReceive(s_queue, &message, 0) == pdTRUE) {
            if (message.type == MESSAGE_COMMAND) {
                bool contradictory_start = message.value == BC_CMD_START &&
                    power.state == BC_OFF && gpio_get_level(CONFIG_BC250_STATE_GPIO);
                bool contradictory_wake = power.state == BC_SLEEPING &&
                    (message.value == BC_CMD_WAKE || message.value == BC_CMD_START ||
                     message.value == BC_CMD_STOP) && gpio_get_level(CONFIG_BC250_STATE_GPIO);
                bool accepted = !contradictory_start && !contradictory_wake &&
                    bc_power_command(&power, message.value, now);
                last_error = accepted ? "none" : "command_rejected";
                usb_serial_send(accepted ? "PWR_RESULT:OK\n" : "PWR_RESULT:REJECTED\n");
            } else if (message.type == MESSAGE_OS) {
                bc_power_os_event(&power, message.value, now);
            } else if (awaiting_ack && message.id == awaiting_ack) {
                last_error = message.value ? "none" : "host_rejected";
                if (!message.value && gpio_get_level(CONFIG_BC250_STATE_GPIO))
                    bc_power_os_event(&power, BC_OS_RESUMED, now);
                awaiting_ack = 0;
            }
        }
        uint32_t effects = bc_power_step(&power, now,
            gpio_get_level(CONFIG_BC250_STATE_GPIO), !gpio_get_level(CONFIG_BC250_BUTTON_GPIO));
        if (effects & BC_EFFECT_LATCH) {
            if (!power.latch_on) {
                gpio_set_level(CONFIG_BC250_BOARD_BUTTON_GPIO, 0);
                board_end = 0;
                awaiting_ack = 0;
            }
            gpio_set_level(CONFIG_BC250_LATCH_GPIO, 1);
            latch_end = now + 150;
        }
        if ((effects & BC_EFFECT_BOARD_BUTTON) && power.latch_on) {
            gpio_set_level(CONFIG_BC250_BOARD_BUTTON_GPIO, 1);
            board_end = now + 250;
        }
        if (effects & (BC_EFFECT_REQUEST_STOP | BC_EFFECT_REQUEST_SLEEP)) {
            if (++sequence == 0) ++sequence;
            awaiting_ack = sequence;
            ack_deadline = now + 10000;
            usb_serial_sendf("PWR_REQUEST:%" PRIu32 ":%s\n", sequence,
                effects & BC_EFFECT_REQUEST_STOP ? "STOP" : "SLEEP");
        }
        if (awaiting_ack && now >= ack_deadline) {
            awaiting_ack = 0;
            last_error = "host_ack_timeout"; /* No fallback to hard power-off. */
        }
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_snapshot = (bc_power_snapshot_t){ power.state, power.state_changed };
            snprintf(s_status, sizeof(s_status),
                "{\"state\":\"%s\",\"latch_known\":%s,\"atx_expected_on\":%s,"
                "\"state_pin_high\":%s,\"last_error\":\"%s\"}",
                bc_power_state_name(power.state), power.latch_known ? "true" : "false",
                power.latch_on ? "true" : "false", power.state_high ? "true" : "false", last_error);
            xSemaphoreGive(s_status_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bc250_power_init(void)
{
    const int pins[] = {CONFIG_BC250_BUTTON_GPIO, CONFIG_BC250_LATCH_GPIO,
        CONFIG_BC250_STATE_GPIO, CONFIG_BC250_BOARD_BUTTON_GPIO};
    for (size_t i = 0; i < 4; ++i) {
        if (!free_gpio(pins[i])) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (pins[i] == pins[j]) return ESP_ERR_INVALID_ARG;
    }
    /* Set output latches LOW before enabling the drivers. No start pulse on boot. */
    gpio_set_level(CONFIG_BC250_LATCH_GPIO, 0);
    gpio_set_level(CONFIG_BC250_BOARD_BUTTON_GPIO, 0);
    gpio_config_t outputs = { .pin_bit_mask = (1ULL << CONFIG_BC250_LATCH_GPIO) |
        (1ULL << CONFIG_BC250_BOARD_BUTTON_GPIO), .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "outputs");
    gpio_config_t button = { .pin_bit_mask = 1ULL << CONFIG_BC250_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_RETURN_ON_ERROR(gpio_config(&button), TAG, "button");
    gpio_config_t state = { .pin_bit_mask = 1ULL << CONFIG_BC250_STATE_GPIO,
        /* R8 on the BC250 PCB already supplies the specified 100k pulldown. */
        .mode = GPIO_MODE_INPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&state), TAG, "state input");
    s_queue = xQueueCreate(16, sizeof(message_t));
    s_status_mutex = xSemaphoreCreateMutex();
    if (!s_queue || !s_status_mutex) return ESP_ERR_NO_MEM;
    if (xTaskCreate(control_task, "bc250_power", 4096, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Power controller enabled; unknown latch state until synchronized");
    return ESP_OK;
}
#else
esp_err_t bc250_power_init(void) { return ESP_OK; }
bool bc250_power_submit(bc_command_t command) { (void)command; return false; }
bool bc250_power_handle_serial(const char *line) { (void)line; return false; }
bool bc250_power_status_json(char *buffer, size_t size) { (void)buffer; (void)size; return false; }
bool bc250_power_snapshot(bc_power_snapshot_t *snapshot) { (void)snapshot; return false; }
#endif
