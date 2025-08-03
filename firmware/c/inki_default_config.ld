MEMORY
{
    WIFI_FLASH        (rx) : ORIGIN = 0x101E7000, LENGTH = 4K
    SEATSURFING_FLASH (rx) : ORIGIN = 0x101E8000, LENGTH = 4K
    DEVICE_FLASH      (rx) : ORIGIN = 0x101E9000, LENGTH = 4K
}

SECTIONS
{
    .wifi_config 0x101E7000 : ALIGN(4096) {
        KEEP(*(.wifi_config))
    } > WIFI_FLASH

    .seatsurfing_config 0x101E8000 : ALIGN(4096) {
        KEEP(*(.seatsurfing_config))
    } > SEATSURFING_FLASH

    .device_config 0x101E9000 : ALIGN(4096) {
        KEEP(*(.device_config))
    } > DEVICE_FLASH
}
