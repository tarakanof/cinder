#include "j2.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tone.h"

#define DMA_DESC 4
#define DMA_FRAMES 240
#define CHUNK 240
#define WRITE_MS 200

esp_err_t j2_amp_tone(const j2_pins_t *pins, uint32_t hz, uint32_t ms, uint8_t vol_pct)
{
    if (pins->bclk < 0 || pins->lrc < 0 || pins->din < 0 || !j2_pins_valid(pins)) return ESP_ERR_INVALID_ARG;
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num = DMA_DESC;
    cc.dma_frame_num = DMA_FRAMES;
    cc.auto_clear = true;
    esp_err_t err = i2s_new_channel(&cc, &tx, NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(J2_AMP_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = pins->bclk, .ws = pins->lrc, .dout = pins->din, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(tx, &sc);
    if (err == ESP_OK) err = i2s_channel_enable(tx);
    if (err == ESP_OK) {
        tone_t t;
        int16_t buf[CHUNK];
        size_t n;
        tone_init(&t, J2_AMP_RATE, hz, ms, vol_pct);
        while (err == ESP_OK && (n = tone_fill(&t, buf, CHUNK)) > 0) {
            size_t done = 0;
            err = i2s_channel_write(tx, buf, n * sizeof buf[0], &done, WRITE_MS);
        }
        vTaskDelay(pdMS_TO_TICKS(DMA_DESC * DMA_FRAMES * 1000 / J2_AMP_RATE + 10));
        i2s_channel_disable(tx);
    }
    i2s_del_channel(tx);
    return err;
}
