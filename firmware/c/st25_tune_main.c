/*
 * ST25 RF antenna tuning firmware for inki L2 PCB.
 *
 * Standalone helper: measures V_EH (energy harvesting voltage) via ADC
 * and displays a live ANSI terminal graph for antenna matching optimization.
 *
 * Ported from NFC_exploration/firmware/src/tune_main.c with inki L2 pin
 * mapping: ADC2 (GP28) for V_EH, GP18 for ST25 VCC enable.
 *
 * Build:  ./build.sh --st25-tune
 * Flash:  SWD to slot0 address (0x10010000) or use UF2
 * Use:    sudo tio /dev/ttyACM0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#if ST25_TUNE_HAS_CYW43
#include "pico/cyw43_arch.h"
#endif

#include "st25dv.h"

/* ---------- pin and hardware configuration (inki L2 PCB) ----------------- */

#ifndef ST25_TUNE_I2C_INSTANCE
#define ST25_TUNE_I2C_INSTANCE i2c0
#endif

#ifndef ST25_TUNE_I2C_BAUDRATE_HZ
#define ST25_TUNE_I2C_BAUDRATE_HZ 100000
#endif

#ifndef ST25_TUNE_I2C_SDA_PIN
#define ST25_TUNE_I2C_SDA_PIN 20
#endif

#ifndef ST25_TUNE_I2C_SCL_PIN
#define ST25_TUNE_I2C_SCL_PIN 21
#endif

#ifndef ST25_TUNE_VCC_EN_PIN
#define ST25_TUNE_VCC_EN_PIN 18
#endif

#ifndef ST25_TUNE_POWER_ON_DELAY_MS
#define ST25_TUNE_POWER_ON_DELAY_MS 10
#endif

#ifndef ST25_TUNE_I2C_OP_TIMEOUT_US
#define ST25_TUNE_I2C_OP_TIMEOUT_US 20000
#endif

/* Power hold: inki L2 uses GP22 (GATE_PIN) for power latch */
#ifndef ST25_TUNE_POWER_HOLD_PIN
#define ST25_TUNE_POWER_HOLD_PIN 22
#endif

/* ---------- ADC / tuning parameters -------------------------------------- */

/* ADC2 = GP28 = V_EH on inki L2 */
#ifndef ST25_TUNE_ADC_INPUT
#define ST25_TUNE_ADC_INPUT 2
#endif

#ifndef ST25_TUNE_SAMPLE_COUNT
#define ST25_TUNE_SAMPLE_COUNT 256
#endif

#ifndef ST25_TUNE_REPORT_INTERVAL_MS
#define ST25_TUNE_REPORT_INTERVAL_MS 100
#endif

#ifndef ST25_TUNE_SAMPLE_DELAY_US
#define ST25_TUNE_SAMPLE_DELAY_US 20
#endif

#ifndef ST25_TUNE_EMA_ALPHA
#define ST25_TUNE_EMA_ALPHA 0.50f
#endif

#ifndef ST25_TUNE_BASELINE_SAMPLES
#define ST25_TUNE_BASELINE_SAMPLES 4000
#endif

#ifndef ST25_TUNE_FIELD_ON_MV
#define ST25_TUNE_FIELD_ON_MV 9.0f
#endif

#ifndef ST25_TUNE_FIELD_OFF_MV
#define ST25_TUNE_FIELD_OFF_MV 4.0f
#endif

#ifndef ST25_TUNE_DEBOUNCE_COUNT
#define ST25_TUNE_DEBOUNCE_COUNT 1
#endif

#ifndef ST25_TUNE_BASELINE_TRACK_ALPHA
#define ST25_TUNE_BASELINE_TRACK_ALPHA 0.01f
#endif

#ifndef ST25_TUNE_FREEZE_BASELINE
#define ST25_TUNE_FREEZE_BASELINE 1
#endif

#ifndef ST25_TUNE_GRAPH_WIDTH
#define ST25_TUNE_GRAPH_WIDTH 72
#endif

