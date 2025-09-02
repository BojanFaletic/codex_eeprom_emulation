#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "flash_sim.h"
#include "axi_spi_sim.h"
#include "sim_test.h"
#include "flash_ll.h"
#include "flash_ll_io_sim.h"

static void write_bytes(AxiSpiSim *s, const uint8_t *data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        axi_spi_write(s, REG_SPI_DIN, data[i]);
    }
}

static void read_bytes(AxiSpiSim *s, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = (uint8_t)axi_spi_read(s, REG_SPI_DOUT);
    }
}

static void issue_cmd(AxiSpiSim *s, uint8_t cmd, uint32_t addr, uint32_t len) {
    axi_spi_write(s, REG_SPI_CMD, cmd);
    axi_spi_write(s, REG_SPI_ADDR, addr);
    axi_spi_write(s, REG_SPI_LEN, len);
    axi_spi_write(s, REG_SPI_CTRL, 0x3); // CS_EN=1, START=1
}

// Shared setup for tests
static void setup(FlashSim *flash, AxiSpiSim *spi) {
    FlashSimConfig cfg = {
        .mem_bytes = 4096,
        .page_size = 256,
        .sector_size = 4096,
        .prog_busy_ticks = 4,
        .erase_busy_ticks = 64,
    };
    int r = flash_sim_init(flash, &cfg);
    ASSERT_EQ_U32(r, 0);
    r = axi_spi_sim_init(spi, flash, 1024);
    ASSERT_EQ_U32(r, 0);
}

// Test 1: RDSR after reset
TEST_CASE(test_rdsr_after_reset) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    issue_cmd(&s, SPI_CMD_RDSR, 0, 4);
    uint8_t st[4] = {0};
    read_bytes(&s, st, 4);
    ASSERT_EQ_U8(st[0] & 0x1, 0); // WIP=0
    ASSERT_EQ_U8((st[0] >> 1) & 1, 0); // WEL=0
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 2: WREN sets WEL
TEST_CASE(test_wren_sets_wel) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    issue_cmd(&s, SPI_CMD_RDSR, 0, 1);
    uint8_t st = (uint8_t)axi_spi_read(&s, REG_SPI_DOUT);
    ASSERT_EQ_U8((st >> 1) & 1, 1);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 3: PP 4 bytes and READ back
TEST_CASE(test_pp_and_readback) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0x10;
    const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    write_bytes(&s, data, 4);
    issue_cmd(&s, SPI_CMD_PP, addr, 4);
    // busy, tick until done
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // read back
    issue_cmd(&s, SPI_CMD_READ, addr, 4);
    uint8_t out[4] = {0};
    read_bytes(&s, out, 4);
    ASSERT_MEMEQ(out, data, 4);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 4: PP without WREN → no change
TEST_CASE(test_pp_without_wren) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0x20;
    const uint8_t data[2] = {0x12, 0x34};
    // capture before
    issue_cmd(&s, SPI_CMD_READ, addr, 2);
    uint8_t before[2]; read_bytes(&s, before, 2);
    write_bytes(&s, data, 2);
    issue_cmd(&s, SPI_CMD_PP, addr, 2);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // read after
    issue_cmd(&s, SPI_CMD_READ, addr, 2);
    uint8_t after[2]; read_bytes(&s, after, 2);
    ASSERT_MEMEQ(before, after, 2);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 5: PP across page boundary @0xFE (page=256)
TEST_CASE(test_pp_page_boundary) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0xFE; // last two bytes of page
    const uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    write_bytes(&s, data, 4);
    issue_cmd(&s, SPI_CMD_PP, addr, 4);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // read 4 bytes across boundary; only first 2 programmed, next 2 remain 0xFF
    issue_cmd(&s, SPI_CMD_READ, addr, 4);
    uint8_t out[4]; read_bytes(&s, out, 4);
    ASSERT_EQ_U8(out[0], 0xAA);
    ASSERT_EQ_U8(out[1], 0xBB);
    ASSERT_EQ_U8(out[2], 0xFF);
    ASSERT_EQ_U8(out[3], 0xFF);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 6: Re-program AND semantics
