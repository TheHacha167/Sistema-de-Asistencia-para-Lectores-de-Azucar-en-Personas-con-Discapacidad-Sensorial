#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "secrets.h"

static const char *TAG = "SIM800";
#define MODEM_UART   UART_NUM_1
#define MODEM_TX_PIN 6
#define MODEM_RX_PIN 7
#define MODEM_BAUD   115200
#define UART_BUF     512
#define CLAVE        "0000"

static void uart_init(void) {
    uart_config_t cfg = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk= UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, UART_BUF, UART_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART inicializado para el SIM800L");
}

static void at_send(const char *cmd) {
    ESP_LOGI(TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void send_sms(const char *num, const char *txt) {
    char cmd[64];
    at_send("AT+CMGF=1");
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", num);
    at_send(cmd);
    // wait for '>' prompt
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(500)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(TAG, "SMS sent to %s", num);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static bool remitente_valido(const char *from) {
    return strstr(from, NUM1) || strstr(from, NUM2);
}

static void procesar_sms(const char *msg, const char *from) {
    if (!remitente_valido(from)) {
        ESP_LOGW(TAG, "Remitente no autorizado: %s", from);
        return;
    }
    if (strncmp(msg, CLAVE, strlen(CLAVE)) != 0) {
        ESP_LOGW(TAG, "Clave no válida: %s", msg);
        return;
    }
    int cmd = atoi(msg + strlen(CLAVE));
    char resp[64];
    if (cmd >= 1 && cmd <= 10) {
        snprintf(resp, sizeof(resp), "Comando %d ejecutado", cmd);
    } else {
        snprintf(resp, sizeof(resp), "Comando fuera del rango (1-10)");
    }
    send_sms(from, resp);
}

static void read_sms(int index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    at_send(cmd);
    vTaskDelay(pdMS_TO_TICKS(500));
    char buf[UART_BUF*2] = {0};
    int len, pos = 0;
    // read all data available
    while ((len = uart_read_bytes(MODEM_UART, (uint8_t*)buf + pos, sizeof(buf)-pos-1, pdMS_TO_TICKS(200))) > 0) {
        pos += len;
    }
    buf[pos] = '\0';
    ESP_LOGI(TAG, "CMGR>> %s", buf);
    // parse +CMGR: "mem","num",...\r\nmessage\r\n
    char from[32] = {0};
    char *p = strstr(buf, "+CMGR:");
    if (p) {

        // skip +CMGR:
        p = strchr(p, '"'); if (!p) return;
        p = strchr(p+1, '"'); if (!p) return;
        p = strchr(p+1, '"'); if (!p) return;
        char *q = strchr(p+1, '"'); if (!q) return;
        int nlen = q - (p+1);
        if (nlen > (int)sizeof(from)-1) nlen = sizeof(from)-1;
        memcpy(from, p+1, nlen); from[nlen] = '\0';
        // el mensaje comienza después de CRLF
        char *msg = strstr(q+1, "\r\n");
        if (msg) {
            msg += 2;
            char *e = strstr(msg, "\r\n"); if (e) *e = '\0';
            ESP_LOGI(TAG, "SMS de %s: %s", from, msg);
            procesar_sms(msg, from);
        }
    }
    // borrar SMS
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    at_send(cmd);
}

void app_main(void) {
    uart_init();
    at_send("AT+CMGF=1");
    at_send("AT+CNMI=2,1,0,0,0");
    char buf[UART_BUF];
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, sizeof(buf)-1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, ">> %s", buf);
            char *cmti = strstr(buf, "+CMTI:");
                if (cmti) {
                    char *comma = strchr(cmti, ',');
                    if (comma) {
                        int idx = atoi(comma + 1);
                        ESP_LOGI(TAG, "Nuevo SMS en el índice %d", idx);
                        read_sms(idx);
                    }
                }
            }
        }
    }

