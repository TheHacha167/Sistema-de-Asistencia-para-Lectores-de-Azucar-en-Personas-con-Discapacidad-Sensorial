#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "dtmf.h"
#include "sms.h"
#include "secrets.h"

#define TASK_STACK_SZ 4096
#define TASK_PRIORITY 5

static const char *MODEM_TAG = "MODEM_TASK";

void modem_task(void *arg) {
    // Inicialización UART una sola vez
    uart_init_sms();       // instala driver y configura pines
    at_send_sms("AT+CMGF=1");
    at_send_sms("AT+CNMI=2,1,0,0,0"); // Notificaciones SMS
    at_send_dtmf("AT+CLIP=1");       // Mostrar ID de llamada
    at_send_dtmf("AT+DDET=1");       // Activar detección DTMF tras ATA

    char buf[UART_BUF_LEN];
    while (true) {
        int len = uart_read_bytes(MODEM_UART, (uint8_t*)buf, UART_BUF_LEN-1, pdMS_TO_TICKS(1000));
        if (len <= 0) continue;
        buf[len] = '\0';
        ESP_LOGI(MODEM_TAG, ">> %s", buf);

        // Evento SMS
        if (strstr(buf, "+CMTI:")) {
            ESP_LOGI(MODEM_TAG, "Nueva notificación de SMS");
            char *comma = strchr(buf, ',');
            if (comma) {
                int idx = atoi(comma + 1);
                ESP_LOGI(MODEM_TAG, "Leyendo SMS %d", idx);
                read_sms(idx);
            }
        }

        // Evento llamada entrante
        if (strstr(buf, "+CLIP:")) {
            // Extraer número bruto entre comillas
            char *p = strchr(buf, '"');
            char *q = p ? strchr(p+1, '"') : NULL;
            if (!p || !q) continue;

            char raw_num[32] = {0};
            int n = q - (p+1);
            if (n > (int)sizeof(raw_num)-1) n = sizeof(raw_num)-1;
            memcpy(raw_num, p+1, n);

            // Normalizar con prefijo +34 si no está presente
            char full_num[32] = {0};
            if (raw_num[0] == '+') {
                // Ya incluye prefijo
                strncpy(full_num, raw_num, sizeof(full_num)-1);
            } else {
                const char *prefix = "+34";
                // Copiar prefijo
                strncpy(full_num, prefix, sizeof(full_num)-1);
                // Concatenar parte numérica truncada si es necesario
                size_t remaining = sizeof(full_num) - 1 - strlen(prefix);
                strncat(full_num, raw_num, remaining);
            }

            ESP_LOGI(MODEM_TAG, "Llamada de: %s (normalizado: %s)", raw_num, full_num);

            // Verificar autorización con número normalizado
            if (remitente_valido_sms(full_num)) {
                ESP_LOGI(MODEM_TAG, "Número autorizado, contestando");
                at_send_dtmf("ATA");
                vTaskDelay(pdMS_TO_TICKS(500));
                task_listen_dtmf(full_num);
                at_send_dtmf("ATH");
                vTaskDelay(pdMS_TO_TICKS(500));
                ESP_LOGI(MODEM_TAG, "Llamada finalizada");
            } else {
                ESP_LOGW(MODEM_TAG, "Número no autorizado: %s", full_num);
            }
        }
    }
}

void app_main(void) {
    xTaskCreate(modem_task, "modem_task", TASK_STACK_SZ, NULL, TASK_PRIORITY, NULL);
}
