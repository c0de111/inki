/**
 * @file at24c32.c
 * @author  Alper Tunga Güven (alperguven@std.iyte.edu.tr)
 * @brief   A driver library for the onboard AT24C32 EEPROM inside DS3231 module written for Raspberry Pi Pico.
 * Datasheet Link: http://ww1.microchip.com/downloads/en/DeviceDoc/doc0336.pdf
 * @version 0.1
 * @date    2023-08-12
 * 
 * @copyright Copyright (c) 2023
 * 
 */

 #include "ds3231.h"

/**
 * @brief                   Library function to write to a page of an I2C EEPROM.
 * Maximum of 32 bytes can be written to a single page.
 * 
 * @param[in] i2c           I2C instance used.
 * @param[in] dev_addr      Adress of the I2C device.
 * @param[in] page_addr     Register adress to be written.
 * @param[in] starting_byte Which byte the page write must start from. Max value = 31;
 * @param[in] length        Length of the data to be written in bytes.
 * @param[in] data          Pointer to the data buffer.
 * @return                  0 if succesful, -1 if i2c failure.
 */
int at24c32_i2c_write_page(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t page_addr, uint8_t starting_byte, size_t length, uint8_t * data)
{
    if(!length)
        return -1;
    if(starting_byte >= AT24C32_PAGE_SIZE)
        return -1;
    uint8_t messeage[length + 2];
    messeage[0] = page_addr;
    messeage[1] = starting_byte;
    for(int i = 0; i < length; i++) {
        messeage[i + 2] = data[i];
    }
    if(i2c_write_blocking(i2c, dev_addr, messeage, (length + 2), false) == PICO_ERROR_GENERIC)
        return -1;
    return 0;
}

/**
 * @brief                   Library function to read from specific I2C EEPROM page.
 * 
 * @param[in] i2c           I2C instance used.
 * @param[in] dev_addr      Adress of the I2C device.
 * @param[in] page_addr     Register adress to be written.
 * @param[in] starting_byte Which byte the page read must start from. Max value = 31;
 * @param[in] length        Length of the data to be written in bytes.
 * @param[out] data         Pointer to the data buffer.
 * @return                  0 if succesful, -1 if i2c failure.
 */
int at24c32_i2c_read_page(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t page_addr, uint8_t starting_byte, size_t length, uint8_t * data)
{
    if(!length)
        return -1;
    uint8_t messeage[2];
    messeage[0] = page_addr;
    messeage[1] = starting_byte;
    if(i2c_write_blocking(i2c, dev_addr, messeage, 2, true) == PICO_ERROR_GENERIC)
        return -1;
    if(i2c_read_blocking(i2c, dev_addr, data, length, false) == PICO_ERROR_GENERIC)
        return -1;
    
    return 0;
}

/**
 * @brief               Read from the last written adress in EEPROM. 
 * The internal data word address counter maintains the last address 
 * accessed during the last read or write operation, incremented by one. This address
 * stays valid between operations as long as the chip power is maintained. 
 * 
 * @param[in] i2c       I2C instance used.  
 * @param[in] dev_addr  Device adress.
 * @param[in] length    Length of the data to be read.
 * @param[out] data     
 * @return int 
 */
int at24c32_read_current_adress(i2c_inst_t * i2c, uint8_t dev_addr,
    size_t length, uint8_t * data) 
{
    if(!length)
        return -1;
    if(i2c_read_blocking(i2c, dev_addr, data, length, false) == PICO_ERROR_GENERIC)
        return -1;
    return 0;
}

/**
 * @brief                   Write the current time stored in DS3231 module to the AT24C32 EEPROM.
 * 
 * @param[in] rtc           DS3231 struct.
 * @param[in] page_addr     Page adress to start writing from.
 * @return                  0 if succesful.
 */
int at24c32_write_current_time(ds3231_t * rtc, uint8_t page_addr) {
    ds3231_data_t current_time;
    if(ds3231_read_current_time(rtc, &current_time)) {
        return -1;
    }
    uint8_t data[8] = {0};
    if(rtc->am_pm_mode) {
        data[0] = current_time.seconds;
        data[1] = current_time.minutes;
        data[2] = current_time.hours;
        data[3] = current_time.am_pm;
        data[4] = current_time.day;
        data[5] = current_time.date;
        data[6] = current_time.month;
        data[7] = current_time.year;
    } else {
        data[0] = current_time.seconds;
        data[1] = current_time.minutes;
        data[2] = current_time.hours;
        data[3] = current_time.day;
        data[4] = current_time.date;
        data[5] = current_time.month;
        data[6] = current_time.year;
        data[7] = current_time.century;
    }
    if(at24c32_i2c_write_page(rtc->i2c, rtc->at24c32_addr, page_addr, 0, 8, data))
        return -1;
    return 0;
}

