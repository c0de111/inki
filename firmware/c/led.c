#include "led.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

static uint8_t s_ext_led_gpio = 0xFF;

void ext_led_init(uint8_t gpio) {
    s_ext_led_gpio = gpio;
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0); // start off
}

bool ext_led_is_initialized(void) {
    return s_ext_led_gpio != 0xFF;
}

void ext_led_on(void) {
    if (s_ext_led_gpio != 0xFF) gpio_put(s_ext_led_gpio, 1);
}

void ext_led_off(void) {
    if (s_ext_led_gpio != 0xFF) gpio_put(s_ext_led_gpio, 0);
}

void ext_led_toggle(void) {
    if (s_ext_led_gpio != 0xFF) gpio_put(s_ext_led_gpio, !gpio_get_out_level(s_ext_led_gpio));
}

void board_led_on(void) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
}

void board_led_off(void) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

void board_led_toggle(void) {
    // Readback not available; emulate by keeping both on/off calls? Simpler: track is not necessary here.
    static bool s_state = false;
    s_state = !s_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, s_state ? 1 : 0);
}

