#include "j2.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

const int J2_PROBE_GPIOS[] = {43, 44, 38, 39, 40, 18};
const size_t J2_PROBE_N = sizeof J2_PROBE_GPIOS / sizeof J2_PROBE_GPIOS[0];

void j2_pins_default(j2_pins_t *p)
{
    *p = (j2_pins_t){
        .sda = CONFIG_CINDER_J2_I2C_SDA,
        .scl = CONFIG_CINDER_J2_I2C_SCL,
        .bclk = CONFIG_CINDER_J2_I2S_BCLK,
        .lrc = CONFIG_CINDER_J2_I2S_LRC,
        .din = CONFIG_CINDER_J2_I2S_DIN,
        .radar_rx = CONFIG_CINDER_J2_RADAR_RX,
        .radar_bit = CONFIG_CINDER_J2_RADAR_BIT,
        .pcf_addr = CONFIG_CINDER_J2_PCF_ADDR,
        .presence_off_ms = CONFIG_CINDER_J2_PRESENCE_OFF_MS,
    };
}

bool j2_probe_allowed(int gpio)
{
    for (size_t i = 0; i < J2_PROBE_N; i++)
        if (J2_PROBE_GPIOS[i] == gpio) return true;
    return false;
}

esp_err_t j2_probe_drive(int gpio, bool high)
{
    if (!j2_probe_allowed(gpio)) return ESP_ERR_INVALID_ARG;
    gpio_reset_pin(gpio);
    esp_err_t err = gpio_set_level(gpio, high);
    if (err == ESP_OK) err = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    return err;
}

static bool unset_or_allowed(int gpio) { return gpio == -1 || j2_probe_allowed(gpio); }

bool j2_pins_valid(const j2_pins_t *p)
{
    const int bus[5] = {p->sda, p->scl, p->bclk, p->lrc, p->din};
    for (int i = 0; i < 5; i++) {
        if (!unset_or_allowed(bus[i])) return false;
        for (int k = 0; k < i; k++)
            if (bus[i] >= 0 && bus[i] == bus[k]) return false;
    }
    for (int i = 0; i < 5; i++)
        if (p->radar_rx >= 0 && p->radar_rx == bus[i]) return false;
    return unset_or_allowed(p->radar_rx) && p->radar_bit >= 0 && p->radar_bit < 8;
}

esp_err_t j2_probe_float(int gpio)
{
    if (!j2_probe_allowed(gpio)) return ESP_ERR_INVALID_ARG;
    return gpio_reset_pin(gpio);
}

int j2_probe_level(int gpio)
{
    if (!j2_probe_allowed(gpio)) return -1;
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(50);
    return gpio_get_level(gpio);
}
