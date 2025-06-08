/**
 * @file ds3231.c
 * @author  Alper Tunga Güven (alperguven@std.iyte.edu.tr)
 * @brief   A driver library for DS3231 written for Raspberry Pi Pico.
 * Datasheet Link: https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf
 * @version 0.1
 * @date    2023-08-12
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "ds3231.h"

/**
 * @brief               Library function to read a specific I2C register adress.
 * 
 * @param[in] i2c       I2C instance used.
 * @param[in] dev_addr  Adress of the I2C device.
 * @param[in] reg_addr  Register adress to be read.
 * @param[in] length    length of the data in bytes to be read.
 * @param[out] data     Buffer to store the read data.
 * @return              0 if succesful, -1 if i2c failure.    
 */
int i2c_read_reg(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t reg_addr, size_t length, uint8_t * data) 
{
    if(!length) 
        return -1;
    uint8_t reg = reg_addr; 
    if(i2c_write_blocking(i2c, dev_addr, &reg, 1, true) == PICO_ERROR_GENERIC) {
        return -1;
    }
    if(i2c_read_blocking(i2c, dev_addr, data, length, false) == PICO_ERROR_GENERIC) {
        return -1;
    }
    return 0;
}

/**
 * @brief               Library function to write to a specific I2C register adress.
 * 
 * @param[in] i2c       I2C instance used.
 * @param[in] dev_addr  Adress of the I2C device.
 * @param[in] reg_addr  Register adress to be written.
 * @param[in] length    Length of the data to be written in bytes.
 * @param[in] data      Pointer to the data buffer.
 * @return              0 if succesful, -1 if i2c failure.
 */
int i2c_write_reg(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t reg_addr, size_t length, uint8_t * data)
{
    if(!length) 
        return -1;
    uint8_t messeage[length + 1];
    messeage[0] = reg_addr;
    for(int i = 0; i < length; i++) {
        messeage[i + 1] = data[i];
    }
    if(i2c_write_blocking(i2c, dev_addr, messeage, (length + 1), false) == PICO_ERROR_GENERIC)
        return -1;
    return 0;
}

/**
 * @brief           Library function that takes an 8 bit unsigned integer and converts into
 *  Binary Coded Decimal number that can be written to DS3231 registers.
 * 
 * @param[in] data  Number to be converted.
 * @return          Number in BCD form.
 */
uint8_t bin_to_bcd(const uint8_t data) {
    uint8_t ones_digit = (uint8_t)(data % 10);
    uint8_t twos_digit = (uint8_t)(data - ones_digit) / 10;
    return ((twos_digit << 4) + ones_digit);
}

/**
 * @brief           Library function that takes an 8 bit unsigned integer and converts it into
 * Binary Coded Decimal number where bit 5 and 6 represent the AM/PM characteristics.
 * 
 * @param[in] data  Number to converted.
 * @return          Number in BCD form with AM/PM bits.
 */
uint8_t bin_to_bcd_am_pm(uint8_t data) {
    uint8_t temp = bin_to_bcd(data);
    uint8_t am_pm = 0x00;
    if(data > 12) {
        am_pm = 0x01;
        data -= 12;
    }
    temp |= (am_pm << 5);
    return temp;
}

/**
 * @brief                   Initiliaze ds3231 struct and specify which I2C instance is going to be used.
 * 
 * @param[out] rtc          Pointer to the ds3231 struct.
 * @param[in] i2c           Chosen I2C instance.
 * @param[in] dev_addr      DS3231 device adress. Leave 0 for default adress.
 * @param[in] eeprom_addr   EEPROM device adress. Leave 0 for default adress. IF an incorrect adress is entered, it is set to the default adress.
 * @return                  0 if succesful. 
 */
