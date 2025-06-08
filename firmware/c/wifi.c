/**
 * @file    wifi.c
 * @brief   Implementation of Wi-Fi functionality for the eSign project.
 *
 * Provides global variables and configurations for Wi-Fi connection,
 * including country settings, authentication mode, and MAC address storage.
 */

#include "wifi.h"
#include "pico/cyw43_arch.h" // Pico SDK header for Wi-Fi country and auth definitions

/**
 * @brief   Country configuration for Wi-Fi.
 * @details Used to set the regulatory domain for Wi-Fi operation.
 *          Default is Germany (CYW43_COUNTRY_GERMANY).
 */
uint32_t country = CYW43_COUNTRY_GERMANY;

/**
 * @brief   Wi-Fi authentication mode.
 * @details Configured to use WPA2 mixed mode (CYW43_AUTH_WPA2_MIXED_PSK).
 */
uint32_t auth = CYW43_AUTH_WPA2_MIXED_PSK;

/**
 * @brief   MAC address of the Wi-Fi interface.
 * @details Populated during Wi-Fi initialization and used for network communication.
 */
uint8_t mac_address[6] = {0}; // Initialize with zeros

