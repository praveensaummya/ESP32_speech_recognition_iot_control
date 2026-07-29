/**
 * Custom board configuration for ESP32-S3 with INMP441 I2S microphone
 *
 * INMP441 connections:
 *   SD  (data)  -> GPIO2
 *   SCK (clock) -> GPIO41
 *   WS  (WS)    -> GPIO42
 */
#pragma once

#include "driver/gpio.h"
#include "esp_idf_version.h"

/**
 * @brief Custom board I2C GPIO definition
 * Not used with INMP441, but required by framework
 */
#define FUNC_I2C_EN     (0)
#define I2C_NUM         (0)
#define I2C_CLK         (600000)
#define GPIO_I2C_SCL    (GPIO_NUM_NC)
#define GPIO_I2C_SDA    (GPIO_NUM_NC)

/**
 * @brief SD card not used
 */
#define FUNC_SDMMC_EN   (0)
#define SDMMC_BUS_WIDTH (4)
#define GPIO_SDMMC_CLK  (GPIO_NUM_NC)
#define GPIO_SDMMC_CMD  (GPIO_NUM_NC)
#define GPIO_SDMMC_D0   (GPIO_NUM_NC)
#define GPIO_SDMMC_D1   (GPIO_NUM_NC)
#define GPIO_SDMMC_D2   (GPIO_NUM_NC)
#define GPIO_SDMMC_D3   (GPIO_NUM_NC)
#define GPIO_SDMMC_DET  (GPIO_NUM_NC)

#define FUNC_SDSPI_EN       (0)
#define SDSPI_HOST          (SPI2_HOST)
#define GPIO_SDSPI_CS       (GPIO_NUM_NC)
#define GPIO_SDSPI_SCLK     (GPIO_NUM_NC)
#define GPIO_SDSPI_MISO     (GPIO_NUM_NC)
#define GPIO_SDSPI_MOSI     (GPIO_NUM_NC)

/**
 * @brief INMP441 I2S GPIO definition
 *   SD  (data)  -> GPIO2
 *   SCK (clock) -> GPIO41
 *   WS  (WS)    -> GPIO42
 */
#define FUNC_I2S_EN         (1)
#define GPIO_I2S_LRCK       (GPIO_NUM_42)  // WS
#define GPIO_I2S_MCLK       (GPIO_NUM_NC)  // Not used
#define GPIO_I2S_SCLK       (GPIO_NUM_41)  // SCK
#define GPIO_I2S_SDIN       (GPIO_NUM_2)   // SD (data in)
#define GPIO_I2S_DOUT       (GPIO_NUM_NC)  // No speaker output

#define FUNC_I2S0_EN         (0)
#define GPIO_I2S0_LRCK       (GPIO_NUM_NC)
#define GPIO_I2S0_MCLK       (GPIO_NUM_NC)
#define GPIO_I2S0_SCLK       (GPIO_NUM_NC)
#define GPIO_I2S0_SDIN       (GPIO_NUM_NC)
#define GPIO_I2S0_DOUT       (GPIO_NUM_NC)

#define RECORD_VOLUME   (27.0)

/**
 * @brief player configurations
 * No speaker connected, but framework requires this
 */
#define PLAYER_VOLUME   (50)

/**
 * @brief Power control not used
 */
#define FUNC_PWR_CTRL       (0)
#define GPIO_PWR_CTRL       (GPIO_NUM_NC)
#define GPIO_PWR_ON_LEVEL   (1)

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan) { \
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000), \
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO), \
        .gpio_cfg = { \
            .mclk = GPIO_I2S_MCLK, \
            .bclk = GPIO_I2S_SCLK, \
            .ws   = GPIO_I2S_LRCK, \
            .dout = GPIO_I2S_DOUT, \
            .din  = GPIO_I2S_SDIN, \
        }, \
    }

#else

#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan) { \
    .mode                   = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX, \
    .sample_rate            = 16000, \
    .bits_per_sample        = I2S_BITS_PER_SAMPLE_32BIT, \
    .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT, \
    .communication_format   = I2S_COMM_FORMAT_STAND_I2S, \
    .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1, \
    .dma_buf_count          = 6, \
    .dma_buf_len            = 160, \
    .use_apll               = false, \
    .tx_desc_auto_clear     = true, \
    .fixed_mclk             = 0, \
    .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT, \
    .bits_per_chan          = I2S_BITS_PER_CHAN_32BIT, \
}

#endif