// AXI-Lite SPI Engine simulation that fronts the FlashSim
#ifndef AXI_SPI_SIM_H
#define AXI_SPI_SIM_H

#include <stdint.h>
#include <stddef.h>
#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register map offsets (bytes)
enum {
    REG_SPI_CMD    = 0x00, // command byte
    REG_SPI_ADDR   = 0x04, // 24-bit address packed in 32-bit
    REG_SPI_LEN    = 0x08, // transfer length in bytes
    REG_SPI_DIN    = 0x0C, // write data FIFO (LSB byte used)
    REG_SPI_DOUT   = 0x10, // read data FIFO (LSB byte valid)
    REG_SPI_CTRL   = 0x14, // bit0=CS_EN, bit1=START (write 1 to start)
    REG_SPI_STATUS = 0x18, // bit0=BUSY, bit1=RX_AVAIL, bit2=TX_SPACE
};

// SPI commands we support
enum {
    SPI_CMD_WREN = 0x06,
    SPI_CMD_RDSR = 0x05,
    SPI_CMD_READ = 0x03,
    SPI_CMD_PP   = 0x02,
    SPI_CMD_SE   = 0x20,
};

typedef struct ByteFifo {
    uint8_t *buf;
    size_t cap;
    size_t r;
    size_t w;
    size_t count;
} ByteFifo;

typedef struct AxiSpiSim {
    FlashSim *flash;
    // registers
    uint8_t  cmd;
    uint32_t addr;
    uint32_t len;
    uint32_t ctrl;
    uint32_t status; // bit0=BUSY, bit1=RX_AVAIL, bit2=TX_SPACE
    ByteFifo tx; // host -> flash (PP data)
    ByteFifo rx; // flash -> host (READ/RDSR data)
} AxiSpiSim;

int axi_spi_sim_init(AxiSpiSim *s, FlashSim *flash, size_t fifo_cap);
void axi_spi_sim_free(AxiSpiSim *s);

// register IO
void axi_spi_write(AxiSpiSim *s, uint32_t offset, uint32_t value);
uint32_t axi_spi_read(AxiSpiSim *s, uint32_t offset);

// advance time; pushes flash busy forward and drains BUSY state
void axi_spi_tick(AxiSpiSim *s, uint32_t ticks);

#ifdef __cplusplus
}
#endif

#endif // AXI_SPI_SIM_H

