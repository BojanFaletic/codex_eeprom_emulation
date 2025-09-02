#ifndef FLASH_LL_H
#define FLASH_LL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlashLlConfig {
    uintptr_t base_addr; // for MMIO in real HW (unused in sim)
    uint32_t mem_size;   // total flash size in bytes
    uint32_t page_size;
    uint32_t sector_size;
} FlashLlConfig;

typedef struct FlashLlIo {
    uint32_t (*read)(void *io, uint32_t offset);
    void     (*write)(void *io, uint32_t offset, uint32_t value);
    void     (*tick)(void *io, uint32_t ticks); // optional; can be NULL on real HW
} FlashLlIo;

typedef struct FlashLlCtx {
    FlashLlConfig cfg;
    const FlashLlIo *io_ops;
    void *io; // opaque pointer to IO backend (AXI sim or HAL)
} FlashLlCtx;

typedef enum FlashLlErr {
    FLASH_LL_OK = 0,
    FLASH_LL_EINVAL = -1,
    FLASH_LL_EIO = -2,
    FLASH_LL_EBUSY = -3,
    FLASH_LL_ETIME = -4,
    FLASH_LL_EOOB = -5,
} FlashLlErr;

int flash_ll_init(FlashLlCtx *ctx, const FlashLlConfig *cfg, const FlashLlIo *ops, void *io_backend);
int flash_ll_read(FlashLlCtx *ctx, uint32_t addr, void *buf, size_t len);
int flash_ll_program(FlashLlCtx *ctx, uint32_t addr, const void *data, size_t len);
int flash_ll_sector_erase(FlashLlCtx *ctx, uint32_t addr);
int flash_ll_rdsr(FlashLlCtx *ctx, uint8_t *status_out);
int flash_ll_wren(FlashLlCtx *ctx);
int flash_ll_wait_busy(FlashLlCtx *ctx, uint32_t max_ticks);

#ifdef __cplusplus
}
#endif

#endif // FLASH_LL_H
