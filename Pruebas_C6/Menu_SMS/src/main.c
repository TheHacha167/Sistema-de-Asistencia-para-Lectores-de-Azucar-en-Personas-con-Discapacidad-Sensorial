#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "secrets.h"
#include "esp_timer.h"


static const char *TAG = "SIM800";
#define MODEM_UART   UART_NUM_1
#define MODEM_TX_PIN 6
#define MODEM_RX_PIN 7
#define MODEM_BAUD   115200
#define UART_BUF     512
static char current_key[16] = "0000";


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
    ESP_LOGI(TAG, "UART inicializada para SIM800L");
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
    // esperar el prompt '>'
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(500)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(TAG, "SMS enviado a %s", num);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static bool remitente_valido(const char *from) {
    return strstr(from, NUM1) || strstr(from, NUM2);
}


static void procesar_sms(const char *msg, const char *from) {
    // 1) Validar remitente
    if (!remitente_valido(from)) {
        ESP_LOGW(TAG, "Remitente no autorizado: %s", from);
        return;
    }
    // 2) Validar clave
    size_t key_len = strlen(current_key);
    if (strncmp(msg, current_key, key_len) != 0) {
        ESP_LOGW(TAG, "Clave inválida: %.*s", (int)key_len, msg);
        return;
    }
    // 3) Extraer el número de comando
    const char *p = msg + key_len;
    while (*p == ' ') p++;  // saltar espacios
    int cmd = atoi(p);

    // 4) Preparar respuesta
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
            // Cambiar clave: por ejemplo "0000 1234"
            const char *nueva = strchr(p, ' ');
            if (nueva && strlen(nueva + 1) < sizeof(current_key)) {
                strcpy(current_key, nueva + 1);
                snprintf(resp, sizeof(resp), "Clave cambiada a '%s'", current_key);
            } else {
                snprintf(resp, sizeof(resp),
                         "Uso: %s 2 <nueva_clave>", current_key);
            }
            break;
        }

        case 3:
            // Aquí tu lógica de alertas
            snprintf(resp, sizeof(resp), "Alertas: ninguna");
            break;

        case 4:
            // Llamada de prueba
            // test_calls();
            snprintf(resp, sizeof(resp), "Probando llamadas...");
            break;

        case 5:
            // SMS de prueba
            send_sms(from, "SMS de prueba OK");
            return;

        case 6:
            // Cancelar alerta
            // cancel_alert();
            snprintf(resp, sizeof(resp), "Alerta cancelada");
            break;

        case 7:
            // Reiniciar dispositivo
            send_sms(from, "Reiniciando dispositivo...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return;

        case 8:
            // Prueba general del sistema
            snprintf(resp, sizeof(resp), "Prueba del sistema OK");
            break;

        case 9:
            // Estado del sistema (uptime en segundos)
            snprintf(resp, sizeof(resp),
                     "Tiempo activo: %lld s",
                     (long long)esp_timer_get_time() / 1000000);
            break;

        default:
            snprintf(resp, sizeof(resp),
                     "Comando desconocido. Envía '1' para ayuda.");
            break;
    }

    // 5) Enviar respuesta final
    send_sms(from, resp);
}

static void read_sms(int index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    at_send(cmd);
    vTaskDelay(pdMS_TO_TICKS(500));
    char buf[UART_BUF*2] = {0};
    int len, pos = 0;
    // leer toda la respuesta
    while ((len = uart_read_bytes(MODEM_UART, (uint8_t*)buf + pos, sizeof(buf)-pos-1, pdMS_TO_TICKS(200))) > 0) {
        pos += len;
    }
    buf[pos] = '\0';
    ESP_LOGI(TAG, "CMGR>> %s", buf);
    // parse +CMGR: "mem","num",...\r\nmessage\r\n
    char from[32] = {0};
    char *p = strstr(buf, "+CMGR:");
    if (p) {
        // encontrar segundo grupo de comillas que es el número
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

