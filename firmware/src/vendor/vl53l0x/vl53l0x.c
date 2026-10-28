/*
 * Minimal VL53L0X driver for ESP-IDF (plain C).
 * Init sequence follows ST's reference API as condensed in Pololu's vl53l0x-arduino.
 *
 * VENDORED from /mnt/xlvd/@datx/@projects/@-platformIO/esp-tmp/components/vl53l0x/vl53l0x.c (the user's own file,
 * 2026-10-26). The init sequence, the SPAD code and the `tuning` table below are copied UNCHANGED. Local changes
 * (also listed in VENDOR.md): the legacy `driver/i2c.h` transfers are replaced by hal_i2c_read_reg/hal_i2c_write_reg
 * (the bus is owned by hal_sense.c), and the I2C driver install at the top of vl53l0x_init is removed.
 */
#include <stdbool.h>
#include "vl53l0x.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "hal.h"

#define REG_SYSRANGE_START              0x00
#define REG_SYSTEM_SEQUENCE_CONFIG      0x01
#define REG_SYSTEM_INTERRUPT_CONFIG     0x0A
#define REG_SYSTEM_INTERRUPT_CLEAR      0x0B
#define REG_RESULT_INTERRUPT_STATUS     0x13
#define REG_RESULT_RANGE_MM             0x1E   /* RESULT_RANGE_STATUS (0x14) + 10 */
#define REG_MSRC_CONFIG_CONTROL         0x60
#define REG_MIN_COUNT_RATE_RTN_LIMIT    0x44
#define REG_DYNAMIC_SPAD_NUM_REQUESTED  0x4E
#define REG_DYNAMIC_SPAD_REF_EN_OFFSET  0x4F
#define REG_GLOBAL_SPAD_ENABLES_REF_0   0xB0
#define REG_GLOBAL_REF_EN_START_SELECT  0xB6
#define REG_GPIO_HV_MUX_ACTIVE_HIGH     0x84
#define REG_VHV_CONFIG_PAD_SCL_SDA_EXT  0x89
#define REG_IDENTIFICATION_MODEL_ID     0xC0

/* ---- I2C primitives: the only part rewritten for this project (hal_sense.c owns the bus) ---- */
static esp_err_t wr8(vl53l0x_t *d, uint8_t reg, uint8_t val)
{
    return hal_i2c_write_reg(d->addr, reg, &val, 1) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wr16(vl53l0x_t *d, uint8_t reg, uint16_t val)
{
    const uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)val };
    return hal_i2c_write_reg(d->addr, reg, b, 2) ? ESP_OK : ESP_FAIL;
}

static esp_err_t rd8(vl53l0x_t *d, uint8_t reg, uint8_t *val)
{
    return hal_i2c_read_reg(d->addr, reg, val, 1) ? ESP_OK : ESP_FAIL;
}

static esp_err_t rd16(vl53l0x_t *d, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (!hal_i2c_read_reg(d->addr, reg, b, 2)) return ESP_FAIL;
    *val = ((uint16_t)b[0] << 8) | b[1];
    return ESP_OK;
}

