C Simulation for Flash Low‑Level Driver

Purpose
- Provide a pure‑C simulation of an AXI‑Lite SPI engine and a SPI NOR flash so the low‑level driver can be validated without HDL.

Components
- `axi_spi_sim.c/.h`: register block, START/BUSY, DIN/DOUT FIFOs, feeds transactions to flash model.
- `flash_sim.c/.h`: NOR flash model (WEL/WIP, 1→0 program, sector erase, page boundary handling).
- `sim_main.c`: test runner with eight scenarios mirroring HDL.

Build/Run
- `cmake -S .. -B ../build && cmake --build ../build --config Release`
- Run: `../build/sim/sim_flash_ll` (Unix) or `..\build\sim\Release\sim_flash_ll.exe` (Windows)

Notes
- The register map and API match sw/flash_ll/README.md.
