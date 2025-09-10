
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

static const char *TAG = "SIM800_DTMF";
#define MODEM_UART    UART_NUM_1
#define TX_PIN        6
#define RX_PIN        7
#define BAUD_RATE     115200
#define UART_BUF_LEN  512

// Inicializar UART para SIM800L
typedef int64_t esp_timer_time_t;
static void uart_init(void) {
    uart_config_t cfg = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk= UART_SCLK_DEFAULT
    };
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, UART_BUF_LEN, UART_BUF_LEN, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART inicializada para SIM800L");
}

// Enviar comando AT
static void at_send(const char *cmd) {
    ESP_LOGI(TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Esperar OK o ERROR
static bool wait_ok(int timeout_ms) {
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

// Enviar SMS de notificación
static void send_sms(const char *num, const char *txt) {
    char cmd[64];
    at_send("AT+CMGF=1");
    wait_ok(1000);
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", num);
    at_send(cmd);
    // Esperar prompt '>'
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(1000)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1);
    ESP_LOGI(TAG, "SMS enviado a %s: %s", num, txt);
    if (!wait_ok(10000)) {
        ESP_LOGW(TAG, "No se recibió OK tras enviar SMS");
    }
}

// Valida si remitente está en secrets.h
static bool remitente_valido(const char *from) {
    return strstr(from, NUM1) || strstr(from, NUM2);
}

void app_main(void) {
    uart_init();

    // Configurar SMS texto, DTMF y CLIP
    at_send("AT+CMGF=1");   // SMS texto
    at_send("AT+DDET=1");   // Activar detección DTMF
    at_send("AT+CLIP=1");   // Mostrar ID de llamada
    at_send("AT+CNMI=2,1,0,0,0");

    // Esperar llamada entrante y contestar solo si viene de NUM1 o NUM2
    ESP_LOGI(TAG, "Esperando llamada de remitente autorizado...");
    char buf[UART_BUF_LEN];
    char caller[32] = {0};
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, sizeof(buf) - 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, ">> %s", buf);
            char *clip = strstr(buf, "+CLIP:");
            if (clip) {
                char *q1 = strchr(clip, '"');
                char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2 && q2 > q1 + 1) {
                    int nlen = q2 - (q1 + 1);
                    if (nlen > (int)sizeof(caller) - 1) nlen = sizeof(caller) - 1;
                    memcpy(caller, q1 + 1, nlen);
                    caller[nlen] = '\0';
                    // Normalizar sin '+', añadir '+34' con límite
                    if (caller[0] != '+') {
                        char tmp[32];
                        int max_copy = sizeof(tmp) - 4;
                        snprintf(tmp, sizeof(tmp), "+34%.*s", max_copy, caller);
                        strncpy(caller, tmp, sizeof(caller) - 1);
                        caller[sizeof(caller) - 1] = '\0';
                    }
                    if (remitente_valido(caller)) {
                        ESP_LOGI(TAG, "Contestando llamada de %s", caller);
                        at_send("ATA");  // Contesta
                        ESP_LOGI(TAG, "Autenticado");
                        break;
                    } else {
                        ESP_LOGW(TAG, "Llamada no autorizada de %s, ignorando.", caller);
                    }
                }
            }
        }
    }

    // Escuchar tonos DTMF y enviar un único SMS
    ESP_LOGI(TAG, "Llamada contestada, escuchando DTMF...");
    bool sms_enviado = false;
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, sizeof(buf) - 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, ">> %s", buf);
            char *dtmf = strstr(buf, "+DTMF:");
            if (dtmf) {
                char *q1 = strchr(dtmf, '"');
                char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2 && q2 > q1 + 1) {
                    char digit = *(q1 + 1);
                    if (digit >= '1' && digit <= '9' && !sms_enviado) {
                        ESP_LOGI(TAG, "DTMF detectado: %c", digit);
                        // Cuelga la llamada para volver al modo comando
                        at_send("ATH");
                        wait_ok(2000);
                        // Enviar SMS con el dígito
                        char sms_msg[32];
                        snprintf(sms_msg, sizeof(sms_msg), "DTMF %c recibido", digit);
                        send_sms(caller, sms_msg);
                        sms_enviado = true;
                    }
                }
            }
        }
    }
}
