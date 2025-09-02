#include "axi_spi_sim.h"
#include <stdlib.h>
#include <string.h>

static int fifo_init(ByteFifo *f, size_t cap) {
    f->buf = (uint8_t*)malloc(cap);
    if (!f->buf) return -1;
    f->cap = cap; f->r = f->w = f->count = 0; return 0;
}

static void fifo_free(ByteFifo *f) {
    if (f && f->buf) { free(f->buf); f->buf = NULL; }
    f->cap = f->r = f->w = f->count = 0;
}

static int fifo_push(ByteFifo *f, uint8_t v) {
    if (f->count == f->cap) return -1;
    f->buf[f->w++] = v; if (f->w == f->cap) f->w = 0; f->count++; return 0;
}

static int fifo_pop(ByteFifo *f, uint8_t *out) {
    if (f->count == 0) return -1;
    *out = f->buf[f->r++]; if (f->r == f->cap) f->r = 0; f->count--; return 0;
}

static void update_status(AxiSpiSim *s) {
    // bit0=BUSY, bit1=RX_AVAIL, bit2=TX_SPACE
    if (s->flash->busy_ticks > 0) s->flash->status |= FLASH_SIM_STATUS_WIP;
    s->status = 0;
    if ((s->flash->status & FLASH_SIM_STATUS_WIP) != 0) s->status |= 1u << 0;
    if (s->rx.count > 0) s->status |= 1u << 1;
    if (s->tx.count < s->tx.cap) s->status |= 1u << 2;
}

int axi_spi_sim_init(AxiSpiSim *s, FlashSim *flash, size_t fifo_cap) {
    if (!s || !flash) return -1;
    memset(s, 0, sizeof(*s));
    s->flash = flash;
    if (fifo_init(&s->tx, fifo_cap) != 0) return -2;
    if (fifo_init(&s->rx, fifo_cap) != 0) { fifo_free(&s->tx); return -3; }
    update_status(s);
    return 0;
}

void axi_spi_sim_free(AxiSpiSim *s) {
    if (!s) return;
    fifo_free(&s->tx);
    fifo_free(&s->rx);
}

static void do_start(AxiSpiSim *s) {
    // handle based on s->cmd
    uint8_t cmd = s->cmd;
    if (cmd == SPI_CMD_READ) {
        // READ: fill RX with LEN bytes from addr
        uint8_t tmp[256];
        size_t remain = s->len;
        uint32_t a = s->addr & 0xFFFFFFu;
        while (remain > 0) {
            size_t chunk = remain > sizeof(tmp) ? sizeof(tmp) : remain;
            size_t got = flash_sim_read(s->flash, a, tmp, chunk);
            for (size_t i = 0; i < got; ++i) {
                if (fifo_push(&s->rx, tmp[i]) != 0) break; // stop if RX is full
            }
            a += (uint32_t)got;
            if (got == 0) break;
            remain -= got;
            if (s->rx.count == s->rx.cap) break; // RX full
        }
    } else if (cmd == SPI_CMD_RDSR) {
        // Stream LEN status bytes
        for (uint32_t i = 0; i < s->len && s->rx.count < s->rx.cap; ++i) {
            fifo_push(&s->rx, flash_sim_rdsr(s->flash));
        }
    } else if (cmd == SPI_CMD_WREN) {
        flash_sim_wren(s->flash);
    } else if (cmd == SPI_CMD_PP) {
        // consume up to LEN bytes from TX and program
        uint8_t buf[256];
        size_t n = 0;
        while (n < sizeof(buf) && n < s->len) {
            uint8_t b;
            if (fifo_pop(&s->tx, &b) != 0) break;
            buf[n++] = b;
        }
        (void)flash_sim_page_program(s->flash, s->addr & 0xFFFFFFu, buf, n);
    } else if (cmd == SPI_CMD_SE) {
        (void)flash_sim_sector_erase(s->flash, s->addr & 0xFFFFFFu);
    }
    // Clear START bit
    s->ctrl &= ~((uint32_t)1u << 1);
    update_status(s);
}

void axi_spi_write(AxiSpiSim *s, uint32_t offset, uint32_t value) {
    switch (offset) {
        case REG_SPI_CMD:    s->cmd  = (uint8_t)(value & 0xFFu); break;
        case REG_SPI_ADDR:   s->addr = value & 0xFFFFFFu; break;
        case REG_SPI_LEN:    s->len  = value; break;
        case REG_SPI_DIN: {
            uint8_t b = (uint8_t)(value & 0xFFu);
            (void)fifo_push(&s->tx, b);
            break;
        }
        case REG_SPI_CTRL:
            s->ctrl = value;
            if (s->ctrl & (1u << 1)) { // START
                do_start(s);
            }
            break;
        default:
            break;
    }
    update_status(s);
}

uint32_t axi_spi_read(AxiSpiSim *s, uint32_t offset) {
    switch (offset) {
        case REG_SPI_CMD:    return s->cmd;
        case REG_SPI_ADDR:   return s->addr;
        case REG_SPI_LEN:    return s->len;
        case REG_SPI_DOUT: {
            uint8_t b = 0;
            (void)fifo_pop(&s->rx, &b);
            update_status(s);
            return b;
        }
        case REG_SPI_CTRL:   return s->ctrl;
        case REG_SPI_STATUS: return s->status;
        default: return 0;
    }
}

void axi_spi_tick(AxiSpiSim *s, uint32_t ticks) {
    flash_sim_tick(s->flash, ticks);
    update_status(s);
}