#ifndef ST25_TUNE_GRAPH_HEIGHT
#define ST25_TUNE_GRAPH_HEIGHT 20
#endif

#ifndef ST25_TUNE_GRAPH_MAX_MV
#define ST25_TUNE_GRAPH_MAX_MV 3000.0f
#endif

#ifndef ST25_TUNE_BASELINE_SETTLE_MS
#define ST25_TUNE_BASELINE_SETTLE_MS 400
#endif

#ifndef ST25_TUNE_EH_ENFORCE_RETRIES
#define ST25_TUNE_EH_ENFORCE_RETRIES 8
#endif

#ifndef ST25_TUNE_EH_ENFORCE_RETRY_MS
#define ST25_TUNE_EH_ENFORCE_RETRY_MS 20
#endif

#ifndef ST25_TUNE_EH_RECHECK_MS
#define ST25_TUNE_EH_RECHECK_MS 1000
#endif

/* ---------- ANSI color codes --------------------------------------------- */

#define C_RESET "\033[0m"
#define C_CYAN "\033[36m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE "\033[34m"
#define C_RED "\033[31m"
#define C_DIM "\033[2m"
#define C_BOLD_GREEN "\033[1;32m"
#define C_BOLD_YELLOW "\033[1;33m"
#define C_BOLD_RED "\033[1;31m"
#define TERM_HIDE "\033[?25l"

/* ---------- I2C setup ---------------------------------------------------- */

