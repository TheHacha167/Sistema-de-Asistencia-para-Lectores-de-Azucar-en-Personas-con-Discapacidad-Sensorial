#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2s.h"   
#include "esp_log.h"
#include "esp_err.h"

#include "audio.h"
#include "modem.h"
#include "secrets.h"

// ==================== PINES / CONFIG ====================
static const char *ALERT_TAG = "ALERT";
static const char *MAIN_TAG  = "MAIN";

// Sensor vibración
#define PIN_VIB_SENSOR    GPIO_NUM_2    // D0 sensor (activo alto)

// *** Mapeo final relés ***
#define PIN_RELAY_VIB1    GPIO_NUM_8    // Vibrador 1
#define PIN_RELAY_VIB2    GPIO_NUM_10   // Vibrador 2
#define PIN_RELAY_LIGHT   GPIO_NUM_11   // Lamparita (luz)

// Futuro relé (reservado) 
#define PIN_RELAY_EXTRA   GPIO_NUM_12

// Otros pines
#define PIN_BTN_RESET     GPIO_NUM_4    // botón (pull-up, activo a GND)
#define PIN_ARM_SENSE     GPIO_NUM_3    // lector puentea a GND (activo-bajo)

// Lógicas
#define RELAY_ACTIVE_HIGH_DEFAULT  0    // 0 = activo-bajo (LOW=ON), 1 = activo-alto
#define SENSOR_ACTIVE_HIGH         1
#define BTN_ACTIVE_LOW             1
#define ARM_ACTIVE_WHEN_HIGH       0

// Antirruido del sensor
#define VIB_WINDOW_MS           800
#define VIB_THRESHOLD           3

// Tiempos
#define ATTEND_TIMEOUT_MS       30000
#define CALL_PLAY_MS            12000
#define LAMP_ON_MS              30000

// Módem (UART1)
#define MODEM_TX_PIN  GPIO_NUM_6
 
#define MODEM_BAUD    115200
const uart_port_t MODEM_UART = UART_NUM_1;

// I2S (audio.h usa legacy)
#ifndef I2S_NUM
#define I2S_NUM I2S_NUM_0
#endif

// ==================== ESTADO / GLOBALES ====================
typedef enum { ST_IDLE=0, ST_ARMED, ST_ALARM, ST_CALLING } alert_state_t;
static volatile alert_state_t g_state = ST_IDLE;

static TimerHandle_t g_timer_alarm      = NULL;
static TimerHandle_t g_timer_lamp       = NULL;
static TimerHandle_t g_timer_vib_window = NULL;
static TimerHandle_t g_timer_test       = NULL;

static volatile uint16_t g_vib_count = 0;
static volatile bool g_test_active   = false;

// Polaridad runtime (0=activo-bajo, 1=activo-alto)
static volatile int g_relay_active_high = RELAY_ACTIVE_HIGH_DEFAULT;

// ==================== PROTOTIPOS NECESARIOS (callbacks y exports) ====================
static void vTimerVibWindowTimeout(TimerHandle_t xTimer);
static void vTimerAlarmTimeout(TimerHandle_t xTimer);
static void vTimerLampTimeout(TimerHandle_t xTimer);
static void vTimerTestTimeout(TimerHandle_t xTimer);

// Exportadas para SMS (8/41/42/43/44/90/91)
void alert_test_start(uint32_t ms);
void relays_force_for_ms(bool vib1, bool vib2, bool lamp, uint32_t ms);
void set_relay_polarity(int active_high);
int  get_relay_polarity(void) { return g_relay_active_high; }

// Módem / audio
void      modem_task(void *arg);
esp_err_t modem_init(void);
esp_err_t modem_dial(const char *number);
void      modem_hangup(void);
void      audio_play_wav(const char *path);

// ==================== HELPERS GPIO ====================
static inline void relay_write(gpio_num_t pin, bool on) {
    int level = g_relay_active_high ? (on ? 1 : 0) : (on ? 0 : 1);
    gpio_set_level(pin, level);
}
static inline void relays_set(bool vib1, bool vib2, bool lamp) {
    relay_write(PIN_RELAY_VIB1, vib1);
    relay_write(PIN_RELAY_VIB2, vib2);
    relay_write(PIN_RELAY_LIGHT, lamp);
}
static inline void lamp_set(bool on) { relay_write(PIN_RELAY_LIGHT, on); }

