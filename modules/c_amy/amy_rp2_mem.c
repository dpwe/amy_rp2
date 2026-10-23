// amy_rp2_mem.c -- AMY's malloc_caps for rp2: SRAM first, PSRAM fallback.
//
// AMY allocates through malloc_caps() (we build it with MALLOC_CAPS_DEFINED so
// amy.c leaves the definition to us) but frees with plain free(), so AMY's
// sources are built with -Dfree=amy_rp2_free to route pool pointers back here.
//
// The PSRAM pool is a static array in the SDK's .psram_uninitialised section,
// which MicroPython's GC heap (starting at __psram_end__) automatically avoids.
// Allocations never run on both cores at once (see amy_rp2.c), but the pool is
// still guarded by a critical section so stray frees can't corrupt it.

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>

#include "amy_rp2.h"
#include "hardware/structs/watchdog.h"
#include "pico/critical_section.h"

#define POOL_BYTES (2 * 1024 * 1024)
#define ALIGN 16
#define HDR ALIGN  // block header, keeps payloads 16-byte aligned

static uint8_t psram_pool[POOL_BYTES]
    __attribute__((section(".psram_uninitialised.amy_pool"), aligned(ALIGN)));

typedef struct blk {
    size_t size;  // whole block, header included
    struct blk *next;
} blk_t;

static blk_t *free_list = NULL;  // address-ordered, for coalescing
static bool pool_ready = false;
static critical_section_t pool_cs;

static uint32_t psram_in_use = 0;
static uint32_t alloc_failures = 0;

static inline bool in_pool(void *p) {
    return (uint8_t *)p >= psram_pool && (uint8_t *)p < psram_pool + POOL_BYTES;
}

static void pool_init(void) {
    critical_section_init(&pool_cs);
    free_list = (blk_t *)psram_pool;
    free_list->size = POOL_BYTES;
    free_list->next = NULL;
    pool_ready = true;
}

static void *pool_alloc(size_t n) {
    if (!pool_ready) pool_init();
    size_t need = (n + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    void *result = NULL;
    critical_section_enter_blocking(&pool_cs);
    for (blk_t **pp = &free_list; *pp; pp = &(*pp)->next) {
        blk_t *b = *pp;
        if (b->size < need) continue;
        if (b->size - need >= 2 * HDR) {
            blk_t *rest = (blk_t *)((uint8_t *)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            b->size = need;
            *pp = rest;
        } else {
            *pp = b->next;
        }
        psram_in_use += b->size;
        result = (uint8_t *)b + HDR;
        break;
    }
    critical_section_exit(&pool_cs);
    return result;
}

static void pool_free(void *p) {
    blk_t *b = (blk_t *)((uint8_t *)p - HDR);
    critical_section_enter_blocking(&pool_cs);
    psram_in_use -= b->size;
    blk_t *prev = NULL, *cur = free_list;
    while (cur && cur < b) {
        prev = cur;
        cur = cur->next;
    }
    b->next = cur;
    if (prev) prev->next = b; else free_list = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) {
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) {
        prev->size += b->size;
        prev->next = b->next;
    }
    critical_section_exit(&pool_cs);
}

void *malloc_caps(uint32_t size, uint32_t flags) {
    (void)flags;
    void *p = malloc(size);
    if (p == NULL) p = pool_alloc(size);
    if (p == NULL) alloc_failures++;
    // Survives a watchdog reboot, for post-mortems (see amy_rp2_crumbs()).
    watchdog_hw->scratch[1] = psram_in_use;
    watchdog_hw->scratch[2] = alloc_failures;
    return p;
}

void *malloc_caps_block(uint32_t size, uint32_t flags) {
    return malloc_caps(size, flags);
}

void amy_rp2_free(void *p) {
    if (p == NULL) return;
    if (in_pool(p)) {
        pool_free(p);
    } else {
        free(p);
    }
}

void amy_rp2_mem_stats(uint32_t *sram_used, uint32_t *sram_free, uint32_t *psram_used,
                       uint32_t *psram_size, uint32_t *failures) {
    struct mallinfo mi = mallinfo();
    extern char end, __StackLimit;  // newlib heap: end .. __StackLimit
    *sram_used = mi.uordblks;
    *sram_free = (uint32_t)(&__StackLimit - &end) - mi.uordblks;
    *psram_used = psram_in_use;
    *psram_size = POOL_BYTES;
    *failures = alloc_failures;
}
