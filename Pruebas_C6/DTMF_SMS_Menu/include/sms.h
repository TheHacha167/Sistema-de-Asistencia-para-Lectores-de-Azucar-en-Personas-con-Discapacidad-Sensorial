#ifndef SMS_H
#define SMS_H

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "secrets.h"

static const char *SMS_TAG = "SIM800";
#define MODEM_UART   UART_NUM_1
#define MODEM_TX_PIN 6
#define MODEM_RX_PIN 7
#define MODEM_BAUD   115200
#define UART_BUF     512
static char current_key[16] = "0000";

// Inicializar UART para SIM800L
static void uart_init_sms(void) {
    uart_config_t cfg = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk= UART_SCLK_DEFAULT
    };
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, UART_BUF, UART_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(SMS_TAG, "UART inicializada para SIM800L (SMS)");
}

// Enviar comando AT
static void at_send_sms(const char *cmd) {
    ESP_LOGI(SMS_TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Enviar SMS
static void send_sms_sms(const char *num, const char *txt) {
    char cmd[64];
    at_send_sms("AT+CMGF=1");
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", num);
    at_send_sms(cmd);
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(500)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(SMS_TAG, "SMS enviado a %s", num);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// Validar remitente
static bool remitente_valido_sms(const char *from) {
    return strstr(from, NUM1) || strstr(from, NUM2);
}

// Procesar comando SMS
static void procesar_sms(const char *msg, const char *from) {
    if (!remitente_valido_sms(from)) {
        ESP_LOGW(SMS_TAG, "Remitente no autorizado: %s", from);
        return;
    }
    size_t key_len = strlen(current_key);
    if (strncmp(msg, current_key, key_len) != 0) {
        ESP_LOGW(SMS_TAG, "Clave inválida: %.*s", (int)key_len, msg);
        return;
    }
    const char *p = msg + key_len;
    while (*p == ' ') p++;
    int cmd = atoi(p);
    char resp[256] = {0};
    switch (cmd) {
        case 1:
            snprintf(resp, sizeof(resp),
                "1: Listar comandos\n"
                "2: Cambiar clave\n"
                "3: Mostrar alertas\n"
                "4: Probar llamadas\n"
                "5: Probar SMS\n"
                "6: Cancelar alerta\n"
                "7: Reiniciar dispositivo\n"
                "8: Prueba sistema\n"
                "9: Estado del sistema");
            break;
        case 2: {
            const char *nueva = strchr(p, ' ');
            if (nueva && strlen(nueva + 1) < sizeof(current_key)) {
                strcpy(current_key, nueva + 1);
                snprintf(resp, sizeof(resp), "Clave cambiada a '%s'", current_key);
            } else {
                snprintf(resp, sizeof(resp), "Uso: %s 2 <nueva_clave>", current_key);
            }
            break;
        }
        case 3:
            snprintf(resp, sizeof(resp), "Alertas: ninguna");
            break;
        case 4:
            snprintf(resp, sizeof(resp), "Probando llamadas...");
            break;
        case 5:
            send_sms_sms(from, "SMS de prueba OK");
            return;
        case 6:
            snprintf(resp, sizeof(resp), "Alerta cancelada");
            break;
        case 7:
            send_sms_sms(from, "Reiniciando dispositivo...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return;
        case 8:
            snprintf(resp, sizeof(resp), "Prueba del sistema OK");
            break;
        case 9:
            snprintf(resp, sizeof(resp), "Tiempo activo: %lld s", (long long)esp_timer_get_time() / 1000000);
            break;
        default:
            snprintf(resp, sizeof(resp), "Comando desconocido. Envía '1' para ayuda.");
            break;
    }
    send_sms_sms(from, resp);
}

// Leer SMS del buzón
static void read_sms(int index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    at_send_sms(cmd);
    vTaskDelay(pdMS_TO_TICKS(500));
    char buf[UART_BUF*2] = {0};
    int len, pos = 0;
    while ((len = uart_read_bytes(MODEM_UART, (uint8_t*)buf + pos, sizeof(buf)-pos-1, pdMS_TO_TICKS(200))) > 0) {
        pos += len;
    }
    buf[pos] = '\0';
    ESP_LOGI(SMS_TAG, "CMGR>> %s", buf);
    char from[32] = {0};
    char *p = strstr(buf, "+CMGR:");
    if (p) {
        p = strchr(p, '"'); if (!p) return;
        p = strchr(p+1, '"'); if (!p) return;
        p = strchr(p+1, '"'); if (!p) return;
        char *q = strchr(p+1, '"'); if (!q) return;
        int nlen = q - (p+1);
        if (nlen > (int)sizeof(from)-1) nlen = sizeof(from)-1;
        memcpy(from, p+1, nlen); from[nlen] = '\0';
        char *msg = strstr(q+1, "\r\n");
        if (msg) {
            msg += 2;
            char *e = strstr(msg, "\r\n"); if (e) *e = '\0';
            procesar_sms(msg, from);
        }
    }
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    at_send_sms(cmd);
}

// Arrancar servicio de SMS
static void start_sms_service(void) {
    uart_init_sms();
    at_send_sms("AT+CMGF=1");
    at_send_sms("AT+CNMI=2,1,0,0,0");
    char buf[UART_BUF];
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, sizeof(buf)-1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(SMS_TAG, ">> %s", buf);
            char *cmti = strstr(buf, "+CMTI:");
            if (cmti) {
                char *comma = strchr(cmti, ',');
                if (comma) {
                    int idx = atoi(comma + 1);
                    ESP_LOGI(SMS_TAG, "New SMS index %d", idx);
                    read_sms(idx);
                }
            }
        }
    }
}

#endif // SMS_H
