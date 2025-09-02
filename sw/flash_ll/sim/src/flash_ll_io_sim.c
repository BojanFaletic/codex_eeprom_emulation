#include "flash_ll_io_sim.h"

static uint32_t sim_read(void *io, uint32_t offset) {
    return axi_spi_read((AxiSpiSim*)io, offset);
}

static void sim_write(void *io, uint32_t offset, uint32_t value) {
    axi_spi_write((AxiSpiSim*)io, offset, value);
}

static void sim_tick(void *io, uint32_t ticks) {
    axi_spi_tick((AxiSpiSim*)io, ticks);
}

static const FlashLlIo kSimOps = {
    .read = sim_read,
    .write = sim_write,
    .tick = sim_tick,
};

const FlashLlIo* flash_ll_axi_sim_ops(void) { return &kSimOps; }