int ds3231_init(ds3231_t * rtc, i2c_inst_t * i2c, uint8_t dev_addr, uint8_t eeprom_addr) {
    rtc->am_pm_mode = false;
    rtc->i2c = i2c;
    if(dev_addr)
        rtc->ds3231_addr = dev_addr;
    else 
        rtc->ds3231_addr = DS3231_DEVICE_ADRESS;

    switch (eeprom_addr)
    {
    case AT24C32_EEPROM_ADRESS_0:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_0;
        break;
    case AT24C32_EEPROM_ADRESS_1:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_1;    
        break;
    case AT24C32_EEPROM_ADRESS_2:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_2;
        break;
    case AT24C32_EEPROM_ADRESS_3:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_3;
        break;
    case AT24C32_EEPROM_ADRESS_4:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_4;
        break;
    case AT24C32_EEPROM_ADRESS_5:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_5;
        break;
    case AT24C32_EEPROM_ADRESS_6:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_6;
        break;
    case AT24C32_EEPROM_ADRESS_7:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_7;
        break;
    default:
        rtc->at24c32_addr = AT24C32_EEPROM_ADRESS_0;
        break;
    }
    return 0;
}

/**
 * @brief               Enable or disable AM/PM mode of DS3231. By default, it is disabled
 * 
 * @param[in] rtc       DS3231 struct.           
 * @param[in] enable    If true, AM_PM mode is enabled. If false, 24-Hour mode is enabled.
 * @return              0 if succeful.
 */
int ds3231_enable_am_pm_mode(ds3231_t * rtc, bool enable) {
    uint8_t temp = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_HOURS_REG, 1, &temp))
        return -1;
    if(enable) {
        temp |= (0x01 << 6);
        rtc->am_pm_mode = true;
    } else {
        rtc->am_pm_mode = false;
        temp &= ~(0x01 << 6);
    }
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_HOURS_REG, 1, &temp))
        return -1;
    return 0;
}

/**
 * @brief               Configure the current time in DS3231.
 * 
 * @param[in] rtc       DS3231 struct.
 * @param[in] data      Data struct that holds the time that will be set in DS3231.
 * @return              0 if succesful.
 */
int ds3231_configure_time(ds3231_t * rtc, ds3231_data_t * data) {
    uint8_t temp[7] = {0, 0, 0, 0, 0, 0, 0};
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_SECONDS_REG, 7, temp)) 
        return -1;
    
    /* Checking if time values are within correct ranges. */
    if(data->seconds > 59) 
        data->seconds = 59;

    if(data->minutes > 59) 
        data->minutes = 59;

    if(rtc->am_pm_mode) {
        if(data->hours > 12)
            data->hours = 12;
        else if(data->hours < 1)
            data->hours = 1;
    } else {
        if(data->hours > 23) 
            data->hours = 23;
    }

    if(data->day > 7) 
        data->day = 7;
    else if(data->day < 1)
        data->day = 1;
    
    if(data->date > 31) 
        data->date = 31;
    else if(data->date < 1)
        data->date = 1;

    if(data->month > 12) 
        data->month = 12;
    else if(data->month < 1)
        data->month = 1;
    
    if(data->year > 99) 
        data->year = 99;
        

    temp[0] = bin_to_bcd(data->seconds);

    temp[1] = bin_to_bcd(data->minutes);

    if(rtc->am_pm_mode) {
        temp[2] = bin_to_bcd_am_pm(data->hours);
        temp[2] |= (0x01 << 6);
    } else {
        temp[2] = bin_to_bcd(data->hours);
        temp[2] &= ~(0x01 << 6);
    }

    temp[3] = bin_to_bcd(data->day);

    temp[4] = bin_to_bcd(data->date);

    temp[5] = bin_to_bcd(data->month);

    if(data->century)
        temp[5] |= (0x01 << 7);
    
    temp[6] = bin_to_bcd(data->year);
    
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_SECONDS_REG, 7, temp)) 
        return -1;
    return 0;
}

/**
 * @brief               Reads the timekeeping registers and converts time to real units.
 * 
 * @param[in]   rtc     ds3231 struct.    
 * @param[out]  data    data struct to save converted time units.
 * @return              0 if succesful. 
 */
