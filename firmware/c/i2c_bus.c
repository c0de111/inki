#include "i2c_bus.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <stddef.h>
#include <string.h>

void i2c_bus_init(void) {
    gpio_init(DS3231_SDA_PIN);
    gpio_init(DS3231_SCL_PIN);
    gpio_set_function(DS3231_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DS3231_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DS3231_SDA_PIN);
    gpio_pull_up(DS3231_SCL_PIN);
    i2c_init(i2c_default, I2C_FREQ);
}

#define DS3231_ADDR 0x68u
#define DS3231_STATUS_REG 0x0Fu
#define DS3231_TEMP_REG 0x11u

#define BMP581_ADDR_PRIMARY 0x47u
#define BMP581_ADDR_SECONDARY 0x46u
#define BMP581_CHIP_ID_REG 0x01u
#define BMP581_CHIP_ID_VALUE 0x50u

#define RV3028_ADDR 0x52u
#define RV3028_HIDVID_REG 0x28u

#define ST25_ADDR_USER_DYNAMIC 0x53u
#define ST25_ADDR_SYSTEM 0x57u
#define ST25_SYSTEM_IC_REF_REG 0x0017u
#define ST25_IC_REF_DV04KC 0x50u
#define ST25_IC_REF_DV16_64KC 0x51u

static bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
    const int write_rc = i2c_write_blocking(i2c_default, addr, &reg, 1, true);
    if (write_rc < 0) {
        return false;
    }

    const int read_rc = i2c_read_blocking(i2c_default, addr, data, (size_t)len, false);
    if (read_rc < 0) {
        return false;
    }

    return ((size_t)read_rc == len);
}

static bool i2c_read_reg16(uint8_t addr, uint16_t reg, uint8_t *data, size_t len) {
    const uint8_t reg_buf[2] = {
        (uint8_t)((reg >> 8) & 0xFFu),
        (uint8_t)(reg & 0xFFu),
    };

    const int write_rc = i2c_write_blocking(i2c_default, addr, reg_buf, sizeof(reg_buf), true);
    if (write_rc < 0) {
        return false;
    }

    const int read_rc = i2c_read_blocking(i2c_default, addr, data, (size_t)len, false);
    if (read_rc < 0) {
        return false;
    }

    return ((size_t)read_rc == len);
}

/*
 * Address probe for 8-bit register devices.
 * Writes only the register pointer and checks for ACK.
 */
static bool i2c_probe_reg8(uint8_t addr, uint8_t reg) {
    const int write_rc = i2c_write_blocking(i2c_default, addr, &reg, 1, false);
    return (write_rc == 1);
}

/*
 * Address probe for devices using 16-bit register pointers (ST25 system/user areas).
 */
static bool i2c_probe_addr16(uint8_t addr) {
    const uint8_t probe_reg[2] = {0x00u, 0x00u};
    const int write_rc = i2c_write_blocking(i2c_default, addr, probe_reg, sizeof(probe_reg), false);
    return (write_rc == (int)sizeof(probe_reg));
}

void i2c_probe_expected_devices(i2c_probe_result_t *out) {
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->bmp581_addr = 0xFFu;
    out->bmp581_chip_id = 0xFFu;
    out->rv3028_hid = 0xFFu;
    out->rv3028_vid = 0xFFu;
    out->st25_ic_ref = 0xFFu;

    uint8_t value = 0;
    if (i2c_read_reg(DS3231_ADDR, DS3231_STATUS_REG, &value, 1)) {
        out->ds3231_present = true;
        out->ds3231_status_ok = true;
        out->ds3231_status_reg = value;
    }

    uint8_t ds_temp_raw[2] = {0};
    if (i2c_read_reg(DS3231_ADDR, DS3231_TEMP_REG, ds_temp_raw, sizeof(ds_temp_raw))) {
        out->ds3231_present = true;
        out->ds3231_temp_ok = true;
        /* DS3231 temperature is signed quarter-degrees across MSB and top 2 LSB bits. */
        out->ds3231_temp_quarter_c =
            (int16_t)(((int16_t)(int8_t)ds_temp_raw[0] << 2) | (ds_temp_raw[1] >> 6));
    }

    out->bmp581_addr_primary_ack = i2c_probe_reg8(BMP581_ADDR_PRIMARY, BMP581_CHIP_ID_REG);
    out->bmp581_addr_secondary_ack = i2c_probe_reg8(BMP581_ADDR_SECONDARY, BMP581_CHIP_ID_REG);
    out->bmp581_present = out->bmp581_addr_primary_ack || out->bmp581_addr_secondary_ack;

    static const uint8_t bmp581_addresses[] = {
        BMP581_ADDR_PRIMARY,
        BMP581_ADDR_SECONDARY,
    };
    const size_t bmp581_address_count = sizeof(bmp581_addresses) / sizeof(bmp581_addresses[0]);
    for (size_t i = 0; i < bmp581_address_count; i++) {
        const uint8_t addr = bmp581_addresses[i];
        if (!i2c_read_reg(addr, BMP581_CHIP_ID_REG, &value, 1)) {
            continue;
        }
        out->bmp581_present = true;
        out->bmp581_addr = addr;
        out->bmp581_chip_id = value;
        out->bmp581_chip_id_ok = (value == BMP581_CHIP_ID_VALUE);
        break;
    }

    if (out->bmp581_addr == 0xFFu) {
        if (out->bmp581_addr_primary_ack) {
            out->bmp581_addr = BMP581_ADDR_PRIMARY;
        } else if (out->bmp581_addr_secondary_ack) {
            out->bmp581_addr = BMP581_ADDR_SECONDARY;
        }
    }

    out->rv3028_addr_ack = i2c_probe_reg8(RV3028_ADDR, RV3028_HIDVID_REG);
    out->rv3028_present = out->rv3028_addr_ack;

    uint8_t rv3028_id[2] = {0};
    if (i2c_read_reg(RV3028_ADDR, RV3028_HIDVID_REG, rv3028_id, sizeof(rv3028_id))) {
        out->rv3028_present = true;
        out->rv3028_hid = rv3028_id[0];
        out->rv3028_vid = rv3028_id[1];
        out->rv3028_id_ok = !((rv3028_id[0] == 0x00u && rv3028_id[1] == 0x00u) ||
                              (rv3028_id[0] == 0xFFu && rv3028_id[1] == 0xFFu));
    }

    out->st25_user_present = i2c_probe_addr16(ST25_ADDR_USER_DYNAMIC);
    out->st25_system_present = i2c_probe_addr16(ST25_ADDR_SYSTEM);

    if (i2c_read_reg16(ST25_ADDR_SYSTEM, ST25_SYSTEM_IC_REF_REG, &value, 1)) {
        out->st25_system_present = true;
        out->st25_ic_ref = value;
        out->st25_ic_ref_ok = (value == ST25_IC_REF_DV04KC || value == ST25_IC_REF_DV16_64KC);
    }
}
