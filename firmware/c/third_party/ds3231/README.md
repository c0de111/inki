# pico-ds3231
This is a work in progress Raspberry Pi Pico library for DS3231 real time clock module.
Current features are:
1. Configuring time.
2. Reading time.
3. Setting alarm 1 and alarm 2.
4. Setting alarm interrupt from SQW pin.
5. AM/PM mode.
6. Setting output of square wave signal from SQW pin.
7. Enabling-Disabling oscillator when on powered by on-board battery.
8. Setting 32kHZ output from 32K pin.
9. Reading temperature data.
10. Setting aging offset calibration data.

# Currently Working On:
1. Reading and writing functions for onboard EEPROM AT24C32.

# How to Add to Project:
-
1. Copy ds3231 folder under libraries folder.
2. Add "add_subdirectory(ds3231)" to your CMakeLists.txt file.
3. add "pico_ds3231" in your CMake "target_link_libraries" line 