int ds3231_read_current_time(ds3231_t * rtc, ds3231_data_t * data) {
    uint8_t raw_data[7];
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_SECONDS_REG, 7, raw_data)) 
        return -1;
    
    data->seconds = 10 * ((raw_data[0] & 0x70) >> 4) + (raw_data[0] & 0x0F);

    data->minutes = 10 * ((raw_data[1] & 0x70) >> 4) + (raw_data[1] & 0x0F);
    
    if(rtc->am_pm_mode) {
        data->hours   = 10 * ((raw_data[2] & 0x10) >> 4) + (raw_data[2] & 0x0F);
        data->am_pm = ((raw_data[2] & 0x20) >> 5);
    } else {
        data->hours   = 10 * ((raw_data[2] & 0x30) >> 4) + (raw_data[2] & 0x0F);
    }

    data->day     = (raw_data[3] & (0x07));

    data->date    = 10 * ((raw_data[4] & 0x30) >> 4) + (raw_data[4] & 0x0F);

    data->month   = 10 * ((raw_data[5] & 0x10) >> 4) + (raw_data[5] & 0x0F);

    data->century = (raw_data[5] & (0x01 << 7)) >> 7;

    data->year    = 10 * ((raw_data[6] & 0xF0) >> 4) + (raw_data[6] & 0x0F);

    return 0;
}

/**
 * @brief                   Enable alarm on DS3231 alarm 1. Valid alarm triggers enums are:
 *\n ON_EVERY_SECOND,
 *\n ON_MATCHING_SECOND,
 *\n ON_MATCHING_SECOND_AND_MINUTE,
 *\n ON_MATCHING_SECOND_MINUTE_AND_HOUR, 
 *\n ON_MATCHING_SECOND_MINUTE_HOUR_AND_DATE,
 *\n ON_MATCHING_SECOND_MINUTE_HOUR_AND_DAY, 
 *
 * @param[in] rtc           DS3231 struct pointer.
 * @param[in] alarm_time    Struct that holds time data that will trigger the alarm.
 * @param[in] mask          Valid alarm states.
 * @return                  0 if succesful.
 */
int ds3231_set_alarm_1(ds3231_t * rtc, ds3231_alarm_1_t * alarm_time, enum ALARM_1_MASKS mask) {
    uint8_t temp[4] = {0, 0 , 0, 0};
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_SECONDS_ALARM_1_REG, 4, temp))
        return -1;

        /* Checking if time values are within correct ranges. */
    if(alarm_time->seconds > 59) 
        alarm_time->seconds = 59;

    if(alarm_time->minutes > 59) 
        alarm_time->minutes = 59;

    if(rtc->am_pm_mode) {
        if(alarm_time->hours > 12)
            alarm_time->hours = 12;
        else if(alarm_time->hours < 1)
            alarm_time->hours = 1;
    } else {
        if(alarm_time->hours > 23) 
            alarm_time->hours = 23;
    }

    if(alarm_time->day > 7) 
        alarm_time->day = 7;
    else if(alarm_time->day < 1)
        alarm_time->day = 1;
    
    if(alarm_time->date > 31) 
        alarm_time->date = 31;
    else if(alarm_time->date < 1)
        alarm_time->date = 1;

    
    switch(mask) {
        case ON_EVERY_SECOND:
            for(int i = 0; i < 4; i++) 
                temp[i] |= (0x01 << 7);
        break;

        case ON_MATCHING_SECOND:
            temp[0] = bin_to_bcd(alarm_time->seconds);
            temp[0] &= ~(0x01 << 7);
            for(int i = 1; i < 4; i++) 
                temp[i] |= (0x01 << 7);
        break;

        case ON_MATCHING_SECOND_AND_MINUTE:
            temp[0] = bin_to_bcd(alarm_time->seconds);
            temp[1] = bin_to_bcd(alarm_time->minutes);
            for(int i = 0; i < 2; i++) 
                temp[i] &= ~(0x01 << 7);
            for(int i = 2; i < 4; i++) 
                temp[i] |= (0x01 << 7);
        break;

        case ON_MATCHING_SECOND_MINUTE_AND_HOUR:
            temp[0] = bin_to_bcd(alarm_time->seconds);
            temp[1] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[2] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[2] |= (0x01 << 6);        
            } else {
                temp[2] = bin_to_bcd(alarm_time->hours);
                temp[2] &= ~(0x01 << 6);        
            }     
            for(int i = 0; i < 3; i++)
                temp[i] &= ~(0x01 << 7);            
            temp[3] |= (0x01 << 7);
        break;

        case ON_MATCHING_SECOND_MINUTE_HOUR_AND_DATE:
            temp[0] = bin_to_bcd(alarm_time->seconds);
            temp[1] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[2] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[2] |= (0x01 << 6);        
            } else {
                temp[2] = bin_to_bcd(alarm_time->hours);
                temp[2] &= ~(0x01 << 6);        
            }     
            temp[3] = bin_to_bcd(alarm_time->date);
            temp[3] &= ~(0x01 << 6);
            for(int i = 0; i < 3; i++)
                temp[i] &= ~(0x01 << 7);            
            temp[3] |= (0x01 << 7);
        break;

        case ON_MATCHING_SECOND_MINUTE_HOUR_AND_DAY:
            temp[0] = bin_to_bcd(alarm_time->seconds);
            temp[1] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[2] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[2] |= (0x01 << 6);        
            } else {
                temp[2] = bin_to_bcd(alarm_time->hours);
                temp[2] &= ~(0x01 << 6);        
            }     
            temp[3] = bin_to_bcd(alarm_time->day);
            temp[3] |= (0x01 << 6);
            for(int i = 0; i < 3; i++)
                temp[i] &= ~(0x01 << 7);            
            temp[3] |= (0x01 << 7);
        break;

        default:
            return -1;
        break;
    }

    uint8_t alarm_enable = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &alarm_enable))
        return -1;    
    alarm_enable |= (0x01);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &alarm_enable))
        return -1;

    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_SECONDS_ALARM_1_REG, 4, temp))
        return -1;
    return 0;
}

