/*
 * hal_sense.c - ADC and I2C drivers: IR sensors, motor current, INA219 battery voltage.
 *
 * Purpose : turn pins and buses into numbers. No decisions here (conventions: drivers do no thinking).
 * Called by: base.c (safety and power tasks), later the console (i2c scan) and the device registry.
 * API     : hal.h. Written for ESP-IDF 6.x: esp_adc/adc_oneshot.h and driver/i2c_master.h.
 *           The legacy adc and i2c drivers are gone or end-of-life in 6.x, so they are not used.
 * Test    : none on the PC (hardware). On the board: console "ir" / "batt" once base.c is running,
 *           expect four IR numbers in 0..4095 and a battery voltage close to a multimeter reading.
 * Spec    : vault 04-firmware/02-base-node.md (Cliff protection, Power monitor), pins in board_pins.h.
 */
#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "board/board_pins.h"
#include "hal.h"

static const char *TAG = "hal_sense";

#define I2C_TIMEOUT_MS 50
#define I2C_SPEED_HZ   400000
#define INA219_REG_BUS_VOLTAGE 0x02

static adc_oneshot_unit_handle_t s_adc;
static bool s_adc_ready;

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ina;
static SemaphoreHandle_t s_i2c_lock;      /* one transaction at a time on bus 0 (OLED pages later release it between pages) */

/* ---- ADC1 ------------------------------------------------------------------ */
void hal_adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc unit init failed");
        return;
    }
    /* 12 dB attenuation = about 0 to 3.1 V full scale on ESP32 (the IR modules swing up to 3.3 V: #verify
     * with a multimeter; a reading stuck near 4095 is reported as a sensor fault by the cliff logic). */
    const adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    static const int channels[] = { ADC_CH_IR_FRONT_LEFT, ADC_CH_IR_FRONT_RIGHT, ADC_CH_IR_REAR_LEFT,
                                    ADC_CH_IR_REAR_RIGHT, ADC_CH_MOTOR_IS };
    for (size_t i = 0; i < sizeof channels / sizeof channels[0]; i++) {
        if (adc_oneshot_config_channel(s_adc, (adc_channel_t)channels[i], &chan) != ESP_OK) {
            ESP_LOGE(TAG, "adc channel %d config failed", channels[i]);
            return;
        }
    }
    s_adc_ready = true;
}

int hal_adc_read(int adc1_channel)
{
    int raw = 0;
    if (!s_adc_ready || adc_oneshot_read(s_adc, (adc_channel_t)adc1_channel, &raw) != ESP_OK) { return -1; }
    return raw;
}

/* ---- I2C bus 0 (GPIO21 SDA, GPIO22 SCL) ----------------------------------------- */
void hal_i2c_init(void)
{
    s_i2c_lock = xSemaphoreCreateMutex();
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,     /* modules usually carry pull-ups too; harmless, #verify on the scope */
    };
    if (s_i2c_lock == NULL || i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        s_bus = NULL;
        return;
    }
    const i2c_device_config_t ina = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SKYNOID_INA219_ADDR,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &ina, &s_ina) != ESP_OK) {
        ESP_LOGW(TAG, "ina219 handle not created");
        s_ina = NULL;
    }
}

bool hal_i2c_probe(uint8_t addr)
{
    if (s_bus == NULL || xSemaphoreTake(s_i2c_lock, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) != pdTRUE) { return false; }
    const bool ack = (i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS) == ESP_OK);
    xSemaphoreGive(s_i2c_lock);
    return ack;
}

bool hal_ina219_read_mv(int32_t *mv)
{
    if (s_ina == NULL || xSemaphoreTake(s_i2c_lock, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) != pdTRUE) { return false; }
    const uint8_t reg = INA219_REG_BUS_VOLTAGE;
    uint8_t buf[2] = { 0, 0 };
    const esp_err_t err = i2c_master_transmit_receive(s_ina, &reg, 1, buf, 2, I2C_TIMEOUT_MS);
    xSemaphoreGive(s_i2c_lock);
    if (err != ESP_OK) { return false; }
    const uint16_t raw = (uint16_t)((buf[0] << 8) | buf[1]);
    *mv = (int32_t)(raw >> 3) * 4;                /* bits 15..3 hold the voltage, LSB = 4 mV (datasheet, #verify) */
    return true;
}