TEST_CASE(test_reprogram_and) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0x100;
    // First program 0xAA
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    uint8_t a = 0xAA; write_bytes(&s, &a, 1);
    issue_cmd(&s, SPI_CMD_PP, addr, 1);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // Now attempt to program 0x55 at same address
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    uint8_t b = 0x55; write_bytes(&s, &b, 1);
    issue_cmd(&s, SPI_CMD_PP, addr, 1);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // Read back should be 0x00 (0xAA & 0x55)
    issue_cmd(&s, SPI_CMD_READ, addr, 1);
    uint8_t out = (uint8_t)axi_spi_read(&s, REG_SPI_DOUT);
    ASSERT_EQ_U8(out, 0x00);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 7: SE with WREN
TEST_CASE(test_sector_erase) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0x200;
    // program some data first
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    uint8_t dat[3] = {0x00, 0x11, 0x22}; write_bytes(&s, dat, 3);
    issue_cmd(&s, SPI_CMD_PP, addr, 3);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // erase sector
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    issue_cmd(&s, SPI_CMD_SE, addr, 0);
    for (int i = 0; i < 100; ++i) axi_spi_tick(&s, 1);
    // read back programmed region
    issue_cmd(&s, SPI_CMD_READ, addr, 3);
    uint8_t out[3]; read_bytes(&s, out, 3);
    ASSERT_EQ_U8(out[0], 0xFF); ASSERT_EQ_U8(out[1], 0xFF); ASSERT_EQ_U8(out[2], 0xFF);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Test 8: SE without WREN → no change
TEST_CASE(test_erase_without_wren) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    const uint32_t addr = 0x300;
    // program a byte first
    issue_cmd(&s, SPI_CMD_WREN, 0, 0);
    uint8_t dat = 0x00; write_bytes(&s, &dat, 1);
    issue_cmd(&s, SPI_CMD_PP, addr, 1);
    for (int i = 0; i < 10; ++i) axi_spi_tick(&s, 1);
    // attempt erase without WREN
    issue_cmd(&s, SPI_CMD_SE, addr, 0);
    for (int i = 0; i < 100; ++i) axi_spi_tick(&s, 1);
    // read back: should still be 0x00
    issue_cmd(&s, SPI_CMD_READ, addr, 1);
    uint8_t out = (uint8_t)axi_spi_read(&s, REG_SPI_DOUT);
    ASSERT_EQ_U8(out, 0x00);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

// Driver-based tests (top-level)
TEST_CASE(drv_rdsr_after_reset) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    FlashLlCtx ctx; FlashLlConfig cfg = {0};
    cfg.mem_size = 4096; cfg.page_size = 256; cfg.sector_size = 4096;
    ASSERT_EQ_U32(flash_ll_init(&ctx, &cfg, flash_ll_axi_sim_ops(), &s), 0);
    uint8_t st = 0xFF; flash_ll_rdsr(&ctx, &st);
    ASSERT_EQ_U8(st & 1, 0); ASSERT_EQ_U8((st>>1)&1, 0);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

TEST_CASE(drv_pp_and_readback) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    FlashLlCtx ctx; FlashLlConfig cfg = {0};
    cfg.mem_size = 4096; cfg.page_size = 256; cfg.sector_size = 4096;
    ASSERT_EQ_U32(flash_ll_init(&ctx, &cfg, flash_ll_axi_sim_ops(), &s), 0);
    const uint32_t addr = 0x10;
    const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_EQ_U32(flash_ll_program(&ctx, addr, data, sizeof data), 0);
    uint8_t out[4] = {0};
    ASSERT_EQ_U32(flash_ll_read(&ctx, addr, out, sizeof out), 0);
    ASSERT_MEMEQ(out, data, 4);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