/**
 * @brief                   Enable alarm on DS3231 alarm 2. Valid alarm triggers enums are:
 *\n ON_EVERY_MINUTE,
 *\n ON_MATCHING_MINUTE,
 *\n ON_MATCHING_MINUTE_AND_HOUR,
 *\n ON_MATCHING_MINUTE_HOUR_AND_DATE, 
 *\n ON_MATCHING_MINUTE_HOUR_AND_DAY, 
 *
 * @param[in] rtc           DS3231 struct pointer.
 * @param[in] alarm_time    Struct that holds time data that will trigger the alarm.
 * @param[in] mask          Valid alarm states.
 * @return                  0 if succesful.
 */
int ds3231_set_alarm_2(ds3231_t * rtc, ds3231_alarm_2_t * alarm_time, enum ALARM_2_MASKS mask) {
    uint8_t temp[3] = {0, 0 , 0};
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_MINUTES_ALARM_2_REG, 3, temp))
        return -1;

    if(alarm_time->minutes > 59) 
        alarm_time->minutes = 59;

    if(rtc->am_pm_mode) {
        if(alarm_time->hours > 12)
            alarm_time->hours = 12;
        else if(alarm_time->hours < 1)
            alarm_time->hours = 1;
    } else {
        if(alarm_time->hours > 23) 
            alarm_time->hours = 23;
    }

    if(alarm_time->day > 7) 
        alarm_time->day = 7;
    else if(alarm_time->day < 1)
        alarm_time->day = 1;
    
    if(alarm_time->date > 31) 
        alarm_time->date = 31;
    else if(alarm_time->date < 1)
        alarm_time->date = 1;

    switch(mask) {
        case ON_EVERY_MINUTE:
            for(int i = 0; i < 3; i++)
                temp[i] |= (0x01 << 7);
        break;

        case ON_MATCHING_MINUTE:
            temp[0] = bin_to_bcd(alarm_time->minutes);
            temp[0] &= ~(0x01 << 7);
            for(int i = 1; i < 3; i++)
                temp[i] |= (0x01 << 7);
        break;

        case ON_MATCHING_MINUTE_AND_HOUR:
            temp[0] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[1] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[1] |= (0x01 << 6);
            } else {
                temp[1] = bin_to_bcd(alarm_time->hours);
                temp[1] &= ~(0x01 << 6);
            }
            for(int i = 0; i < 2; i++)
                temp[i] &= ~(0x01 << 7);
            temp[2] |= (0x01 << 7);
        break;

        case ON_MATCHING_MINUTE_HOUR_AND_DATE:
            temp[0] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[1] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[1] |= (0x01 << 6);
            } else {
                temp[1] = bin_to_bcd(alarm_time->hours);
                temp[1] &= ~(0x01 << 6);
            }
            temp[2] = bin_to_bcd(alarm_time->date);
            temp[2] &= ~(0x01 << 6);
            for(int i = 0; i < 3; i++)
                temp[i] &= ~(0x01 << 7);
        break;

        case ON_MATCHING_MINUTE_HOUR_AND_DAY:
            temp[0] = bin_to_bcd(alarm_time->minutes);
            if(rtc->am_pm_mode) {
                temp[1] = bin_to_bcd_am_pm(alarm_time->hours);
                temp[1] |= (0x01 << 6);
            } else {
                temp[1] = bin_to_bcd(alarm_time->hours);
                temp[1] &= ~(0x01 << 6);
            }
            temp[2] = bin_to_bcd(alarm_time->date);
            temp[2] |= (0x01 << 6);
            for(int i = 0; i < 3; i++)
                temp[i] &= ~(0x01 << 7);
        break;

        default:
            return -1;
        break;
    }

    uint8_t alarm_enable = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &alarm_enable))
        return -1;    
    alarm_enable |= (0x02);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &alarm_enable))
        return -1;

    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_MINUTES_ALARM_2_REG, 3, temp))
        return -1;
    return 0;
}


