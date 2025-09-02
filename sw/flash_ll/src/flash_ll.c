#include "flash_ll.h"
#include "flash_ll_regs.h"

#include <string.h>

#define BIT(x) (1u << (x))

static inline uint32_t rd(FlashLlCtx *c, uint32_t off){ return c->io_ops->read(c->io, off);} 
static inline void wr(FlashLlCtx *c, uint32_t off, uint32_t v){ c->io_ops->write(c->io, off, v);} 
static inline void tk(FlashLlCtx *c, uint32_t t){ if (c->io_ops->tick) c->io_ops->tick(c->io, t);} 

int flash_ll_init(FlashLlCtx *ctx, const FlashLlConfig *cfg, const FlashLlIo *ops, void *io_backend) {
    if (!ctx || !cfg || !ops) return FLASH_LL_EINVAL;
    if (cfg->page_size == 0 || cfg->sector_size == 0 || cfg->mem_size == 0) return FLASH_LL_EINVAL;
    ctx->cfg = *cfg;
    ctx->io_ops = ops;
    ctx->io = io_backend;
    return FLASH_LL_OK;
}

static int start_cmd(FlashLlCtx *ctx, uint8_t cmd, uint32_t addr, uint32_t len) {
    wr(ctx, FLASH_LL_REG_SPI_CMD, cmd);
    wr(ctx, FLASH_LL_REG_SPI_ADDR, addr & 0xFFFFFFu);
    wr(ctx, FLASH_LL_REG_SPI_LEN, len);
    wr(ctx, FLASH_LL_REG_SPI_CTRL, BIT(0) | BIT(1)); // CS_EN | START
    return FLASH_LL_OK;
}

static uint8_t rdsr_once(FlashLlCtx *ctx) {
    start_cmd(ctx, FLASH_LL_CMD_RDSR, 0, 1);
    return (uint8_t)rd(ctx, FLASH_LL_REG_SPI_DOUT);
}

int flash_ll_wren(FlashLlCtx *ctx) {
    if (!ctx) return FLASH_LL_EINVAL;
    return start_cmd(ctx, FLASH_LL_CMD_WREN, 0, 0);
}

int flash_ll_rdsr(FlashLlCtx *ctx, uint8_t *status_out) {
    if (!ctx || !status_out) return FLASH_LL_EINVAL;
    *status_out = rdsr_once(ctx);
    return FLASH_LL_OK;
}

int flash_ll_wait_busy(FlashLlCtx *ctx, uint32_t max_ticks) {
    if (!ctx) return FLASH_LL_EINVAL;
    while (1) {
        uint8_t st = rdsr_once(ctx);
        if ((st & 0x1u) == 0) return FLASH_LL_OK; // WIP cleared
        if (max_ticks == 0) return FLASH_LL_ETIME;
        tk(ctx, 1);
        max_ticks--;
    }
}

static int check_oob(FlashLlCtx *ctx, uint32_t addr, uint32_t len) {
    if (addr >= ctx->cfg.mem_size) return FLASH_LL_EOOB;
    if (len > ctx->cfg.mem_size - addr) return FLASH_LL_EOOB;
    return FLASH_LL_OK;
}

int flash_ll_read(FlashLlCtx *ctx, uint32_t addr, void *buf, size_t len) {
    if (!ctx || !buf || len == 0) return FLASH_LL_EINVAL;
    int rc = check_oob(ctx, addr, (uint32_t)len);
    if (rc != FLASH_LL_OK) return rc;
    start_cmd(ctx, FLASH_LL_CMD_READ, addr, (uint32_t)len);
    uint8_t *out = (uint8_t*)buf;
    size_t read_cnt = 0;
    uint32_t budget = (uint32_t)(len * 8 + 1024); // generous budget
    while (read_cnt < len && budget--) {
        uint32_t st = rd(ctx, FLASH_LL_REG_SPI_STATUS);
        if (st & BIT(1)) { // RX_AVAIL
            out[read_cnt++] = (uint8_t)rd(ctx, FLASH_LL_REG_SPI_DOUT);
        } else {
            tk(ctx, 1);
        }
    }
    return (read_cnt == len) ? FLASH_LL_OK : FLASH_LL_EIO;
}

static size_t tx_write_all(FlashLlCtx *ctx, const uint8_t *data, size_t len) {
    size_t sent = 0;
    uint32_t budget = (uint32_t)(len * 8 + 1024);
    while (sent < len && budget--) {
        uint32_t st = rd(ctx, FLASH_LL_REG_SPI_STATUS);
        if (st & BIT(2)) { // TX_SPACE
            wr(ctx, FLASH_LL_REG_SPI_DIN, data[sent++]);
        } else {
            tk(ctx, 1);
        }
    }
    return sent;
}

int flash_ll_program(FlashLlCtx *ctx, uint32_t addr, const void *data, size_t len) {
    if (!ctx || !data || len == 0) return FLASH_LL_EINVAL;
    int rc = check_oob(ctx, addr, (uint32_t)len);
    if (rc != FLASH_LL_OK) return rc;
    const uint8_t *p = (const uint8_t*)data;
    size_t remaining = len;
    while (remaining > 0) {
        uint32_t page_off = addr % ctx->cfg.page_size;
        uint32_t room = ctx->cfg.page_size - page_off;
        uint32_t chunk = (remaining < room) ? (uint32_t)remaining : room;

        rc = flash_ll_wren(ctx);
        if (rc != FLASH_LL_OK) return rc;

        size_t sent = tx_write_all(ctx, p, chunk);
        if (sent != chunk) return FLASH_LL_EIO;

        start_cmd(ctx, FLASH_LL_CMD_PP, addr, chunk);
        rc = flash_ll_wait_busy(ctx, 100000);
        if (rc != FLASH_LL_OK) return rc;

        addr += chunk; p += chunk; remaining -= chunk;
    }
    return FLASH_LL_OK;
}

int flash_ll_sector_erase(FlashLlCtx *ctx, uint32_t addr) {
    if (!ctx) return FLASH_LL_EINVAL;
    // align to sector base inside
    if (addr >= ctx->cfg.mem_size) return FLASH_LL_EOOB;
    int rc = flash_ll_wren(ctx);
    if (rc != FLASH_LL_OK) return rc;
    start_cmd(ctx, FLASH_LL_CMD_SE, addr, 0);
    rc = flash_ll_wait_busy(ctx, 1000000);
    return rc;
}