TEST_CASE(drv_pp_without_wren_is_handled) {
    // Driver always sends WREN; this test ensures AND semantics occur.
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    FlashLlCtx ctx; FlashLlConfig cfg = {0};
    cfg.mem_size = 4096; cfg.page_size = 256; cfg.sector_size = 4096;
    ASSERT_EQ_U32(flash_ll_init(&ctx, &cfg, flash_ll_axi_sim_ops(), &s), 0);
    uint32_t addr = 0x100;
    uint8_t a = 0xAA; uint8_t b = 0x55; uint8_t out = 0xFF;
    ASSERT_EQ_U32(flash_ll_program(&ctx, addr, &a, 1), 0);
    ASSERT_EQ_U32(flash_ll_program(&ctx, addr, &b, 1), 0);
    ASSERT_EQ_U32(flash_ll_read(&ctx, addr, &out, 1), 0);
    ASSERT_EQ_U8(out, 0x00);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

TEST_CASE(drv_page_boundary_respected) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    FlashLlCtx ctx; FlashLlConfig cfg = {0};
    cfg.mem_size = 4096; cfg.page_size = 256; cfg.sector_size = 4096;
    ASSERT_EQ_U32(flash_ll_init(&ctx, &cfg, flash_ll_axi_sim_ops(), &s), 0);
    uint32_t addr = 0xFE; uint8_t d[4] = {0xAA,0xBB,0xCC,0xDD}; uint8_t out[4]={0};
    ASSERT_EQ_U32(flash_ll_program(&ctx, addr, d, 4), 0);
    ASSERT_EQ_U32(flash_ll_read(&ctx, addr, out, 4), 0);
    // Driver handles multi-page chunking, so all 4 bytes should be programmed
    ASSERT_EQ_U8(out[0],0xAA); ASSERT_EQ_U8(out[1],0xBB); ASSERT_EQ_U8(out[2],0xCC); ASSERT_EQ_U8(out[3],0xDD);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

TEST_CASE(drv_sector_erase) {
    FlashSim f; AxiSpiSim s; setup(&f, &s);
    FlashLlCtx ctx; FlashLlConfig cfg = {0};
    cfg.mem_size = 4096; cfg.page_size = 256; cfg.sector_size = 4096;
    ASSERT_EQ_U32(flash_ll_init(&ctx, &cfg, flash_ll_axi_sim_ops(), &s), 0);
    uint32_t addr = 0x200; uint8_t dat[3] = {0x00, 0x11, 0x22}; uint8_t out[3] = {0};
    ASSERT_EQ_U32(flash_ll_program(&ctx, addr, dat, 3), 0);
    ASSERT_EQ_U32(flash_ll_sector_erase(&ctx, addr), 0);
    ASSERT_EQ_U32(flash_ll_read(&ctx, addr, out, 3), 0);
    ASSERT_EQ_U8(out[0],0xFF); ASSERT_EQ_U8(out[1],0xFF); ASSERT_EQ_U8(out[2],0xFF);
    axi_spi_sim_free(&s); flash_sim_free(&f);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    RUN_TEST(test_rdsr_after_reset);
    RUN_TEST(test_wren_sets_wel);
    RUN_TEST(test_pp_and_readback);
    RUN_TEST(test_pp_without_wren);
    RUN_TEST(test_pp_page_boundary);
    RUN_TEST(test_reprogram_and);
    RUN_TEST(test_sector_erase);
    RUN_TEST(test_erase_without_wren);
    RUN_TEST(drv_rdsr_after_reset);
    RUN_TEST(drv_pp_and_readback);
    RUN_TEST(drv_pp_without_wren_is_handled);
    RUN_TEST(drv_page_boundary_respected);
    RUN_TEST(drv_sector_erase);

    if (sim_test_failures) {
        fprintf(stderr, "\nTOTAL FAILURES: %d\n", sim_test_failures);
        return 1;
    }
    fprintf(stdout, "\nAll tests passed.\n");
    return 0;
}