/**
 * @brief               Enable alarm interrupt on DS3231. If either alarm 1 or alarm 2 is enabled,
 * DS3231 will send a signal through INT/SQW pin. Default mode is enabled. 
 * This functionality is mutually exclusive with square wave output.
 * 
 * @param[in] rtc       DS3231 struct.
 * @param[in] enable    Enabled if true, disabled if false;
 * @return int 
 */
int ds3231_enable_alarm_interrupt(ds3231_t * rtc, bool enable) {
    uint8_t interrupt_enable = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &interrupt_enable))
        return -1;    
    if(enable)
        interrupt_enable |= (0x01 << 2);
    else 
        interrupt_enable &= ~(0x01 << 2);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &interrupt_enable))
        return -1;
    return 0;
} 

/**
 * @brief               Enable or disable the 32.768kHZ square wave output of DS3231 from the 32K pin. 
 * If disabled, the pin goes into high impedence mode.
 * 
 * @param[in] rtc       DS3231 struct.
 * @param[in] enable    Enabled if true, disabled if false.
 * @return              0 if succesful.
 */
int ds3231_enable_32khz_square_wave(ds3231_t * rtc, bool enable) {
    uint8_t status = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;
    if(enable) 
        status |= (0x01 << 3);
    else 
        status &= ~(0x01 << 3);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;    
    return 0;    
}

int ds3231_clear_alarm1(ds3231_t * rtc) {
    uint8_t status = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;
    status &= ~(0x01 << 0);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;    
    return 0;    
}


int ds3231_clear_alarm2(ds3231_t * rtc) {
    uint8_t status = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;
    status &= ~(0x01 << 1);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;    
    return 0;    
}

/**
 * @brief               Enable or disable oscillator on DS3231. It is enabled by default.
 * If disabled, oscillator stops if running on on-board battery (Vbat). 
 * 
 * @param[in] rtc       DS3231 struct.
 * @param[in] enable    Enabled if true, disabled if false.
 * @return              0 if succesful. 
 */
int ds3231_enable_oscillator(ds3231_t * rtc, bool enable) {
    uint8_t enable_byte = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1;    
    if(!enable)
        enable_byte |= (0x01 << 7);
    else 
        enable_byte &= ~(0x01 << 7);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1; 
    return 0;   
}

/**
 * @brief               Enable or disable square-wave output from INT/SQW pin. 
 * This functionality is disabled if alarm interrupts are enabled. It is disabled by default.
 * 
 * @param[in] rtc       DS3231 struct.
 * @param[in] enable    Enabled if true, disabled if false.
 * @return              0 if succesful.
 */
int ds3231_enable_battery_backed_square_wave(ds3231_t * rtc, bool enable) {
    uint8_t enable_byte = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1;    
    if(enable) {
        enable_byte |= (0x01 << 6);
        enable_byte &= ~(0x01 << 2);
    }
    else 
        enable_byte &= ~(0x01 << 6);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1;    
    return 0;
}

/**
 * @brief               Set the frequency of the square-wave output from INT_SQW pin.
 * If square-wave output is selected, the DS3231 module cannot send alarm interrupt signals.
 * Valid expressions for sqr_fqr are:
 * \n FREQUENCY_1_HZ,
 * \n FREQUENCY_1024_HZ,
 * \n FREQUENCY_4096_HZ,
 * \n FREQUENCY_8192_HZ
 * @param rtc           DS3231 struct.
 * @param sqr_frq       Frequency enum.
 * @return              0 if succesful.
 */
