// Common register map and command constants for the AXI-Lite SPI engine
#ifndef FLASH_LL_REGS_H
#define FLASH_LL_REGS_H

#include <stdint.h>

enum {
    FLASH_LL_REG_SPI_CMD    = 0x00,
    FLASH_LL_REG_SPI_ADDR   = 0x04,
    FLASH_LL_REG_SPI_LEN    = 0x08,
    FLASH_LL_REG_SPI_DIN    = 0x0C,
    FLASH_LL_REG_SPI_DOUT   = 0x10,
    FLASH_LL_REG_SPI_CTRL   = 0x14, // bit0=CS_EN, bit1=START
    FLASH_LL_REG_SPI_STATUS = 0x18, // bit0=BUSY, bit1=RX_AVAIL, bit2=TX_SPACE
};

enum {
    FLASH_LL_CMD_WREN = 0x06,
    FLASH_LL_CMD_RDSR = 0x05,
    FLASH_LL_CMD_READ = 0x03,
    FLASH_LL_CMD_PP   = 0x02,
    FLASH_LL_CMD_SE   = 0x20,
};

#endif // FLASH_LL_REGS_H

