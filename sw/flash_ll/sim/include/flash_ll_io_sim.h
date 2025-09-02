#ifndef FLASH_LL_IO_SIM_H
#define FLASH_LL_IO_SIM_H

#include "flash_ll.h"
#include "axi_spi_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

// Get ops vtable for AxiSpiSim and use with flash_ll_init
const FlashLlIo* flash_ll_axi_sim_ops(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_LL_IO_SIM_H