int ds3231_set_square_wave_frequency(ds3231_t * rtc, enum SQUARE_WAVE_FREQUENCY sqr_frq) {
    uint8_t enable_byte = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1;    

    enable_byte &= ~(0x18);

    switch(sqr_frq) {
        case FREQUENCY_1_HZ:
        enable_byte |= (FREQUENCY_1_HZ << 3);
        break;

        case FREQUENCY_1024_HZ:
        enable_byte |= (FREQUENCY_1024_HZ << 3);
        break;

        case FREQUENCY_4096_HZ:
        enable_byte |= (FREQUENCY_4096_HZ << 3);
        break;

        case FREQUENCY_8192_HZ:
        enable_byte |= (FREQUENCY_8192_HZ << 3);
        break;

        default:
            return -1;
        break;
    }

    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &enable_byte))
        return -1;   
    return  0;
}

/**
 * @brief           Force the temperature sensor to convert the temperature into digital code 
 * and execute the TCXO algorithm to update the capacitance array to the oscillator.
 * 
 * @param[in] rtc   DS3231 struct.
 * @return          0 if succesful.
 */
int ds3231_force_convert_temperature(ds3231_t * rtc) {
    uint8_t status = 0;
    /* Read the status register to check the BSY bit. */
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status)) 
        return -1;
    /* If BSY bit in the status register is logic 1, the temperature conversion cannot be initiated. */
    if(status & (0x01 << 2)) 
        return -1;
    /* Read the control register and set the CONV bit to logic 1. */
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &status)) 
        return -1;    
    status |= (0x01 <<  5);
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_REG, 1, &status)) 
        return -1;
    return 0;
}

/**
 * @brief                   Read the temperature data from DS3231.
 * 
 * @param[in] rtc           DS3231 struct.      
 * @param[out] temperature  Temperature data with 0.25 resolution.
 * @return                  0 if succesful.
 */ 
int ds3231_read_temperature(ds3231_t * rtc, float * temperature) {
    uint8_t temp[2] = {0, 0};
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_TEMPERATURE_MSB_REG, 2, temp))
        return -1;
    
    *temperature = temp[0] + (float)(1 / (temp[1] >> 6));
    return 0;
}

/**
 * @brief           Check the DS3231 status register to see if the oscillator is working.
 * 
 * @param[in] rtc   DS3231 struct.
 * @return          0 if oscillator is working, 1 if oscillator stopped, -1 if an I2C error occurs.
 */
int ds3231_check_oscillator_stop_flag(ds3231_t * rtc) {
    uint8_t status = 0;
    if(i2c_read_reg(rtc->i2c, rtc->ds3231_addr, DS3231_CONTROL_STATUS_REG, 1, &status))
        return -1;
    if(status & (0x01 << 7)) 
        return 1;
    return 0;
}

/** 
 * @brief               Set the aging offset for the ds3231 for oscillator calibration.
 * The offset value is encoded in two's complement with 7 representing the sign bit.
 * Check the datasheet at the beginning of the file for more information on how to calibrate.
 * 
 * @param[in] rtc       DS3231 struct.  
 * @param[in] offset    Offset value to be written in registers.
 * @return              0 if succesful.
 */
int ds3231_set_aging_offset(ds3231_t * rtc, int8_t offset) {
    int8_t temp = offset;
    uint8_t aging_offset = *((uint8_t *)(&temp));
    if(i2c_write_reg(rtc->i2c, rtc->ds3231_addr, DS3231_AGING_OFFSET_REG, 1, &aging_offset))
        return -1;    
    return 0;
}

/**
 * @brief               Set a interrupt callback function to trigger whenever DS3231 sends an alarm signal.
 * Each core in RP2040 can only have a interrupt single callback function. 
 * 
 * @param[out] gpio     Pin to receive the interrupt signal.     
 * @param[out] callback Pointer to the callback function.
 * @return              0 if succesful.
 */
int ds3231_set_interrupt_callback_function(uint gpio, gpio_irq_callback_t callback) {
    /* Set the pin that will trigger the interrupt as an input pull-up pin. */
    gpio_init(gpio);
    gpio_set_dir(gpio, 0);
    gpio_pull_up(gpio);

    gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_FALL, true, callback);
    return 0;
}
