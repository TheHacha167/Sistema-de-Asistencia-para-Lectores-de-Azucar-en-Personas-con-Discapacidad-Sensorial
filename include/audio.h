#ifndef AUDIO_H
#define AUDIO_H

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#define I2S_NUM             (0)
#define I2S_SAMPLE_RATE     (16000)
#define I2S_BITS            I2S_BITS_PER_SAMPLE_16BIT
#define I2S_CHANNEL_FORMAT  I2S_CHANNEL_FMT_ONLY_LEFT
#define I2S_COMM_FORMAT     I2S_COMM_FORMAT_I2S
#define I2S_PIN_BCK         GPIO_NUM_20
#define I2S_PIN_WS          GPIO_NUM_22
#define I2S_PIN_DOUT        GPIO_NUM_23

#define PIN_NUM_MOSI  GPIO_NUM_19 
#define PIN_NUM_MISO  GPIO_NUM_21 
#define PIN_NUM_CLK   GPIO_NUM_18
#define PIN_NUM_CS    GPIO_NUM_5
#define SD_MOUNT_POINT      "/sdcard"

static const char *AUDIO_TAG = "AUDIO";
static bool audio_ready = false;

static void audio_init(void) {
    // I2S config
    i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS,
        .channel_format = I2S_CHANNEL_FORMAT,
        .communication_format = I2S_COMM_FORMAT,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false
    };

    i2s_pin_config_t pin_cfg = {
        .bck_io_num = I2S_PIN_BCK,
        .ws_io_num = I2S_PIN_WS,
        .data_out_num = I2S_PIN_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM, &i2s_cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM, &pin_cfg));
    ESP_LOGI(AUDIO_TAG, "I2S inicializado.");

    // SPI SD
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.gpio_cs = PIN_NUM_CS;
    dev_cfg.host_id = host.slot;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dev_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "Falló montar la SD: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(AUDIO_TAG, "SD montada correctamente.");
    // Listar archivos en la SD para depuración
    DIR *d = opendir(SD_MOUNT_POINT);
    if (d) {
        struct dirent *ent;
        ESP_LOGI(AUDIO_TAG, "Archivos en SD:");
        while ((ent = readdir(d)) != NULL) {
            ESP_LOGI(AUDIO_TAG, "  %s", ent->d_name);
        }
        closedir(d);
    } else {
        ESP_LOGE(AUDIO_TAG, "No se pudo abrir el directorio SD");
    }
    audio_ready = true;
}

static void playWav(const char *path) {
    if (!audio_ready) {
        ESP_LOGW(AUDIO_TAG, "Audio no listo");
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(AUDIO_TAG, "No puedo abrir archivo: %s", path);
        return;
    }

    fseek(f, 44, SEEK_SET);  // Saltar cabecera WAV

    uint8_t buf[512];
    size_t len, written;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        i2s_write(I2S_NUM, buf, len, &written, portMAX_DELAY);
    }
    fclose(f);
}

#endif // AUDIO_H