static inline bool read_sensor_active(void) {
    int v = gpio_get_level(PIN_VIB_SENSOR);
    return SENSOR_ACTIVE_HIGH ? (v==1) : (v==0);
}
static inline bool read_button_pressed(void) {
    int v = gpio_get_level(PIN_BTN_RESET);
    return BTN_ACTIVE_LOW ? (v==0) : (v==1);
}
static inline bool read_arm_present(void) {
    int v = gpio_get_level(PIN_ARM_SENSE);
    return ARM_ACTIVE_WHEN_HIGH ? (v==1) : (v==0);
}

// ==================== ESTADOS ====================
static void enter_idle(void) {
    g_state = ST_IDLE;
    if (g_timer_alarm) xTimerStop(g_timer_alarm, 0);
    if (!g_test_active) relays_set(false,false,false);
    ESP_LOGI(ALERT_TAG, "Estado: IDLE (lector ausente)");
}
static void enter_armed(void) {
    g_state = ST_ARMED;
    if (g_timer_alarm) xTimerStop(g_timer_alarm, 0);
    if (!g_test_active) relays_set(false,false,false);
    ESP_LOGI(ALERT_TAG, "Estado: ARMED (lector presente)");
}

// ==================== TIMERS ====================
static void vTimerVibWindowTimeout(TimerHandle_t xTimer) {
    g_vib_count = 0;
}
static void vTimerAlarmTimeout(TimerHandle_t xTimer) {
    if (g_state != ST_ALARM) return;
    ESP_LOGW(ALERT_TAG, "No atendida en 30s → llamadas");
    g_state = ST_CALLING;

    const char *numbers[] = { NUM1, NUM2 };
    for (int i=0; i<2; i++) {
        ESP_LOGI(ALERT_TAG, "Llamando a %s …", numbers[i]);
        if (modem_dial(numbers[i]) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2500));
            audio_play_wav("/sdcard/alert.wav");
            vTaskDelay(pdMS_TO_TICKS(CALL_PLAY_MS));
            modem_hangup();
        }
        vTaskDelay(pdMS_TO_TICKS(800));
    }
    relays_set(false,false,true);  // vib OFF, lámpara ON fija
}
static void vTimerLampTimeout(TimerHandle_t xTimer) {
    lamp_set(false);
    ESP_LOGI(ALERT_TAG, "Lamparita OFF (timeout)");
}
static void vTimerTestTimeout(TimerHandle_t xTimer) {
    g_test_active = false;
    relays_set(false,false,false);
    ESP_LOGI(ALERT_TAG, "TEST/FORCE: FIN (relés OFF)");
}

