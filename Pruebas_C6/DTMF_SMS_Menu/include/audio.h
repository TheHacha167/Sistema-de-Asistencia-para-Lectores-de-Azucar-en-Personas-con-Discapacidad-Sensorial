#ifndef AUDIO_H
#define AUDIO_H

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/i2s_std.h"        
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#define AUDIO_TAG            "AUDIO"
#define I2S_NUM              (I2S_NUM_0)
#define I2S_SAMPLE_RATE      (44100)
#define I2S_BITS_PER_SAMPLE  I2S_BITS_PER_SAMPLE_16BIT
#define I2S_CHANNEL_FORMAT   I2S_CHANNEL_FMT_RIGHT_LEFT

// Pines SPI para la tarjeta SD
#define SPI_SCK_PIN GPIO_NUM_18
#define SPI_MOSI_PIN GPIO_NUM_23
#define SPI_MISO_PIN GPIO_NUM_19
#define SPI_CS_PIN   GPIO_NUM_5
#define SD_MOUNT_POINT "/sdcard"

// Inicializar I2S para salida de audio
static void i2s_init(void) {
    i2s_std_config_t cfg = I2S_STD_CONFIG_DEFAULT();
    cfg.mode = I2S_MODE_MASTER | I2S_MODE_TX;
    cfg.sample_rate = I2S_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE;
    cfg.channel_format = I2S_CHANNEL_FORMAT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_std_pin_config_t pin_cfg = I2S_STD_PIN_DEFAULT();
    pin_cfg.bck_io_num = GPIO_NUM_26;
    pin_cfg.ws_io_num  = GPIO_NUM_25;
    pin_cfg.data_out_num = GPIO_NUM_22;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;
    ESP_ERROR_CHECK(i2s_std_driver_install(I2S_NUM, &cfg));
    ESP_ERROR_CHECK(i2s_std_set_pin(I2S_NUM, &pin_cfg));
}

// montar tarjeta SD en modo SPI
static void sdcard_init(void) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    // Configurar slot para modo SPI
    slot_config.flags = SDMMC_SLOT_FLAG_SPI;
    slot_config.gpio_miso = SPI_MISO_PIN;
    slot_config.gpio_mosi = SPI_MOSI_PIN;
    slot_config.gpio_sck  = SPI_SCK_PIN;
    slot_config.gpio_cs   = SPI_CS_PIN;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "Error al montar tarjeta SD: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(AUDIO_TAG, "Tarjeta SD montada en %s", SD_MOUNT_POINT);
    }
}

// Reproducir archivo WAV desde la tarjeta SD
static void play_audio(const char *filename) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(AUDIO_TAG, "Error al abrir %s", path);
        return;
    }
    fseek(f, 44, SEEK_SET); // saltar cabecera WAV
    uint8_t buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t written;
        i2s_std_write(I2S_NUM, buf, read_bytes, &written, portMAX_DELAY);
    }
    fclose(f);
}

// Inicializar sistema de audio (I2S + tarjeta SD)
static void audio_system_init(void) {
    i2s_init();
    sdcard_init();
}

#endif // AUDIO_H
