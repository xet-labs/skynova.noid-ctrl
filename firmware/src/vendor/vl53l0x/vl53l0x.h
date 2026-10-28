/*
 * vl53l0x.h - VL53L0X time-of-flight driver, VENDORED (see VENDOR.md in this folder).
 * Local change from the original: no I2C driver install, no port or pin arguments; the bus belongs to hal_sense.c.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VL53L0X_DEFAULT_ADDR 0x29

typedef struct {
    uint8_t addr;
    uint8_t stop_variable;
} vl53l0x_t;

/* Check the sensor id, then run the ST init + calibration sequence. Takes about 100 to 300 ms. The I2C bus must
 * already be running (hal_i2c_init). Returns ESP_ERR_NOT_FOUND when register 0xC0 is not 0xEE (or nothing answers). */
esp_err_t vl53l0x_init(vl53l0x_t *dev);

/* One blocking range measurement in mm. Values >= 8190 mean no target / out of range. */
esp_err_t vl53l0x_read_mm(vl53l0x_t *dev, uint16_t *mm);

#ifdef __cplusplus
}
#endif
