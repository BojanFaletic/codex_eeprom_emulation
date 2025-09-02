#include "flash_sim.h"
#include <stdlib.h>
#include <string.h>

static uint32_t clamp_to_page(const FlashSim *sim, uint32_t addr, size_t len) {
    uint32_t page_off = addr % (uint32_t)sim->cfg.page_size;
    size_t in_page = sim->cfg.page_size - page_off;
    return (uint32_t)((len < in_page) ? len : in_page);
}

int flash_sim_init(FlashSim *sim, const FlashSimConfig *cfg) {
    if (!sim || !cfg || cfg->mem_bytes == 0 || cfg->page_size == 0 || cfg->sector_size == 0) {
        return -1;
    }
    sim->cfg = *cfg;
    sim->mem = (uint8_t*)malloc(cfg->mem_bytes);
    if (!sim->mem) return -2;
    memset(sim->mem, 0xFF, cfg->mem_bytes);
    sim->status = 0; // WIP=0, WEL=0
    sim->busy_ticks = 0;
    return 0;
}

void flash_sim_free(FlashSim *sim) {
    if (sim && sim->mem) {
        free(sim->mem);
        sim->mem = NULL;
    }
}

void flash_sim_tick(FlashSim *sim, uint32_t ticks) {
    if (!sim) return;
    if (sim->busy_ticks > 0) {
        if (ticks >= sim->busy_ticks) {
            sim->busy_ticks = 0;
            sim->status &= (uint8_t)~FLASH_SIM_STATUS_WIP; // clear WIP
        } else {
            sim->busy_ticks -= ticks;
        }
    }
}

void flash_sim_wren(FlashSim *sim) {
    if (!sim) return;
    sim->status |= FLASH_SIM_STATUS_WEL;
}

uint8_t flash_sim_rdsr(const FlashSim *sim) {
    return sim ? sim->status : 0; 
}

size_t flash_sim_read(const FlashSim *sim, uint32_t addr, uint8_t *out, size_t len) {
    if (!sim || !out || len == 0) return 0;
    if (addr >= sim->cfg.mem_bytes) return 0;
    size_t max = sim->cfg.mem_bytes - addr;
    if (len > max) len = max;
    memcpy(out, sim->mem + addr, len);
    return len;
}

size_t flash_sim_page_program(FlashSim *sim, uint32_t addr, const uint8_t *data, size_t len) {
    if (!sim || !data || len == 0) return 0;
    if (sim->status & FLASH_SIM_STATUS_WIP) return 0; // busy
    if ((sim->status & FLASH_SIM_STATUS_WEL) == 0) return 0; // not enabled
    if (addr >= sim->cfg.mem_bytes) return 0;

    uint32_t max_in_page = clamp_to_page(sim, addr, len);
    size_t max_bytes = sim->cfg.mem_bytes - addr;
    size_t n = max_in_page < max_bytes ? max_in_page : max_bytes;
    for (size_t i = 0; i < n; ++i) {
        sim->mem[addr + i] &= data[i]; // 1->0 only
    }
    // start busy, clear WEL
    sim->status |= FLASH_SIM_STATUS_WIP;
    sim->status &= (uint8_t)~FLASH_SIM_STATUS_WEL;
    sim->busy_ticks = sim->cfg.prog_busy_ticks;
    return n;
}

int flash_sim_sector_erase(FlashSim *sim, uint32_t addr) {
    if (!sim) return -1;
    if (sim->status & FLASH_SIM_STATUS_WIP) return -2; // busy
    if ((sim->status & FLASH_SIM_STATUS_WEL) == 0) return -3; // not enabled
    if (addr >= sim->cfg.mem_bytes) return -4;
    uint32_t base = (addr / (uint32_t)sim->cfg.sector_size) * (uint32_t)sim->cfg.sector_size;
    size_t n = sim->cfg.sector_size;
    if (base + n > sim->cfg.mem_bytes) n = sim->cfg.mem_bytes - base;
    memset(sim->mem + base, 0xFF, n);
    sim->status |= FLASH_SIM_STATUS_WIP;
    sim->status &= (uint8_t)~FLASH_SIM_STATUS_WEL;
    sim->busy_ticks = sim->cfg.erase_busy_ticks;
    return 0;
}

