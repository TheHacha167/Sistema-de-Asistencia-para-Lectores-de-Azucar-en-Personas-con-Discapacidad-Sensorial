#ifndef DTMF_H
#define DTMF_H

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "secrets.h"

static const char *DTMF_TAG = "SIM800_DTMF";
#define MODEM_UART    UART_NUM_1
#define TX_PIN        6
#define RX_PIN        7
#define BAUD_RATE     115200
#define UART_BUF_LEN  512

// Inicializar UART para SIM800L (configura parámetros y pines)
static void uart_init_dtmf(void) {
    uart_config_t cfg = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk= UART_SCLK_DEFAULT
    };
    // Driver ya instalado en uart_init_sms; solo reconfiguramos parámetros y pines
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(DTMF_TAG, "UART configurada para SIM800L (DTMF)");
}

// Enviar comando AT
static void at_send_dtmf(const char *cmd) {
    ESP_LOGI(DTMF_TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Esperar OK o ERROR
static bool wait_ok_dtmf(int timeout_ms) {
    int64_t start = esp_timer_get_time();
    char resp[UART_BUF_LEN] = {0};
    int len = 0;
    while ((esp_timer_get_time() - start)/1000 < timeout_ms) {
        int r = uart_read_bytes(MODEM_UART, (uint8_t*)resp + len, 1, pdMS_TO_TICKS(100));
        if (r > 0) {
            len += r;
            resp[len] = '\0';
            if (strstr(resp, "OK")) return true;
            if (strstr(resp, "ERROR")) return false;
        }
    }
    return false;
}

// Enviar SMS con dígito DTMF
static void send_sms_dtmf(const char *num, const char *txt) {
    char cmd[64];
    at_send_dtmf("AT+CMGF=1");
    wait_ok_dtmf(1000);
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", num);
    at_send_dtmf(cmd);
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(1000)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(DTMF_TAG, "SMS enviado a %s: %s", num, txt);
    wait_ok_dtmf(10000);
}

// Escuchar y procesar DTMF
static void task_listen_dtmf(const char *caller) {
    char buf[UART_BUF_LEN];
    bool sms_sent = false;
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, UART_BUF_LEN-1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(DTMF_TAG, ">> %s", buf);
            char *dtmf = strstr(buf, "+DTMF:");
            if (dtmf) {
                char *q1 = strchr(dtmf, '"');
                char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2 && q2 > q1 + 1) {
                    char digit = *(q1 + 1);
                    if (digit >= '1' && digit <= '9' && !sms_sent) {
                        ESP_LOGI(DTMF_TAG, "DTMF detectado: %c", digit);
                        at_send_dtmf("ATH");
                        wait_ok_dtmf(2000);
                        char sms_msg[32];
                        snprintf(sms_msg, sizeof(sms_msg), "DTMF %c recibido", digit);
                        send_sms_dtmf(caller, sms_msg);
                        sms_sent = true;
                    }
                }
            }
        }
    }
}

// Inicializa detección DTMF y arranca la tarea
static void start_dtmf_detection(const char *caller) {
    uart_init_dtmf();
    at_send_dtmf("AT+CMGF=1");
    at_send_dtmf("AT+DDET=1");
    at_send_dtmf("AT+CLIP=1");
    at_send_dtmf("AT+CNMI=2,1,0,0,0");
    task_listen_dtmf(caller);
}

#endif // DTMF_H
