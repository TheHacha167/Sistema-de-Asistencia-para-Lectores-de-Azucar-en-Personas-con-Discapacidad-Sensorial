#ifndef MODEM_H
#define MODEM_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"   

#include "audio.h"
#include "sms.h"          // read_sms(), procesar_sms()
#include "dtmf.h"         // caller_authorized(), dtmf_files[]


extern const uart_port_t MODEM_UART;

static const char *MODEM_TAG = "MODEM";
#define UART_BUF_LEN 512
#define MODEM_TASK_STACK 12288

/* ─────────────────────────── utilidades ─────────────────────────── */
static int read_line(char *buf, int max, int timeout_ms)
{
    int len = 0;
    int64_t t0 = esp_timer_get_time();
    while (((esp_timer_get_time() - t0) / 1000) < timeout_ms && len < max - 1) {
        if (uart_read_bytes(MODEM_UART, (uint8_t *)buf + len, 1,
                            pdMS_TO_TICKS(50)) == 1 && buf[len++] == '\n')
            break;
    }
    buf[len] = '\0';
    return len;
}

static void send_at_wait(const char *cmd)
{
    ESP_LOGI(MODEM_TAG, "AT> %s", cmd);
    uart_write_bytes(MODEM_UART, cmd, strlen(cmd));
    uart_write_bytes(MODEM_UART, "\r", 1);

    char l[96];
    /* lee hasta OK/ERROR o agota 1,5 s */
    while (read_line(l, sizeof l, 1500) > 0) {
        ESP_LOGI(MODEM_TAG, "<< %s", l);
        if (strstr(l, "OK") || strstr(l, "ERROR")) break;
    }
}

/* ──────────────────────── manejadores URC ──────────────────────── */
static void handle_sms_push(const char *first)
{
    char sender[32] = {0};
    sscanf(first, "+CMT: \"%31[^\"]\"", sender);

    char body[161] = {0};
    if (read_line(body, sizeof body, 3000) <= 0) return;
    ESP_LOGI(MODEM_TAG, "SMS %s: %s", sender, body);

    procesar_sms(sender, body);          // función del usuario
}

static void handle_sms_notification(const char *line)
{
    char *comma = strchr(line, ',');
    if (!comma) return;
    int idx = atoi(comma + 1);
    read_sms(idx);
}

static void handle_call_notification(const char *line)
{
    ESP_LOGI(MODEM_TAG, "URC llamada: %s", line);
    char raw[32] = {0}, full[36] = {0};
    // Acepta +CLIP: o +CLIP2:
    if (strstr(line, "+CLIP:")) {
        sscanf(line, "+CLIP: \"%31[^\"]\"", raw);
    } else if (strstr(line, "+CLIP2:")) {
        sscanf(line, "+CLIP2: \"%31[^\"]\"", raw);
    } else {
        ESP_LOGW(MODEM_TAG, "URC llamada desconocida: %s", line);
        return;
    }
    snprintf(full, sizeof full, "+34%s", raw);
    ESP_LOGI(MODEM_TAG, "Llamada de %s", full);
    if (!caller_authorized(full)) {
        ESP_LOGW(MODEM_TAG, "No autorizado");
        //uart_write_bytes(MODEM_UART, "ATH\r", 4);
        return;
    }
    uart_write_bytes(MODEM_UART, "ATA\r", 4);
    vTaskDelay(pdMS_TO_TICKS(300));
    // Enviar lista de comandos por SMS al descolgar
    send_sms_to(NUM1, "Comando 1: Listar comandos.\nComando 2: Cambiar clave.\nComando 3: Mostrar alertas.\nComando 4: Probar llamadas.\nComando 5: Probar SMS.\nComando 6: Cancelar alerta.\nComando 7: Reiniciar dispositivo.\nComando 8: Prueba sistema.\nComando 9: Estado del sistema.");
    if (audio_ready) playWav(SD_MOUNT_POINT "/menu.wav");
    // NO colgar aquí: dejar la llamada abierta para DTMF
}

static void handle_dtmf_event(const char *line)
{
    ESP_LOGI(MODEM_TAG, "DTMF URC recibido: %s", line);
    char tone = 0;
    if (sscanf(line, "+DTMF: %c", &tone) != 1) {
        ESP_LOGW(MODEM_TAG, "DTMF malformado: %s", line);
        return;
    }
    ESP_LOGI(MODEM_TAG, "DTMF recibido: %c", tone);
    if (tone == '*') {
        if (audio_ready) playWav(SD_MOUNT_POINT "/menu.wav");
        return;
    }
    if (tone < '0' || tone > '9') {
        ESP_LOGW(MODEM_TAG, "DTMF fuera de rango: %c", tone);
        return;
    }
    int idx = tone - '0';
    // Primero reproducir el audio correspondiente
    if (audio_ready) {
        const char *dtmf_audio_files[10] = {
            "0.wav",  
            "1.wav",   
            "2.wav",     
            "3.wav",           
            "4.wav",       
            "5.wav",        
            "6.wav",    
            "7.wav",  
            "8.wav",  
            "9.wav"  
        };
        char path[64];
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", dtmf_audio_files[idx]);
        ESP_LOGI(MODEM_TAG, "Reproduciendo: %s", path);
        playWav(path);
    }
    // Después de reproducir el audio, ejecutar la acción asociada
    if (tone >= '1' && tone <= '9') {
        char sms_msg[64];
        if (tone == '7') {
            send_sms_to(NUM1, "Reiniciando...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return;
        } else if (tone == '9') {
            snprintf(sms_msg, sizeof(sms_msg), "Tiempo activo: %lld s", (long long)esp_timer_get_time()/1000000);
            send_sms_to(NUM1, sms_msg);
        } else if (tone == '8') {
            send_sms_to(NUM1, "Prueba sistema OK");
        } else {
            const char *sms_msgs[10] = {
                "Opción inválida.",
                "Comando 1: Listar comandos.",
                "Comando 2: Cambiar clave.",
                "Comando 3: Mostrar alertas.",
                "Comando 4: Probar llamadas.",
                "Comando 5: Probar SMS.",
                "Comando 6: Cancelar alerta.",
                "Comando 7: Reiniciar dispositivo.",
                "Comando 8: Prueba sistema.",
                "Comando 9: Estado del sistema."
            };
            send_sms_to(NUM1, sms_msgs[idx]);
        }
    }
    uart_write_bytes(MODEM_UART, "ATH\r", 4);
}

/* ────────────────────────── tarea módem ────────────────────────── */
void modem_task(void *arg)
{
    (void)arg;
    /* secuencia de arranque */
    send_at_wait("AT");
    send_at_wait("ATE0");                // sin eco
    send_at_wait("AT+IPR=115200");       // fija baudios
    send_at_wait("AT+IFC=0,0");          // sin RTS/CTS
    send_at_wait("AT+CMGF=1");           // SMS texto
    send_at_wait("AT+CNMI=2,1,0,0,0");   // URC +CMTI
    send_at_wait("AT+CLIP=1");
    send_at_wait("AT+DDET=1,0");         // DTMF SIEMPRE con ,0

    char line[UART_BUF_LEN];

    while (true) {
        if (read_line(line, sizeof line, 30000) <= 0) continue;
        ESP_LOGI(MODEM_TAG, "<< %s", line);

        if (strstr(line, "+CMT:"))       handle_sms_push(line);
        else if (strstr(line, "+CMTI:")) handle_sms_notification(line);
        else if (strstr(line, "+CLIP:")) handle_call_notification(line);
        else if (strstr(line, "+DTMF:")) handle_dtmf_event(line);
    }
}

#endif /* MODEM_H */
