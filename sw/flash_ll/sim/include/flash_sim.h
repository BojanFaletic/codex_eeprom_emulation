// Simple SPI NOR flash simulation model (pure C)
#ifndef FLASH_SIM_H
#define FLASH_SIM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { FLASH_SIM_STATUS_WIP = 1u << 0, FLASH_SIM_STATUS_WEL = 1u << 1 };

typedef struct FlashSimConfig {
    size_t mem_bytes;       // total bytes in flash
    size_t page_size;       // page size for program operations
    size_t sector_size;     // sector erase granularity
    uint32_t prog_busy_ticks;   // simulated busy ticks for page program
    uint32_t erase_busy_ticks;  // simulated busy ticks for sector erase
} FlashSimConfig;

typedef struct FlashSim {
    FlashSimConfig cfg;
    uint8_t *mem;           // memory array of size cfg.mem_bytes
    uint8_t status;         // bit0=WIP, bit1=WEL
    uint32_t busy_ticks;    // remaining busy ticks
} FlashSim;

int flash_sim_init(FlashSim *sim, const FlashSimConfig *cfg);
void flash_sim_free(FlashSim *sim);

// time advance; decreases busy ticks and clears WIP when finished
void flash_sim_tick(FlashSim *sim, uint32_t ticks);

// Commands
void flash_sim_wren(FlashSim *sim);
uint8_t flash_sim_rdsr(const FlashSim *sim);
// READ bytes into out; returns number of bytes read
size_t flash_sim_read(const FlashSim *sim, uint32_t addr, uint8_t *out, size_t len);
// Page Program: programs up to page boundary; returns bytes actually programmed
// Requires WEL set and not busy; sets WIP and clears WEL; applies 1->0 AND semantics
size_t flash_sim_page_program(FlashSim *sim, uint32_t addr, const uint8_t *data, size_t len);
// Sector Erase: requires WEL and not busy; sets to 0xFF across sector
int flash_sim_sector_erase(FlashSim *sim, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif // FLASH_SIM_H

