# Pimoroni Pico Plus 2: RP2350B, 16MB flash, 8MB PSRAM on GPIO47.
set(PICO_BOARD "pimoroni_pico_plus2_rp2350")
set(PICO_PLATFORM "rp2350")
set(PICO_NUM_GPIOS 48)

if(NOT DEFINED MICROPY_HW_FLASH_STORAGE_BYTES)
    set(MICROPY_HW_FLASH_STORAGE_BYTES 14680064)  # 14 * 1024 * 1024
endif()

# PSRAM (CS pin and size from the pico-sdk board header) becomes the GC heap,
# so most of SRAM can go to the C heap that AMY mallocs from.
set(MICROPY_HW_ENABLE_PSRAM 1)
set(MICROPY_C_HEAP_SIZE 360448)  # 352 KB

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
