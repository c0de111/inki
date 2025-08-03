#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "flash.h"

/* ──────────────── Constants ──────────────── */
#ifndef FIRMWARE_MAGIC
#define FIRMWARE_MAGIC      "inki_firmware"
#define FIRMWARE_MAGIC_LEN  (sizeof(FIRMWARE_MAGIC) - 1)
#endif

#ifndef VECTOR_TABLE_OFFSET
#define VECTOR_TABLE_OFFSET 0x200u  // Vector table is located 512 B after header
#endif

#define GATE_PIN 22  // MOSFET gate pin to hold power

/* ───────────── Power Hold Helper ───────────── */
static void hold_power(void) {
    gpio_init(GATE_PIN);
    gpio_set_dir(GATE_PIN, GPIO_OUT);
    gpio_put(GATE_PIN, 1);
    #ifdef USB_BOOTLOADER_ENABLE
    printf("Gate pin high -> power held\n");
    #endif
}

/* ───────────── Firmware Jump ───────────── */
static void __noinline jump_to_firmware(uint32_t header_offset) {
    asm volatile (
        "mov r0, %[vector]\n"    // r0 = address of vector table
        "ldr r1, =%[vtor]\n"     // r1 = address of SCB->VTOR
        "str r0, [r1]\n"         // write Xto VTOR
        "ldmia r0, {r0, r1}\n"   // r0 = initial MSP, r1 = reset handler
        "msr msp, r0\n"          // set stack pointer
        "bx  r1\n"               // jump to reset handler
        :
        : [vector] "r" (XIP_BASE + header_offset + VECTOR_TABLE_OFFSET),
                  [vtor]   "X" (PPB_BASE + M0PLUS_VTOR_OFFSET)
                  : "r0", "r1"
    );
    __builtin_unreachable();
}

/* ───────────── Version Parsing ───────────── */
typedef struct {
    uint8_t major, minor, patch;
    uint32_t build;
} parsed_version_t;

static bool parse_git_version(const char *str, parsed_version_t *out) {
    unsigned int maj = 0, min = 0, pat = 0, bld = 0;

    int matched = sscanf(str, "v%u.%u.%u-%u", &maj, &min, &pat, &bld);
    if (matched == 4) {
        out->major = (uint8_t)maj;
        out->minor = (uint8_t)min;
        out->patch = (uint8_t)pat;
        out->build = bld;
        return true;
    }

    matched = sscanf(str, "v%u.%u.%u", &maj, &min, &pat);
    if (matched == 3) {
        out->major = (uint8_t)maj;
        out->minor = (uint8_t)min;
        out->patch = (uint8_t)pat;
        out->build = 0;
        return true;
    }

    return false;
}

static int compare_versions(const parsed_version_t *a, const parsed_version_t *b) {
    if (a->major != b->major) return (a->major > b->major) ? 1 : -1;
    if (a->minor != b->minor) return (a->minor > b->minor) ? 1 : -1;
    if (a->patch != b->patch) return (a->patch > b->patch) ? 1 : -1;
    if (a->build != b->build) return (a->build > b->build) ? 1 : -1;
    return 0;
}

/* ───────────── Firmware Header Check ───────────── */
static inline bool is_valid_firmware(const firmware_header_t *hdr) {
    return hdr->valid_flag == 1 &&
    memcmp(hdr->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) == 0;
}

