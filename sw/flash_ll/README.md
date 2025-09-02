Low‑Level SPI NOR Flash Driver (C)

Goal
- Provide a minimal, robust C driver that issues SPI NOR commands (WREN, RDSR, READ, PP, SE) via a memory‑mapped AXI‑Lite SPI interface, matching the HDL model’s semantics.
- Supply a pure‑C simulation of the AXI‑Lite register block and SPI flash to validate the driver logic without HDL.

Scope
- Command coverage: 0x06 WREN, 0x05 RDSR, 0x03 READ, 0x02 PP, 0x20 SE.
- Enforce: 1→0 program rule, page boundary stop, busy timing (logical), and WEL/WIP semantics.

Driver Shape (no code yet)
- Public API (proposed):
  - flash_ll_init(ctx, base_addr, geom)
  - flash_ll_read(ctx, addr, buf, len)
  - flash_ll_program(ctx, addr, const void* data, len)  // handles page chunking
  - flash_ll_sector_erase(ctx, addr)
  - flash_ll_rdsr(ctx, uint8_t* status)
  - flash_ll_wren(ctx)
  - flash_ll_wait_busy(ctx, timeout)
- Error model: simple int/enum return codes (OK, TIMEOUT, OOB, ALIGN, BAD_STATE).
- HAL abstraction for MMIO:
  - axi_read32(ctx, offset, uint32_t* out)
  - axi_write32(ctx, offset, uint32_t val)
  - Optionally a bulk SPI transfer helper if the SPI engine needs it.

AXI‑Lite SPI Engine (simulated)
- Registers (proposed):
  - SPI_CMD      (offset 0x00): command byte
  - SPI_ADDR     (0x04): 24‑bit address (packed)
  - SPI_LEN      (0x08): transfer length in bytes
  - SPI_DIN      (0x0C): write FIFO/data (host→flash)
  - SPI_DOUT     (0x10): read FIFO/data (flash→host)
  - SPI_CTRL     (0x14): bits: CS_EN, START
  - SPI_STATUS   (0x18): bits: BUSY, RX_AVAIL, TX_SPACE
  - SPI_RDSR_SH  (0x1C): for status streaming (optional, can reuse DOUT)
- Flow per op:
  - WRITE path (PP/SE/WREN): load CMD/ADDR/DIN, assert START, poll BUSY, then check WIP via RDSR.
  - READ path: load CMD/ADDR/LEN, START, then pull bytes from DOUT.

Simulation Plan (C only)
- Sim core:
  - AXI‑Lite model: array of regs + simple FIFOs for DIN/DOUT.
  - SPI flash model: byte array + WEL/WIP, same rules as HDL; hooked behind the AXI SPI engine model.
- Test scenarios (mirror HDL TB):
  1) RDSR after reset → WIP=0, WEL=0
  2) WREN, verify WEL=1
  3) PP 4 bytes @0x000010, poll WIP, READ verify
  4) PP without WREN → no change, WIP stays 0
  5) PP across page boundary @0x0000FE → only first in‑page bytes program
  6) Re‑program AND semantics
  7) SE with WREN → erase to 0xFF, poll WIP
  8) SE without WREN → no change

Build/Run (proposed)
- CMake or Meson; single `sim_flash_ll` executable with subcommand `run-all` to execute scenarios.
- Later: CI step to run the C simulation alongside the HDL TB for parity.

Next Steps
1) Lock API names and return codes.
2) Freeze register map for AXI‑Lite SPI engine.
3) Implement sim engine + flash model first; then driver; then tests. [In progress]
4) After sim/tests pass, add `flash_ll` driver implementation and integrate with the sim via the register interface.

Build (simulation only)
- Requires a C compiler and CMake (3.12+). Portable across MSVC/Clang/GCC.
- Commands:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
  - `build/sim/sim_flash_ll` (or `build\sim\Release\sim_flash_ll.exe` on Windows)

What’s implemented
- Pure‑C `FlashSim`: memory array + WEL/WIP semantics, page‑bounded program, sector erase, 1→0 rule.
- `AxiSpiSim`: simple AXI‑Lite register block fronting the flash model with FIFOs for DIN/DOUT.
- Tests: mirror HDL TB flows (RDSR, WREN, PP, page boundary, AND semantics, SE, negative cases).

Driver plan (next)
- Implement `flash_ll_*` over MMIO register IO and validate by reusing the same scenarios but routed through the driver API.
- Keep host‑side HAL minimal and portable for integration in embedded targets later.