static void i2c_setup(void) {
    i2c_init(ST25_TUNE_I2C_INSTANCE, ST25_TUNE_I2C_BAUDRATE_HZ);
    gpio_set_function(ST25_TUNE_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(ST25_TUNE_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(ST25_TUNE_I2C_SDA_PIN);
    gpio_pull_up(ST25_TUNE_I2C_SCL_PIN);
}

static void init_power_hold(void) {
#if (ST25_TUNE_POWER_HOLD_PIN >= 0)
    gpio_init(ST25_TUNE_POWER_HOLD_PIN);
    gpio_set_dir(ST25_TUNE_POWER_HOLD_PIN, GPIO_OUT);
    gpio_put(ST25_TUNE_POWER_HOLD_PIN, 1);
#endif
}

static void st25_power_on(void) {
#if (ST25_TUNE_VCC_EN_PIN >= 0)
    gpio_init(ST25_TUNE_VCC_EN_PIN);
    gpio_set_dir(ST25_TUNE_VCC_EN_PIN, GPIO_OUT);
    gpio_put(ST25_TUNE_VCC_EN_PIN, 1);
#endif
}

/* ---------- ST25 I2C bus adapter ----------------------------------------- */

static uint8_t addr7(uint16_t st_addr8) { return (uint8_t)(st_addr8 >> 1); }

static int32_t bus_init(void) {
    i2c_setup();
    return 0;
}
static int32_t bus_deinit(void) { return 0; }
static uint32_t bus_get_tick(void) { return (uint32_t)to_ms_since_boot(get_absolute_time()); }

static int32_t bus_is_ready(uint16_t dev_addr, const uint32_t trials) {
    const uint8_t a7 = addr7(dev_addr);
    const uint8_t probe[2] = {0x00, 0x00};
    for (uint32_t i = 0; i < trials; i++) {
        if (i2c_write_timeout_us(ST25_TUNE_I2C_INSTANCE, a7, probe, sizeof(probe), false,
                                 ST25_TUNE_I2C_OP_TIMEOUT_US) == (int)sizeof(probe)) {
            return NFCTAG_OK;
        }
        sleep_ms(2);
    }
    return NFCTAG_NACK;
}

static int32_t bus_write(uint16_t dev_addr, uint16_t reg, const uint8_t *data, uint16_t len) {
    const uint8_t a7 = addr7(dev_addr);
    uint8_t tmp[2 + ST25DV_MAX_WRITE_BYTE];
    if (len > ST25DV_MAX_WRITE_BYTE)
        return NFCTAG_ERROR;
    tmp[0] = (uint8_t)((reg >> 8) & 0xFF);
    tmp[1] = (uint8_t)(reg & 0xFF);
    for (uint16_t i = 0; i < len; i++)
        tmp[2 + i] = data[i];
    const int res = i2c_write_timeout_us(ST25_TUNE_I2C_INSTANCE, a7, tmp, (size_t)(len + 2), false,
                                         ST25_TUNE_I2C_OP_TIMEOUT_US);
    return (res < 0) ? NFCTAG_NACK : NFCTAG_OK;
}

static int32_t bus_read(uint16_t dev_addr, uint16_t reg, uint8_t *data, uint16_t len) {
    const uint8_t a7 = addr7(dev_addr);
    const uint8_t addr_buf[2] = {
        (uint8_t)((reg >> 8) & 0xFF),
        (uint8_t)(reg & 0xFF),
    };
    int res = i2c_write_timeout_us(ST25_TUNE_I2C_INSTANCE, a7, addr_buf, sizeof(addr_buf), true,
                                   ST25_TUNE_I2C_OP_TIMEOUT_US);
    if (res < 0)
        return NFCTAG_NACK;
    res = i2c_read_timeout_us(ST25_TUNE_I2C_INSTANCE, a7, data, len, false,
                              ST25_TUNE_I2C_OP_TIMEOUT_US);
    return (res < 0) ? NFCTAG_NACK : NFCTAG_OK;
}

/* ---------- EH (energy harvesting) control ------------------------------- */

static bool tick_elapsed(uint32_t now_ms, uint32_t deadline_ms) {
    return ((int32_t)(now_ms - deadline_ms) >= 0);
}

static bool st25_enforce_eh(ST25DV_Object_t *st, bool verbose, ST25DV_EH_CTRL *out) {
    ST25DV_EH_CTRL eh = {0};
    int32_t ret;
    for (uint32_t attempt = 0; attempt < (uint32_t)ST25_TUNE_EH_ENFORCE_RETRIES; attempt++) {
        ret = ST25DV_ReadEHCtrl_Dyn(st, &eh);
        if (ret == NFCTAG_OK && eh.EH_EN_Mode == ST25DV_ENABLE) {
            if (out)
                *out = eh;
            if (verbose && attempt > 0)
                printf("[OK] EH enable latched after %lu retries\n", (unsigned long)attempt);
            return true;
        }
        ret = ST25DV_SetEHENMode_Dyn(st);
        if (ret != NFCTAG_OK)
            (void)ST25DV_WriteEHMode(st, ST25DV_EH_ACTIVE_AFTER_BOOT);
        sleep_ms(ST25_TUNE_EH_ENFORCE_RETRY_MS);
    }
    ret = ST25DV_ReadEHCtrl_Dyn(st, &eh);
    if (ret == NFCTAG_OK && out)
        *out = eh;
    if (verbose)
        printf("[WARN] EH not latched: EN=%u ON=%u FIELD=%u VCC=%u\n", (unsigned)eh.EH_EN_Mode,
               (unsigned)eh.EH_on, (unsigned)eh.Field_on, (unsigned)eh.VCC_on);
    return false;
}

static bool st25_rearm_eh(ST25DV_Object_t *st, bool verbose, ST25DV_EH_CTRL *out) {
    (void)ST25DV_ResetEHENMode_Dyn(st);
    sleep_ms(2);
    (void)ST25DV_WriteEHMode(st, ST25DV_EH_ACTIVE_AFTER_BOOT);
    sleep_ms(2);
    return st25_enforce_eh(st, verbose, out);
}

static bool st25_enable_eh_mode(ST25DV_Object_t *st) {
    bool ok = true;
    int32_t ret;

    ret = ST25DV_WriteEHMode(st, ST25DV_EH_ACTIVE_AFTER_BOOT);
    if (ret == NFCTAG_OK) {
        printf("%s[OK]%s EH mode: ACTIVE_AFTER_BOOT\n", C_BOLD_GREEN, C_RESET);
    } else {
        printf("%s[ERR]%s EH mode write failed: %ld\n", C_BOLD_RED, C_RESET, (long)ret);
        ok = false;
    }

    ST25DV_EH_CTRL eh = {0};
    if (!st25_enforce_eh(st, true, &eh)) {
        printf("[WARN] trying rearm...\n");
        if (!st25_rearm_eh(st, true, &eh))
            ok = false;
    }

    ret = ST25DV_ReadEHCtrl_Dyn(st, &eh);
    if (ret == NFCTAG_OK) {
        printf("%s[INFO]%s EH: EN=%u ON=%u FIELD=%u VCC=%u\n", C_CYAN, C_RESET,
               (unsigned)eh.EH_EN_Mode, (unsigned)eh.EH_on, (unsigned)eh.Field_on,
               (unsigned)eh.VCC_on);
    }
    return ok;
}

/* ---------- ADC sampling ------------------------------------------------- */

static int adc_input_to_gpio(uint input) { return (input <= 3u) ? (int)(26u + input) : -1; }

static float adc_raw_to_v(float raw) { return raw * (3.3f / 4095.0f); }

static uint16_t sample_once(uint input) {
    adc_select_input(input);
    sleep_us(2);
    (void)adc_read();
    sleep_us(2);
    return adc_read();
}

static float sample_average(uint input, uint32_t count) {
    if (count == 0u)
        return (float)sample_once(input);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += sample_once(input);
#if (ST25_TUNE_SAMPLE_DELAY_US > 0)
        sleep_us(ST25_TUNE_SAMPLE_DELAY_US);
#endif
    }
    return (float)sum / (float)count;
}

/* ---------- EH control quick read ---------------------------------------- */

static bool eh_ctrl_quick(ST25DV_Object_t *st, ST25DV_EH_CTRL *out) {
    if (ST25DV_ReadEHCtrl_Dyn(st, out) == NFCTAG_OK)
        return true;
    sleep_ms(2);
    return ST25DV_ReadEHCtrl_Dyn(st, out) == NFCTAG_OK;
}

/* ---------- graph helpers ------------------------------------------------ */

static uint32_t bar_height(float mv, float max_mv, uint32_t height) {
    if (height == 0u || max_mv <= 0.0f)
        return 0u;
    if (mv < 0.0f)
        mv = 0.0f;
    if (mv > max_mv)
        mv = max_mv;
    uint32_t h = (uint32_t)((mv * (float)height / max_mv) + 0.5f);
    return (h > height) ? height : h;
}

static int32_t row_for_mv(float mv, float max_mv, uint32_t height) {
    if (height == 0u || max_mv <= 0.0f)
        return -1;
    if (mv < 0.0f)
        mv = 0.0f;
    if (mv > max_mv)
        mv = max_mv;
    int32_t row = (int32_t)((1.0f - mv / max_mv) * (float)(height - 1u) + 0.5f);
    if (row < 0)
        row = 0;
    if (row >= (int32_t)height)
        row = (int32_t)height - 1;
    return row;
}

static const char *color_for_row(uint32_t row, uint32_t height) {
    if (height <= 1u)
        return C_GREEN;
    const float frac = 1.0f - ((float)row / (float)(height - 1u));
    if (frac > 0.85f)
        return C_BOLD_RED;
    if (frac > 0.65f)
        return C_BOLD_YELLOW;
    if (frac > 0.45f)
        return C_BOLD_GREEN;
    if (frac > 0.25f)
        return C_CYAN;
    return C_BLUE;
}

/* ---------- main --------------------------------------------------------- */

int main(void) {
    init_power_hold();
    stdio_init_all();
    sleep_ms(1500);

    printf("%s[BOOT]%s inki ST25 tune (L2 PCB)\n", C_GREEN, C_RESET);
    printf("%s[INFO]%s I2C: SDA=GP%u SCL=GP%u @ %u Hz\n", C_CYAN, C_RESET,
           (unsigned)ST25_TUNE_I2C_SDA_PIN, (unsigned)ST25_TUNE_I2C_SCL_PIN,
           (unsigned)ST25_TUNE_I2C_BAUDRATE_HZ);
    printf("%s[INFO]%s VCC_EN: GP%d, ADC input: %u (GP%d)\n", C_CYAN, C_RESET, ST25_TUNE_VCC_EN_PIN,
           (unsigned)ST25_TUNE_ADC_INPUT, adc_input_to_gpio(ST25_TUNE_ADC_INPUT));
    printf("%s[INFO]%s Power hold: GP%d\n", C_CYAN, C_RESET, ST25_TUNE_POWER_HOLD_PIN);

    st25_power_on();
    sleep_ms(ST25_TUNE_POWER_ON_DELAY_MS);

    /* Register ST25 bus I/O */
    ST25DV_Object_t st = {0};
    ST25DV_IO_t io = {
        .Init = bus_init,
        .DeInit = bus_deinit,
        .IsReady = bus_is_ready,
        .Write = bus_write,
        .Read = bus_read,
        .GetTick = bus_get_tick,
    };
    if (ST25DV_RegisterBusIO(&st, &io) != NFCTAG_OK) {
        printf("%s[ERR]%s RegisterBusIO failed\n", C_BOLD_RED, C_RESET);
        while (true)
            sleep_ms(1000);
    }
    if (St25Dv_Drv.Init(&st) != NFCTAG_OK) {
        printf("%s[ERR]%s driver init failed\n", C_BOLD_RED, C_RESET);
        while (true)
            sleep_ms(1000);
    }
    printf("%s[OK]%s ST25 init\n", C_BOLD_GREEN, C_RESET);

    uint8_t icref = 0;
    if (St25Dv_Drv.ReadID(&st, &icref) == NFCTAG_OK)
        printf("%s[INFO]%s ICREF: 0x%02X\n", C_CYAN, C_RESET, (unsigned)icref);

    ST25DV_MEM_SIZE mem = {0};
    if (ST25DV_ReadMemSize(&st, &mem) == NFCTAG_OK) {
        uint32_t total = ((uint32_t)mem.Mem_Size + 1u) * ((uint32_t)mem.BlockSize + 1u);
        printf("%s[INFO]%s memory: %lu bytes\n", C_CYAN, C_RESET, (unsigned long)total);
    }

    if (!st25_enable_eh_mode(&st)) {
        printf("%s[ERR]%s EH setup failed. Stopping.\n", C_BOLD_RED, C_RESET);
        while (true)
            sleep_ms(1000);
    }

    /* ADC init */
    const int adc_gpio = adc_input_to_gpio(ST25_TUNE_ADC_INPUT);
    // cppcheck-suppress knownConditionTrueFalse
    if (adc_gpio < 0) {
        printf("%s[ERR]%s Invalid ADC input %u\n", C_BOLD_RED, C_RESET,
               (unsigned)ST25_TUNE_ADC_INPUT);
        while (true)
            sleep_ms(1000);
    }
    adc_init();
    adc_set_temp_sensor_enabled(false);
    adc_gpio_init((uint)adc_gpio);
    adc_set_round_robin(0);
    adc_fifo_drain();
    adc_select_input(ST25_TUNE_ADC_INPUT);
    sleep_ms(2);

    printf("%s[OK]%s ADC: GP%d (ADC%u)\n", C_BOLD_GREEN, C_RESET, adc_gpio,
           (unsigned)ST25_TUNE_ADC_INPUT);

    /* Baseline capture */
    printf("%s[INFO]%s Baseline settle: %u ms\n", C_CYAN, C_RESET,
           (unsigned)ST25_TUNE_BASELINE_SETTLE_MS);
    sleep_ms(ST25_TUNE_BASELINE_SETTLE_MS);
    printf("%s[INFO]%s Baseline capture — keep RF away...\n", C_CYAN, C_RESET);
    const float baseline_raw = sample_average(ST25_TUNE_ADC_INPUT, ST25_TUNE_BASELINE_SAMPLES);
    float baseline_v = adc_raw_to_v(baseline_raw);
    printf("%s[OK]%s Baseline: raw=%.1f v=%.3fV\n", C_BOLD_GREEN, C_RESET, (double)baseline_raw,
           (double)baseline_v);

    /* State */
    float ema_v = baseline_v;
    bool rf_on = false;
    uint32_t rise_hits = 0, fall_hits = 0, sessions = 0;
    float session_peak_delta_mv = 0.0f, last_peak_delta_mv = 0.0f;
    ST25DV_EH_CTRL eh_last = {0};
    bool eh_valid = false;
    uint32_t next_eh_recheck = bus_get_tick();

    const float on_th_v = ST25_TUNE_FIELD_ON_MV / 1000.0f;
    const float off_th_v = ST25_TUNE_FIELD_OFF_MV / 1000.0f;
    float hist[ST25_TUNE_GRAPH_WIDTH];
    uint32_t head = 0;
    for (uint32_t i = 0; i < (uint32_t)ST25_TUNE_GRAPH_WIDTH; i++)
        hist[i] = 0.0f;
    const int32_t on_row =
        row_for_mv(ST25_TUNE_FIELD_ON_MV, ST25_TUNE_GRAPH_MAX_MV, ST25_TUNE_GRAPH_HEIGHT);
    const int32_t off_row =
        row_for_mv(ST25_TUNE_FIELD_OFF_MV, ST25_TUNE_GRAPH_MAX_MV, ST25_TUNE_GRAPH_HEIGHT);

    printf("\033[2J\033[H" TERM_HIDE);
    fflush(stdout);

    /* Main loop */
    while (true) {
        printf("\033[H");

        const float raw_avg = sample_average(ST25_TUNE_ADC_INPUT, ST25_TUNE_SAMPLE_COUNT);
        const uint16_t raw_u16 = (uint16_t)(raw_avg + 0.5f);
        const float v_avg = adc_raw_to_v(raw_avg);

        ema_v = (ST25_TUNE_EMA_ALPHA * v_avg) + ((1.0f - ST25_TUNE_EMA_ALPHA) * ema_v);

        if (!rf_on && !ST25_TUNE_FREEZE_BASELINE)
            baseline_v = (ST25_TUNE_BASELINE_TRACK_ALPHA * ema_v) +
                         ((1.0f - ST25_TUNE_BASELINE_TRACK_ALPHA) * baseline_v);

        float delta_v = ema_v - baseline_v;
        float delta_pos = (delta_v < 0.0f) ? 0.0f : delta_v;
        float delta_mv = delta_v * 1000.0f;
        float delta_mv_pos = delta_pos * 1000.0f;

        hist[head] = delta_mv_pos;
        head = (head + 1u) % (uint32_t)ST25_TUNE_GRAPH_WIDTH;

        /* RF state machine */
        if (!rf_on) {
            rise_hits = (delta_pos >= on_th_v) ? rise_hits + 1 : 0;
            if (rise_hits >= ST25_TUNE_DEBOUNCE_COUNT) {
                rf_on = true;
                rise_hits = fall_hits = 0;
                sessions++;
                session_peak_delta_mv = delta_mv_pos;
            }
        } else {
            if (delta_mv_pos > session_peak_delta_mv)
                session_peak_delta_mv = delta_mv_pos;
            fall_hits = (delta_pos <= off_th_v) ? fall_hits + 1 : 0;
            if (fall_hits >= ST25_TUNE_DEBOUNCE_COUNT) {
                rf_on = false;
                fall_hits = rise_hits = 0;
                last_peak_delta_mv = session_peak_delta_mv;
            }
        }

        /* EH monitor and repair */
        ST25DV_EH_CTRL eh_now = {0};
        if (eh_ctrl_quick(&st, &eh_now)) {
            eh_last = eh_now;
            eh_valid = true;
        }
        const uint32_t now = bus_get_tick();
        if (eh_valid &&
            (eh_last.EH_EN_Mode == ST25DV_DISABLE ||
             (eh_last.Field_on == ST25DV_ENABLE && eh_last.EH_on == ST25DV_DISABLE)) &&
            tick_elapsed(now, next_eh_recheck)) {
            ST25DV_EH_CTRL repair = {0};
            if (!st25_enforce_eh(&st, false, &repair) && eh_last.Field_on == ST25DV_ENABLE)
                st25_rearm_eh(&st, false, &repair);
            eh_last = repair;
            eh_valid = true;
            next_eh_recheck = now + ST25_TUNE_EH_RECHECK_MS;
        }

        /* Draw graph */
        for (uint32_t row = 0; row < (uint32_t)ST25_TUNE_GRAPH_HEIGHT; row++) {
            float y_mv = ST25_TUNE_GRAPH_MAX_MV * (float)((uint32_t)ST25_TUNE_GRAPH_HEIGHT - row) /
                         (float)ST25_TUNE_GRAPH_HEIGHT;
            printf("\r%s%6.1f%s |", C_CYAN, (double)y_mv, C_RESET);
            for (uint32_t col = 0; col < (uint32_t)ST25_TUNE_GRAPH_WIDTH; col++) {
                uint32_t src = (head + col) % (uint32_t)ST25_TUNE_GRAPH_WIDTH;
                uint32_t h = bar_height(hist[src], ST25_TUNE_GRAPH_MAX_MV, ST25_TUNE_GRAPH_HEIGHT);
                bool filled = h >= ((uint32_t)ST25_TUNE_GRAPH_HEIGHT - row);
                if (filled) {
                    printf("%s#%s", color_for_row(row, ST25_TUNE_GRAPH_HEIGHT), C_RESET);
                } else if ((int32_t)row == on_row) {
                    printf("%s-%s", C_RED, C_RESET);
                } else if ((int32_t)row == off_row) {
                    printf("%s.%s", C_YELLOW, C_RESET);
                } else if ((row % 5u) == 0u) {
                    printf("%s.%s", C_DIM, C_RESET);
                } else {
                    printf(" ");
                }
            }
            printf("|\n");
        }

        /* X-axis */
        printf("\r       +");
        for (uint32_t col = 0; col < (uint32_t)ST25_TUNE_GRAPH_WIDTH; col++)
            printf((col % 10u == 9u) ? "+" : "-");
        printf("+\n");

        /* Status line */
        const char *delta_c = (delta_mv >= 0.0f) ? (rf_on ? C_BOLD_GREEN : C_YELLOW) : C_RED;
        printf(
            "\r%s[TUNE]%s RF=%s%s%s raw=%4u v=%.3fV ema=%.3fV base=%.3fV "
            "d=%s%+6.1fmV%s peak=%6.1fmV sess=%3lu | "
            "EH:%s en=%u on=%u f=%u vcc=%u | %s%s%s      ",
            C_CYAN, C_RESET, rf_on ? C_BOLD_GREEN : C_BOLD_YELLOW, rf_on ? "ON " : "OFF", C_RESET,
            (unsigned)raw_u16, (double)v_avg, (double)ema_v, (double)baseline_v, delta_c,
            (double)delta_mv, C_RESET, (double)(rf_on ? session_peak_delta_mv : last_peak_delta_mv),
            (unsigned long)sessions, eh_valid ? "OK" : "NA",
            eh_valid ? (unsigned)eh_last.EH_EN_Mode : 0u, eh_valid ? (unsigned)eh_last.EH_on : 0u,
            eh_valid ? (unsigned)eh_last.Field_on : 0u, eh_valid ? (unsigned)eh_last.VCC_on : 0u,
            (raw_u16 >= 4090u) ? C_BOLD_RED : C_GREEN, (raw_u16 >= 4090u) ? "SAT" : "OK ", C_RESET);
        fflush(stdout);

        sleep_ms(ST25_TUNE_REPORT_INTERVAL_MS);
    }
}