// ==================== ISR ====================
static void IRAM_ATTR isr_btn(void *arg) {
    if (g_test_active) { // cancelar test/forzado si estaba activo
        BaseType_t hpw2 = pdFALSE;
        relays_set(false,false,false);
        if (g_timer_test) xTimerStopFromISR(g_timer_test, &hpw2);
        g_test_active = false;
        if (hpw2) portYIELD_FROM_ISR();
    }
    if (read_button_pressed()) {
        BaseType_t hpw = pdFALSE;
        lamp_set(true);
        if (g_timer_lamp) {
            xTimerStopFromISR(g_timer_lamp, &hpw);
            xTimerStartFromISR(g_timer_lamp, &hpw);
        }
        if (g_state == ST_ALARM || g_state == ST_CALLING) {
            relays_set(false,false,false);
            if (g_timer_alarm) xTimerStopFromISR(g_timer_alarm, &hpw);
            modem_hangup();
            g_state = read_arm_present() ? ST_ARMED : ST_IDLE;
            ESP_EARLY_LOGI(ALERT_TAG, "Atendido por botón. Lámpara ON %d ms.", LAMP_ON_MS);
        }
        if (hpw) portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR isr_sensor(void *arg) {
    if (g_test_active) return; // ignora durante test/forzado
    if (g_state == ST_ARMED && read_arm_present()) {
        uint16_t c = ++g_vib_count;
        BaseType_t hpw = pdFALSE;
        if (g_timer_vib_window) {
            xTimerStopFromISR(g_timer_vib_window, &hpw);
            xTimerStartFromISR(g_timer_vib_window, &hpw);
        }
        if (c >= VIB_THRESHOLD) {
            g_vib_count = 0;
            if (g_timer_vib_window) xTimerStopFromISR(g_timer_vib_window, &hpw);
            g_state = ST_ALARM;
            relays_set(true,true,true);
            if (g_timer_alarm) {
                xTimerStopFromISR(g_timer_alarm, &hpw);
                xTimerStartFromISR(g_timer_alarm, &hpw);
            }
            ESP_EARLY_LOGE(ALERT_TAG, "ALERTA: %d vibraciones en %d ms",
                           VIB_THRESHOLD, VIB_WINDOW_MS);
        }
        if (hpw) portYIELD_FROM_ISR();
    }
}

// ==================== TAREAS ====================
static void arm_monitor_task(void *arg) {
    bool prev = read_arm_present();
    ESP_LOGI(ALERT_TAG, "ARM monitor: start, present=%s", prev ? "YES" : "NO");
    for (;;) {
        if (g_test_active) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        vTaskDelay(pdMS_TO_TICKS(100));
        bool now = read_arm_present();
        if (now != prev) {
            if (now) enter_armed(); else enter_idle();
            prev = now;
        }
    }
}

// ==================== INIT ====================
static void alert_system_start(void) {
    gpio_install_isr_service(0);

    // Salidas (3 relés) + extra reservado comentado
    gpio_config_t outcfg = {
        .pin_bit_mask = (1ULL<<PIN_RELAY_VIB1)|
                        (1ULL<<PIN_RELAY_VIB2)|
                        (1ULL<<PIN_RELAY_LIGHT)
                        /* | (1ULL<<PIN_RELAY_EXTRA) */,
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&outcfg);
    relays_set(false,false,false);

    // Sensor vibración (PULL-DOWN para que no flote si no está)
    gpio_config_t incfg = {
        .pin_bit_mask = (1ULL<<PIN_VIB_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en   = SENSOR_ACTIVE_HIGH ? GPIO_PULLUP_DISABLE  : GPIO_PULLUP_ENABLE,
        .pull_down_en = SENSOR_ACTIVE_HIGH ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = SENSOR_ACTIVE_HIGH ? GPIO_INTR_POSEDGE    : GPIO_INTR_NEGEDGE
    };
    gpio_config(&incfg);
    gpio_isr_handler_add(PIN_VIB_SENSOR, isr_sensor, NULL);

    // Botón
    gpio_config_t btncfg = {
        .pin_bit_mask = (1ULL<<PIN_BTN_RESET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en   = BTN_ACTIVE_LOW ? GPIO_PULLUP_ENABLE  : GPIO_PULLUP_DISABLE,
        .pull_down_en = BTN_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type    = BTN_ACTIVE_LOW ? GPIO_INTR_NEGEDGE   : GPIO_INTR_POSEDGE
    };
    gpio_config(&btncfg);
    gpio_isr_handler_add(PIN_BTN_RESET, isr_btn, NULL);

    // ARM (lector a GND)
    gpio_config_t armcfg = {
        .pin_bit_mask = (1ULL<<PIN_ARM_SENSE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&armcfg);

    // Timers
    g_timer_alarm      = xTimerCreate("attend_tmo", pdMS_TO_TICKS(ATTEND_TIMEOUT_MS), pdFALSE, NULL, vTimerAlarmTimeout);
    g_timer_lamp       = xTimerCreate("lamp_tmo",   pdMS_TO_TICKS(LAMP_ON_MS),       pdFALSE, NULL, vTimerLampTimeout);
    g_timer_vib_window = xTimerCreate("vib_win",    pdMS_TO_TICKS(VIB_WINDOW_MS),    pdFALSE, NULL, vTimerVibWindowTimeout);
    if (!g_timer_test)
        g_timer_test   = xTimerCreate("test_tmo",   pdMS_TO_TICKS(10000),            pdFALSE, NULL, vTimerTestTimeout);

    if (read_arm_present()) enter_armed(); else enter_idle();
    xTaskCreate(arm_monitor_task, "arm_monitor", 2048, NULL, 5, NULL);

    ESP_LOGI(ALERT_TAG, "Sistema de alerta listo.");
}

// ==================== AUDIO ====================
void audio_play_wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE("AUDIO", "No puedo abrir %s", path); return; }
    fseek(f, 44, SEEK_SET); // cabecera WAV
    uint8_t buf[1024];
    size_t rd, wr;
    while ((rd = fread(buf, 1, sizeof buf, f)) > 0) {
        i2s_write(I2S_NUM, buf, rd, &wr, portMAX_DELAY);
    }
    fclose(f);
}

// ==================== MÓDEM ====================
esp_err_t modem_init(void)
{
    uart_config_t ucfg = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        #if defined(UART_SCLK_DEFAULT)
            .source_clk = UART_SCLK_DEFAULT
        #elif defined(UART_SCLK_APB)
            .source_clk = UART_SCLK_APB
        #else
            .source_clk = UART_SCLK_RTC
        #endif
    };
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &ucfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, MODEM_TX_PIN, MODEM_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, 1024, 0, 0, NULL, 0));

    if (xTaskCreate(modem_task, "modem_task", 12288, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE("MODEM", "No pude crear modem_task");
        return ESP_FAIL;
    }
    ESP_LOGI(MAIN_TAG, "UART del módem listo a %d baudios", MODEM_BAUD);
    return ESP_OK;
}

esp_err_t modem_dial(const char *number)
{
    if (!number || !*number) return ESP_ERR_INVALID_ARG;
    char cmd[48];
    int n = snprintf(cmd, sizeof cmd, "ATD%s;\r", number);
    if (n <= 0 || n >= (int)sizeof cmd) return ESP_ERR_INVALID_ARG;
    uart_write_bytes(MODEM_UART, cmd, n);
    return ESP_OK;
}

void modem_hangup(void)
{
    static const char *cmd = "ATH\r";
    uart_write_bytes(MODEM_UART, cmd, 4);
}

// ==================== PRUEBAS / FORZADOS (para SMS) ====================
void alert_test_start(uint32_t ms) {
    g_test_active = true;
    relays_set(true,true,true);  // ON continuo durante 'ms'
    if (!g_timer_test) g_timer_test = xTimerCreate("test_tmo", pdMS_TO_TICKS(ms), pdFALSE, NULL, vTimerTestTimeout);
    xTimerStop(g_timer_test, 0);
    xTimerChangePeriod(g_timer_test, pdMS_TO_TICKS(ms), 0);
    xTimerStart(g_timer_test, 0);
    ESP_LOGI(ALERT_TAG, "TEST: relés ON por %u ms (sin llamadas)", (unsigned)ms);
}

void relays_force_for_ms(bool vib1, bool vib2, bool lamp, uint32_t ms) {
    g_test_active = true;
    relays_set(vib1, vib2, lamp);
    if (!g_timer_test) g_timer_test = xTimerCreate("test_tmo", pdMS_TO_TICKS(ms), pdFALSE, NULL, vTimerTestTimeout);
    xTimerStop(g_timer_test, 0);
    xTimerChangePeriod(g_timer_test, pdMS_TO_TICKS(ms), 0);
    xTimerStart(g_timer_test, 0);
    ESP_LOGI(ALERT_TAG, "FORCE: v1=%d v2=%d lamp=%d por %u ms",
             (int)vib1, (int)vib2, (int)lamp, (unsigned)ms);
}

void set_relay_polarity(int active_high) {
    g_relay_active_high = active_high ? 1 : 0;
    ESP_LOGW(ALERT_TAG, "Polaridad relés: %s",
             g_relay_active_high ? "ACTIVO-ALTO (HIGH=ON)" : "ACTIVO-BAJO (LOW=ON)");
}

// ==================== MAIN ====================
void app_main(void) {
    audio_init();          // I2S + SD
    (void)dtmf_files;      // silenciar warning unused
    modem_init();          // UART + modem_task
    alert_system_start();  // lógica de alerta

    ESP_LOGI(MAIN_TAG, "Sistema iniciado.");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
