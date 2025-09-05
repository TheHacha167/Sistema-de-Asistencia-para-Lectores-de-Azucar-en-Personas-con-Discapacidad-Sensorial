#ifndef SMS_H
#define SMS_H

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "secrets.h"

// === Hook: prueba de sistema ( en main.c) ===
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void relays_force_for_ms(bool vib1, bool vib2, bool lamp, uint32_t ms);
void set_relay_polarity(int active_high);
int  get_relay_polarity(void);
#ifdef __cplusplus
}
#endif


#include <stdint.h>

#ifndef TEST_BUZZ_MS
#define TEST_BUZZ_MS 10000   // 10 s de prueba
#endif

#ifdef __cplusplus
extern "C" {
#endif
void alert_test_start(uint32_t ms);   // implementada en main.c
#ifdef __cplusplus
}
#endif


extern const uart_port_t MODEM_UART;

static const char *SMS_TAG = "SIM800_SMS";
//#define MODEM_UART   UART_NUM_1
#define UART_BUF_LEN 512

// Clave actual para comandos SMS
static char current_key[16] = "0000";

// Envía un comando AT + CR
static void at_send_sms(const char *cmd) {
    ESP_LOGI(SMS_TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Envía un SMS de texto al número `num`
static void send_sms_to(const char *num, const char *txt) {
    char cmd[64];
    at_send_sms("AT+CMGF=1");
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", num);
    at_send_sms(cmd);
    // Espera prompt '>'
    char c;
    while (uart_read_bytes(MODEM_UART, (uint8_t*)&c, 1, pdMS_TO_TICKS(500)) > 0) {
        if (c == '>') break;
    }
    uart_write_bytes(MODEM_UART, txt, strlen(txt));
    uart_write_bytes(MODEM_UART, "\x1A", 1); // Ctrl+Z
    ESP_LOGI(SMS_TAG, "SMS enviado a %s: %s", num, txt);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// Comprueba remitente válido contra NUM1 y NUM2
static bool remitente_valido(const char *from) {
    return (strcmp(from, NUM1) == 0) || (strcmp(from, NUM2) == 0);
}

// Procesa el cuerpo del SMS y envía la respuesta adecuada
static void procesar_sms(const char *msg, const char *from) {
    if (!remitente_valido(from)) {
        ESP_LOGW(SMS_TAG, "Unauthorized sender: %s", from);
        return;
    }
    size_t key_len = strlen(current_key);
    if (strncmp(msg, current_key, key_len) != 0) {
        ESP_LOGW(SMS_TAG, "Invalid key: %.*s", (int)key_len, msg);
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
                "3: Mostrar alertas\n" // No hay alertas en esta versión
                "4: Probar llamadas\n"
                "5: Probar SMS\n"
                "6: Cancelar alerta\n"
                "7: Reiniciar dispositivo\n"
                "8: Prueba sistema\n"
                "9: Estado del sistema");
            break;
        case 2: {
            const char *nuev = strchr(p, ' ');
            if (nuev && strlen(nuev+1) < sizeof(current_key)) {
                strcpy(current_key, nuev+1);
                snprintf(resp, sizeof(resp), "Clave cambiada a %s", current_key);
            } else {
                snprintf(resp, sizeof(resp), "Uso: %s 2 <nueva_clave>", current_key);
            }
            break;
        }
        case 3:
            snprintf(resp, sizeof(resp), "Alertas: ninguna");
            break;
        case 4:
            snprintf(resp, sizeof(resp), "Probando llamada...");
            
            break;
        case 5:
            send_sms_to(from, "SMS de prueba OK");
            return;
        case 6:
            snprintf(resp, sizeof(resp), "Alerta cancelada (no implementada en esta versión)");
            break;
        case 7:
            send_sms_to(from, "Reiniciando...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return;
        case 8:  // Prueba sistema
            alert_test_start(TEST_BUZZ_MS);   // vibra 10 s, sin llamadas
            snprintf(resp, sizeof(resp), "Prueba de sistema: vibracion 10 s (sin llamadas)");
            break;
        case 9:
            snprintf(resp, sizeof(resp), "Tiempo activo: %lld s",
                     (long long)esp_timer_get_time()/1000000);
            break;
            case 41: // Forzar VIB1 5 s
        relays_force_for_ms(true, false, false, 5000);
        snprintf(resp, sizeof(resp), "VIB1 ON 5s");
        break;
        case 42: // Forzar VIB2 5 s
            relays_force_for_ms(false, true, false, 5000);
            snprintf(resp, sizeof(resp), "VIB2 ON 5s");
            break;
    case 43: // Forzar LAMP 5 s
        relays_force_for_ms(false, false, true, 5000);
        snprintf(resp, sizeof(resp), "LAMP ON 5s");
        break;
    case 44: // Forzar TODOS 5 s
        relays_force_for_ms(true, true, true, 5000);
        snprintf(resp, sizeof(resp), "TODOS ON 5s");
        break;
    case 90: { // Cambiar polaridad: 0=activo-bajo, 1=activo-alto
        const char *arg = strchr(p, ' ');
        int ah = (arg ? atoi(arg+1) : -1);
        if (ah==0 || ah==1) {
            set_relay_polarity(ah);
            snprintf(resp, sizeof(resp), "Polaridad: %s", ah ? "ACTIVO-ALTO" : "ACTIVO-BAJO");
        } else {
            snprintf(resp, sizeof(resp), "Uso: %s 90 <0|1>", current_key);
        }
        break;
    }
    case 91: // Consultar polaridad
        snprintf(resp, sizeof(resp), "Polaridad actual: %s",
                get_relay_polarity() ? "ACTIVO-ALTO" : "ACTIVO-BAJO");
        break;
        default:
            snprintf(resp, sizeof(resp), "Comando desconocido. Envía '1' para ayuda.");
            break;
    }
    ESP_LOGI(SMS_TAG, "Respondiendo a %s: %s", from, resp);
    send_sms_to(from, resp);
}

// Lee SMS en índice 'idx', parsea y procesa
static void read_sms(int idx) {
    at_send_sms("AT+CMGF=1"); // Fuerza modo texto antes de leer
    at_send_sms("AT+CPMS=\"ME\",\"ME\",\"ME\""); // Fuerza almacenamiento en memoria interna
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", idx);
    at_send_sms(cmd);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Aumenta el tiempo de espera

    char buf[UART_BUF_LEN*4] = {0}; // Buffer más grande
    int pos = 0, len;
    while ((len = uart_read_bytes(MODEM_UART,
                                  (uint8_t*)buf + pos,
                                  sizeof(buf) - pos - 1,
                                  pdMS_TO_TICKS(200))) > 0) {
        ESP_LOGI(SMS_TAG, "[CMGR] Recibidos %d bytes", len);
        for (int i = 0; i < len; ++i) {
            ESP_LOGI(SMS_TAG, "[CMGR] Byte %d: 0x%02X (%c)", i, (unsigned char)buf[pos+i],
                     isprint((unsigned char)buf[pos+i]) ? buf[pos+i] : '.');
        }
        pos += len;
    }
    buf[pos] = '\0';
    ESP_LOGI(SMS_TAG, "CMGR>> %s", buf);

    // Extraer remitente y mensaje
    char from[32] = {0};
    char *p = strstr(buf, "+CMGR:");
    if (p) {
        char *q1 = strchr(p, '"');
        char *q2 = q1 ? strchr(q1+1, '"') : NULL;
        char *q3 = q2 ? strchr(q2+1, '"') : NULL;
        char *q4 = q3 ? strchr(q3+1, '"') : NULL;
        if (q3 && q4) {
            int nlen = q4 - (q3+1);
            if (nlen > sizeof(from)-1) nlen = sizeof(from)-1;
            memcpy(from, q3+1, nlen);
            from[nlen] = '\0';
            char *msg = strstr(q4+1, "\r\n");
            if (msg) {
                msg += 2;
                char *e = strstr(msg, "\r\n");
                if (e) *e = '\0';
                procesar_sms(msg, from);
            }
        }
    }
    // Borrar SMS
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", idx);
    at_send_sms(cmd);
}

#endif // SMS_H
