/**
 * Custom board implementation for ESP32-S3 with INMP441 I2S microphone
 */
#include "driver/i2c.h"

#include "string.h"
#include "bsp_board.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#else
#include "driver/i2s.h"
#endif
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define ADC_I2S_CHANNEL 2
static const char *TAG = "board";

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
#endif

esp_err_t bsp_i2c_init(i2c_port_t i2c_num, uint32_t clk_speed) { return ESP_OK; }
esp_err_t bsp_codec_adc_init(int sample_rate) { return ESP_OK; }
esp_err_t bsp_codec_dac_init(int sample_rate, int channel_format, int bits_per_chan) { return ESP_OK; }
esp_err_t bsp_audio_set_play_vol(int volume) { return ESP_OK; }
esp_err_t bsp_audio_get_play_vol(int *volume) { *volume = 0; return ESP_OK; }

static esp_err_t bsp_i2s_init(i2s_port_t i2s_num, uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    esp_err_t ret_val = ESP_OK;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_slot_mode_t channel_fmt = I2S_SLOT_MODE_STEREO;
    if (channel_format == 1) channel_fmt = I2S_SLOT_MODE_MONO;
    if (bits_per_chan != 16 && bits_per_chan != 32) bits_per_chan = 32;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, I2S_ROLE_MASTER);
    ret_val |= i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan);
    ret_val |= i2s_channel_init_std_mode(tx_handle, &std_cfg);
    ret_val |= i2s_channel_init_std_mode(rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(tx_handle);
    ret_val |= i2s_channel_enable(rx_handle);
#else
    i2s_channel_fmt_t channel_fmt = I2S_CHANNEL_FMT_RIGHT_LEFT;
    if (channel_format == 1) channel_fmt = I2S_CHANNEL_FMT_ONLY_LEFT;
    if (bits_per_chan != 16 && bits_per_chan != 32) bits_per_chan = 16;

    i2s_config_t i2s_config = I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan);
    i2s_pin_config_t pin_config = {
        .bck_io_num = GPIO_I2S_SCLK,
        .ws_io_num = GPIO_I2S_LRCK,
        .data_out_num = GPIO_I2S_DOUT,
        .data_in_num = GPIO_I2S_SDIN,
        .mck_io_num = GPIO_I2S_MCLK,
    };
    ret_val |= i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
    ret_val |= i2s_set_pin(i2s_num, &pin_config);
#endif
    return ret_val;
}

esp_err_t bsp_audio_play(const int16_t* data, int length, TickType_t ticks_to_wait) { return ESP_OK; }

esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;
    size_t bytes_read = 0;

    int num_samples = buffer_len / sizeof(int16_t);
    size_t read_bytes_32 = num_samples * sizeof(int32_t); // 32-bit I2S read size

    static int32_t *i2s_32bit_buf = NULL;
    static size_t i2s_32bit_buf_bytes = 0;

    if (i2s_32bit_buf_bytes < read_bytes_32) {
        if (i2s_32bit_buf) {
            free(i2s_32bit_buf);
        }
        i2s_32bit_buf = (int32_t *)malloc(read_bytes_32);
        if (i2s_32bit_buf) {
            i2s_32bit_buf_bytes = read_bytes_32;
        } else {
            i2s_32bit_buf_bytes = 0;
            return ESP_ERR_NO_MEM;
        }
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = i2s_channel_read(rx_handle, (void *)i2s_32bit_buf, read_bytes_32, &bytes_read, portMAX_DELAY);
#else
    ret = i2s_read(I2S_NUM_1, (void *)i2s_32bit_buf, read_bytes_32, &bytes_read, portMAX_DELAY);
#endif

    if (ret == ESP_OK && bytes_read > 0) {
        int samples_read = bytes_read / sizeof(int32_t);
        for (int i = 0; i < samples_read && i < num_samples; i++) {
            // INMP441 outputs 24-bit audio inside a 32-bit slot.
            // Shift right by 14 bits to convert to 16-bit PCM with clear gain scaling.
            int32_t sample = i2s_32bit_buf[i] >> 14;
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            buffer[i] = (int16_t)sample;

        }
    }
    return ret;
}


int bsp_get_feed_channel(void) { return ADC_I2S_CHANNEL; }
char* bsp_get_input_format(void) { return "MR"; }

esp_err_t bsp_board_init(uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    ESP_LOGI(TAG, "Initializing INMP441 I2S microphone");
    ESP_LOGI(TAG, "  SCK (BCLK) -> GPIO %d", GPIO_I2S_SCLK);
    ESP_LOGI(TAG, "  WS  (LRCK) -> GPIO %d", GPIO_I2S_LRCK);
    ESP_LOGI(TAG, "  SD  (DIN)  -> GPIO %d", GPIO_I2S_SDIN);
    bsp_i2s_init(I2S_NUM_1, 16000, 2, 32);
    return ESP_OK;
}

esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bsp_sdcard_deinit(char *mount_point) { return ESP_ERR_NOT_SUPPORTED; }
