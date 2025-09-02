SPI NOR flash model (VHDL, behavioral) with a self‑checking testbench (GHDL).

Overview
- Mode: SPI mode 0 (CPOL=0, CPHA=0).
- Geometry: Parameterizable total size, page size, and sector size via generics.
- Status bits: `WEL` (write enable latch) and `WIP` (write in progress).
- Busy timing: Separate `clk` drives programmable busy cycle delays for program/erase.

Supported Commands
- 0x06 `WREN`: Sets WEL when not busy.
- 0x05 `RDSR`: Streams status; bit0=WIP, bit1=WEL.
- 0x03 `READ`: 24‑bit address then streams data bytes.
- 0x02 `PP`: Page Program. 24‑bit address then data bytes.
  - 1→0 transitions only (bitwise AND into array)
  - Stops at page boundary (no wrap into next page)
  - On CS rising: enters busy for `PROG_BUSY_CYCLES` and clears WEL
- 0x20 `SE`: Sector Erase. 24‑bit address (sector‑aligned) then CS rising triggers erase.
  - Erases sector to 0xFF
  - Enters busy for `ERASE_BUSY_CYCLES` and clears WEL

Timing Details
- Samples MOSI on SCLK rising edge; updates MISO on SCLK falling edge (mode 0).
- Requires a small CS‑high gap between transactions in the testbench (mirrors real‑world usage and avoids delta‑cycle races in simulation).

Recent HDL Changes
- Robust CS/SCLK edge detection using process‑local variables (helps with back‑to‑back transactions).
- Deterministic READ startup: first data byte is valid on the very first SCLK after address phase.
- Added lightweight report notes for command decode and READ/ADDR tracing to aid debugging.

Testbench Coverage (tb_spi_flash.vhd)
- RDSR after reset: `WIP=0`, `WEL=0`.
- `WREN`/`RDSR`: `WEL` sets only when not busy.
- Page Program (PP): Writes 4 bytes and verifies via READ.
- PP without `WREN`: No change to memory; `WIP` stays low; `WEL` not latched.
- Page boundary on PP: Start at `0x0000FE` and verify only in‑page bytes are programmed.
- Re‑program AND semantics: Second PP ANDs into existing contents (1→0 only).
- Sector Erase (SE): Erases sector to `0xFF` and clears `WEL`.
- SE without `WREN`: No effect; no busy.

How To Run
Option A (batch script)
- From repo root: `hdl\run_ghdl.bat`
- Generates `hdl\waves.vcd`.

Option B (manual, PowerShell)
1) Ensure GHDL is on PATH (or edit the batch to point to it).
2) From repo root:
   ghdl -a --std=08 hdl/src/spi_flash.vhd
   ghdl -a --std=08 hdl/tb/tb_spi_flash.vhd
   ghdl -e --std=08 tb_spi_flash
   ghdl -r --std=08 tb_spi_flash --vcd=hdl/waves.vcd

Viewing Waves (optional)
- `gtkwave hdl\waves.vcd`

Known Limitations / Notes
- Only the listed commands are modeled; no protection registers or fast‑read variants.
- Model focuses on functional behavior and simple timing; not cycle‑accurate to any specific vendor device.
- Debug `report` statements are enabled for command and READ tracing; they can be reduced if the log is too chatty.
