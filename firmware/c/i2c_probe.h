#ifndef I2C_PROBE_H
#define I2C_PROBE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct i2c_probe_result_t {
    bool ds3231_present;
    bool ds3231_status_ok;
    uint8_t ds3231_status_reg;
    bool ds3231_temp_ok;
    int16_t ds3231_temp_quarter_c;

    bool bmp581_present;
    uint8_t bmp581_addr;
    uint8_t bmp581_chip_id;
    bool bmp581_chip_id_ok;

    bool rv3028_present;
    uint8_t rv3028_hid;
    uint8_t rv3028_vid;
    bool rv3028_id_ok;

    bool st25_user_present;
    bool st25_system_present;
    uint8_t st25_ic_ref;
    bool st25_ic_ref_ok;
} i2c_probe_result_t;

void i2c_probe_expected_devices(i2c_probe_result_t *out);

#endif // I2C_PROBE_H