static esp_err_t rd_multi(vl53l0x_t *d, uint8_t reg, uint8_t *buf, size_t len)
{
    return hal_i2c_read_reg(d->addr, reg, buf, len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wr_multi(vl53l0x_t *d, uint8_t reg, const uint8_t *src, size_t len)
{
    if (len > 6) return ESP_ERR_INVALID_SIZE;
    return hal_i2c_write_reg(d->addr, reg, src, len) ? ESP_OK : ESP_FAIL;
}

#define CHECK(x) do { esp_err_t _e = (x); if (_e != ESP_OK) return _e; } while (0)

/* Poll reg until (value & mask) is non-zero (expect_nonzero) or zero (!expect_nonzero). */
static esp_err_t wait_reg(vl53l0x_t *d, uint8_t reg, uint8_t mask, bool expect_nonzero, uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time();
    uint8_t v;
    for (;;) {
        CHECK(rd8(d, reg, &v));
        bool nonzero = (v & mask) != 0;
        if (nonzero == expect_nonzero) return ESP_OK;
        if ((esp_timer_get_time() - start) > (int64_t)timeout_ms * 1000) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}

static esp_err_t get_spad_info(vl53l0x_t *d, uint8_t *count, bool *is_aperture)
{
    uint8_t tmp;
    CHECK(wr8(d, 0x80, 0x01)); CHECK(wr8(d, 0xFF, 0x01)); CHECK(wr8(d, 0x00, 0x00));
    CHECK(wr8(d, 0xFF, 0x06));
    CHECK(rd8(d, 0x83, &tmp)); CHECK(wr8(d, 0x83, tmp | 0x04));
    CHECK(wr8(d, 0xFF, 0x07)); CHECK(wr8(d, 0x81, 0x01)); CHECK(wr8(d, 0x80, 0x01));
    CHECK(wr8(d, 0x94, 0x6B)); CHECK(wr8(d, 0x83, 0x00));
    CHECK(wait_reg(d, 0x83, 0xFF, true, 500));
    CHECK(wr8(d, 0x83, 0x01));
    CHECK(rd8(d, 0x92, &tmp));
    *count = tmp & 0x7F;
    *is_aperture = (tmp >> 7) & 0x01;
    CHECK(wr8(d, 0x81, 0x00)); CHECK(wr8(d, 0xFF, 0x06));
    CHECK(rd8(d, 0x83, &tmp)); CHECK(wr8(d, 0x83, tmp & ~0x04));
    CHECK(wr8(d, 0xFF, 0x01)); CHECK(wr8(d, 0x00, 0x01));
    CHECK(wr8(d, 0xFF, 0x00)); CHECK(wr8(d, 0x80, 0x00));
    return ESP_OK;
}

static esp_err_t single_ref_calibration(vl53l0x_t *d, uint8_t vhv_init_byte)
{
    CHECK(wr8(d, REG_SYSRANGE_START, 0x01 | vhv_init_byte));
    CHECK(wait_reg(d, REG_RESULT_INTERRUPT_STATUS, 0x07, true, 500));
    CHECK(wr8(d, REG_SYSTEM_INTERRUPT_CLEAR, 0x01));
    CHECK(wr8(d, REG_SYSRANGE_START, 0x00));
    return ESP_OK;
}

static const uint8_t tuning[][2] = {
    {0xFF,0x01},{0x00,0x00},{0xFF,0x00},{0x09,0x00},{0x10,0x00},{0x11,0x00},
    {0x24,0x01},{0x25,0xFF},{0x75,0x00},{0xFF,0x01},{0x4E,0x2C},{0x48,0x00},
    {0x30,0x20},{0xFF,0x00},{0x30,0x09},{0x54,0x00},{0x31,0x04},{0x32,0x03},
    {0x40,0x83},{0x46,0x25},{0x60,0x00},{0x27,0x00},{0x50,0x06},{0x51,0x00},
    {0x52,0x96},{0x56,0x08},{0x57,0x30},{0x61,0x00},{0x62,0x00},{0x64,0x00},
    {0x65,0x00},{0x66,0xA0},{0xFF,0x01},{0x22,0x32},{0x47,0x14},{0x49,0xFF},
    {0x4A,0x00},{0xFF,0x00},{0x7A,0x0A},{0x7B,0x00},{0x78,0x21},{0xFF,0x01},
    {0x23,0x34},{0x42,0x00},{0x44,0xFF},{0x45,0x26},{0x46,0x05},{0x40,0x40},
    {0x0E,0x06},{0x20,0x1A},{0x43,0x40},{0xFF,0x00},{0x34,0x03},{0x35,0x44},
    {0xFF,0x01},{0x31,0x04},{0x4B,0x09},{0x4C,0x05},{0x4D,0x04},{0xFF,0x00},
    {0x44,0x00},{0x45,0x20},{0x47,0x08},{0x48,0x28},{0x67,0x00},{0x70,0x04},
    {0x71,0x01},{0x72,0xFE},{0x76,0x00},{0x77,0x00},{0xFF,0x01},{0x0D,0x01},
    {0xFF,0x00},{0x80,0x01},{0x01,0xF8},{0xFF,0x01},{0x8E,0x01},{0x00,0x01},
    {0xFF,0x00},{0x80,0x00},
};

esp_err_t vl53l0x_init(vl53l0x_t *d)
{
    d->addr = VL53L0X_DEFAULT_ADDR;

    uint8_t id = 0, tmp;
    CHECK(rd8(d, REG_IDENTIFICATION_MODEL_ID, &id));
    if (id != 0xEE) return ESP_ERR_NOT_FOUND;

    /* 2V8 I/O mode (default on most breakout boards) */
    CHECK(rd8(d, REG_VHV_CONFIG_PAD_SCL_SDA_EXT, &tmp));
    CHECK(wr8(d, REG_VHV_CONFIG_PAD_SCL_SDA_EXT, tmp | 0x01));

    /* Standard mode */
    CHECK(wr8(d, 0x88, 0x00));
    CHECK(wr8(d, 0x80, 0x01)); CHECK(wr8(d, 0xFF, 0x01)); CHECK(wr8(d, 0x00, 0x00));
    CHECK(rd8(d, 0x91, &d->stop_variable));
    CHECK(wr8(d, 0x00, 0x01)); CHECK(wr8(d, 0xFF, 0x00)); CHECK(wr8(d, 0x80, 0x00));

    /* Disable SIGNAL_RATE_MSRC and SIGNAL_RATE_PRE_RANGE limit checks */
    CHECK(rd8(d, REG_MSRC_CONFIG_CONTROL, &tmp));
    CHECK(wr8(d, REG_MSRC_CONFIG_CONTROL, tmp | 0x12));
    /* Signal rate limit 0.25 MCPS (fixed point 9.7) */
    CHECK(wr16(d, REG_MIN_COUNT_RATE_RTN_LIMIT, 32));
    CHECK(wr8(d, REG_SYSTEM_SEQUENCE_CONFIG, 0xFF));

    /* SPAD setup */
    uint8_t spad_count; bool spad_aperture;
    CHECK(get_spad_info(d, &spad_count, &spad_aperture));

    uint8_t ref_spad_map[6];
    CHECK(rd_multi(d, REG_GLOBAL_SPAD_ENABLES_REF_0, ref_spad_map, 6));
    CHECK(wr8(d, 0xFF, 0x01));
    CHECK(wr8(d, REG_DYNAMIC_SPAD_REF_EN_OFFSET, 0x00));
    CHECK(wr8(d, REG_DYNAMIC_SPAD_NUM_REQUESTED, 0x2C));
    CHECK(wr8(d, 0xFF, 0x00));
    CHECK(wr8(d, REG_GLOBAL_REF_EN_START_SELECT, 0xB4));

    uint8_t first_spad = spad_aperture ? 12 : 0;
    uint8_t enabled = 0;
    for (uint8_t i = 0; i < 48; i++) {
        if (i < first_spad || enabled == spad_count) {
            ref_spad_map[i / 8] &= ~(1 << (i % 8));
        } else if ((ref_spad_map[i / 8] >> (i % 8)) & 0x01) {
            enabled++;
        }
    }
    CHECK(wr_multi(d, REG_GLOBAL_SPAD_ENABLES_REF_0, ref_spad_map, 6));

    /* Default tuning settings */
    for (size_t i = 0; i < sizeof(tuning) / sizeof(tuning[0]); i++) {
        CHECK(wr8(d, tuning[i][0], tuning[i][1]));
    }

    /* Interrupt: new sample ready, active low */
    CHECK(wr8(d, REG_SYSTEM_INTERRUPT_CONFIG, 0x04));
    CHECK(rd8(d, REG_GPIO_HV_MUX_ACTIVE_HIGH, &tmp));
    CHECK(wr8(d, REG_GPIO_HV_MUX_ACTIVE_HIGH, tmp & ~0x10));
    CHECK(wr8(d, REG_SYSTEM_INTERRUPT_CLEAR, 0x01));

    CHECK(wr8(d, REG_SYSTEM_SEQUENCE_CONFIG, 0xE8));

    /* Reference calibration */
    CHECK(wr8(d, REG_SYSTEM_SEQUENCE_CONFIG, 0x01));
    CHECK(single_ref_calibration(d, 0x40));
    CHECK(wr8(d, REG_SYSTEM_SEQUENCE_CONFIG, 0x02));
    CHECK(single_ref_calibration(d, 0x00));
    CHECK(wr8(d, REG_SYSTEM_SEQUENCE_CONFIG, 0xE8));

    return ESP_OK;
}

esp_err_t vl53l0x_read_mm(vl53l0x_t *d, uint16_t *mm)
{
    CHECK(wr8(d, 0x80, 0x01)); CHECK(wr8(d, 0xFF, 0x01)); CHECK(wr8(d, 0x00, 0x00));
    CHECK(wr8(d, 0x91, d->stop_variable));
    CHECK(wr8(d, 0x00, 0x01)); CHECK(wr8(d, 0xFF, 0x00)); CHECK(wr8(d, 0x80, 0x00));

    CHECK(wr8(d, REG_SYSRANGE_START, 0x01));
    CHECK(wait_reg(d, REG_SYSRANGE_START, 0x01, false, 500));          /* start bit cleared */
    CHECK(wait_reg(d, REG_RESULT_INTERRUPT_STATUS, 0x07, true, 500));  /* result ready */
    CHECK(rd16(d, REG_RESULT_RANGE_MM, mm));
    CHECK(wr8(d, REG_SYSTEM_INTERRUPT_CLEAR, 0x01));
    return ESP_OK;
}