/* ───────────── Firmware Selection ───────────── */
static void bootloader_select_firmware(void) {
    const firmware_header_t *slot0 = (const firmware_header_t*) FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET);
    const firmware_header_t *slot1 = (const firmware_header_t*) FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);

    #ifdef USB_BOOTLOADER_ENABLE
    printf("------ Firmware Slot 0 ------\n");
    printf("Magic        : %-13.*s\n", FIRMWARE_MAGIC_LEN, slot0->magic);
    printf("Valid Flag   : %d\n", slot0->valid_flag);
    printf("Build Date   : %.16s\n", slot0->build_date);
    printf("Git Version  : %.32s\n", slot0->git_version);
    printf("Size         : %lu bytes\n", (unsigned long)slot0->firmware_size);
    printf("Slot ID      : %u\n", slot0->slot);
    // printf("CRC32        : 0x%08lX\n", (unsigned long)slot0->crc32);

    printf("------ Firmware Slot 1 ------\n");
    printf("Magic        : %-13.*s\n", FIRMWARE_MAGIC_LEN, slot1->magic);
    printf("Valid Flag   : %d\n", slot1->valid_flag);
    printf("Build Date   : %.16s\n", slot1->build_date);
    printf("Git Version  : %.32s\n", slot1->git_version);
    printf("Size         : %lu bytes\n", (unsigned long)slot1->firmware_size);
    printf("Slot ID      : %u\n", slot1->slot);
    // printf("CRC32        : 0x%08lX\n", (unsigned long)slot1->crc32);
    fflush(stdout);             // <- USB-Ausgabe sicher abschließen
    #endif

    bool valid0 = is_valid_firmware(slot0);
    bool valid1 = is_valid_firmware(slot1);

    #ifdef USB_BOOTLOADER_ENABLE
    printf("\n------ Looking for newest valid firmware------\n");
    #endif

    if (valid0 && valid1) {
        parsed_version_t v0, v1;
        bool ok0 = parse_git_version(slot0->git_version, &v0);
        bool ok1 = parse_git_version(slot1->git_version, &v1);

        #ifdef USB_BOOTLOADER_ENABLE
        // printf("[DEBUG] Parsed slot 0: %d.%d.%d build %u (ok=%d)\n", v0.major, v0.minor, v0.patch, v0.build, ok0);
        // printf("[DEBUG] Parsed slot 1: %d.%d.%d build %u (ok=%d)\n", v1.major, v1.minor, v1.patch, v1.build, ok1);
        #endif
        if (ok0 && ok1) {
            int cmp = compare_versions(&v0, &v1);
            if (cmp >= 0) {
                #ifdef USB_BOOTLOADER_ENABLE
                printf("→ Booting Slot 0 (newer or equal)\n");
                fflush(stdout);             // <- USB-Ausgabe sicher abschließen
                #endif
                jump_to_firmware(FIRMWARE_SLOT0_FLASH_OFFSET);
            } else {
                #ifdef USB_BOOTLOADER_ENABLE
                printf("→ Booting Slot 1 (newer)\n");
                fflush(stdout);             // <- USB-Ausgabe sicher abschließen
                #endif
                jump_to_firmware(FIRMWARE_SLOT1_FLASH_OFFSET);
            }
            return;
        }
    }

    if (valid0) {
        #ifdef USB_BOOTLOADER_ENABLE
        printf("→ Booting Slot 0 (only valid firmware, fallback)\n");
        fflush(stdout);             // <- USB-Ausgabe sicher abschließen
        #endif
        jump_to_firmware(FIRMWARE_SLOT0_FLASH_OFFSET);
    }

    if (valid1) {
        #ifdef USB_BOOTLOADER_ENABLE
        printf("→ Booting Slot 1 (only valid firmware, fallback)\n");
        #endif
        fflush(stdout);             // <- USB-Ausgabe sicher abschließen
        jump_to_firmware(FIRMWARE_SLOT1_FLASH_OFFSET);
    }

    #ifdef USB_BOOTLOADER_ENABLE
    printf("❌ No valid firmware found. System halted.\n");
    fflush(stdout);             // <- USB-Ausgabe sicher abschließen
    #endif
}

/* ───────────── Entry Point ───────────── */
int main(void) {
    #ifdef USB_BOOTLOADER_ENABLE
    sleep_ms(10);               // Give USB time to initialize
    stdio_init_all();           // Initialize UART and USB-CDC

    while (!stdio_usb_connected()) {
        sleep_ms(10);           // Wait for USB serial to be ready
    }

    printf("\n=== Bootloader started ===\n");
    #endif

    // Optional: keep power via MOSFET gate
    // hold_power();

    bootloader_select_firmware();  // Will jump if successful

    while (true) {
        sleep_ms(500);  // Idle loop if no valid firmware is found
        // Optional: blink LED to indicate error
    }
}
