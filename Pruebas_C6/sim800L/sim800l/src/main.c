#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "secrets.h"
#include "esp_timer.h"

#ifndef NUM1
  #error "NUM1 not defined añádelo a include/secrets.h"
#endif
#ifndef NUM2
  #error "NUM2 not defined  añádelo a include/secrets.h"
#endif

static const char *TAG = "SIM800";

#define MODEM_UART          UART_NUM_1
#define MODEM_TX_PIN        6
#define MODEM_RX_PIN        7
#define MODEM_BAUD          115200
#define UART_BUF            512

static void uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = MODEM_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, UART_BUF, UART_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void at_send(const char *cmd)
{
    ESP_LOGI(TAG, "» %s", cmd);
    ESP_ERROR_CHECK(uart_flush(MODEM_UART));
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
}

static bool wait_ok(int timeout_ms)
{
    int64_t t0 = esp_timer_get_time();
    char buf[UART_BUF] = {0};
    int len = 0;
    while ((esp_timer_get_time() - t0) / 1000 < timeout_ms) {
        int n = uart_read_bytes(MODEM_UART, (uint8_t *)buf + len, sizeof(buf) - len - 1, pdMS_TO_TICKS(50));
        if (n > 0) {
            len += n;
            buf[len] = '\0';
            if (strstr(buf, "OK\r\n")) {
                return true;
            }
            if (strstr(buf, "ERROR")) {
                ESP_LOGE(TAG, "Modem respondió ERROR: %s", buf);
                return false;
            }
        }
    }
    ESP_LOGW(TAG, "No OK en %d ms (got: %s)", timeout_ms, buf);
    return false;
}

static bool send_with_retry(const char *cmd, int retries)
{
    for (int i = 0; i < retries; ++i) {
        at_send(cmd);
        if (wait_ok(2000)) return true;
        ESP_LOGW(TAG, "Reintentando '%s' (%d/%d)...", cmd, i + 1, retries);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    return false;
}

static void send_sms(const char *number, const char *text)
{
    char cmd[64];
    send_with_retry("AT+CMGF=1", 3);

    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    at_send(cmd);

    char c;
    do {
        if (uart_read_bytes(MODEM_UART, (uint8_t *)&c, 1, pdMS_TO_TICKS(1000)) == 1 && c == '>') {
            break;
        }
    } while (true);

    uart_write_bytes(MODEM_UART, text, strlen(text));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(TAG, "SMS enviado a %s", number);
    wait_ok(10000);
}

static void call_number(const char *number, uint32_t ms)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    at_send(cmd);
    vTaskDelay(pdMS_TO_TICKS(ms));
    at_send("ATH");
    wait_ok(2000);
    ESP_LOGI(TAG, "Llamada %s finalizada", number);
}

void app_main(void)
{
    uart_init();
    vTaskDelay(pdMS_TO_TICKS(3000));

    send_with_retry("AT+CFUN=1", 3);
    send_with_retry("ATE0", 3);

    if (!send_with_retry("AT", 5)) {
        ESP_LOGE(TAG, "SIM800L no responde tras múltiples intentos. Abortando.");
        return;
    }

    send_sms(NUM1, "Prueba TFG  SMS #1");
    send_sms(NUM2, "Prueba TFG  SMS #2");

    call_number(NUM1, 10000);
    vTaskDelay(pdMS_TO_TICKS(5000));
    call_number(NUM2, 10000);

    ESP_LOGI(TAG, "Secuencia completa. Entrando en bucle…");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
