Project Overview

- HDL: Behavioral SPI NOR flash model in VHDL with a self‑checking GHDL testbench. See `hdl/README.md` for commands and coverage.
- C: Low‑level SPI NOR flash driver + pure‑C simulator (AXI‑Lite style register block + flash model). Driver and model have unit tests. See `sw/flash_ll/README.md`.
- Rust: Workspace with FFI bindings (`flash_ll_sys`), a safe wrapper (`flash_ll`), and an EEPROM emulation layer (`eeprom_emul`) validated against the C simulator.

Repository Layout
- `hdl/`: VHDL model and testbench; `run_ghdl.bat` to simulate.
- `sw/flash_ll/`: C driver and simulator; CMake build + tests in `sim/sim_main.c`.
- `sw/rust/flash_core/`: Rust `Flash` trait (no C deps)
- `sw/rust/flash_mock/`: Pure‑Rust mock NOR implementing `Flash`
- `sw/rust/flash_ll_sys/`: Rust FFI bindings (bindgen + cc) to the C driver and simulator.
- `sw/rust/flash_ll/`: Safe Rust wrapper over the C driver, with sim‑backed tests.
- `sw/rust/eeprom_emul/`: EEPROM emulation generic over `Flash` (mock by default, C driver optional).
 - `app/`: Standalone CLI that uses a built-in mock EEPROM to store settings and a boot counter.

Build Instructions
- HDL: `hdl\run_ghdl.bat` (Windows). Generates `hdl\waves.vcd`.
- C sim/tests (Unix):
  - `cmake -S sw/flash_ll -B sw/flash_ll/build`
  - `cmake --build sw/flash_ll/build --config Release`
  - `./sw/flash_ll/build/sim/sim_flash_ll`
- C sim/tests (Windows):
  - Open a VS Developer Prompt
  - `cmake -S sw/flash_ll -B sw/flash_ll/build -G "Visual Studio 17 2022"`
  - `cmake --build sw/flash_ll/build --config Release`
  - `sw\flash_ll\build\sim\Release\sim_flash_ll.exe`
- Rust (workspace):
  - Prereqs: C toolchain + LLVM clang (set `LIBCLANG_PATH` on Windows)
  - `cargo test -p flash_core`
  - `cargo test -p flash_mock`
  - `cargo test -p flash_ll_sys`
  - `cargo test -p flash_ll`
  - `cargo test -p eeprom_emul`

Cargo Aliases
- `cargo eeprom-mock`: runs EEPROM tests with the pure‑Rust mock backend (no C/LLVM).
- `cargo eeprom-ffi`: runs EEPROM tests using the C‑backed driver (requires LLVM/bindgen).
- `cargo ffi-all`: builds/tests the C FFI layer (`flash_ll_sys` + `flash_ll`).
 - `cargo eeprom-demo -- <cmd>`: runs the standalone app in `app/`.

Notes
- The C driver supports: 0x06 WREN, 0x05 RDSR, 0x03 READ, 0x02 PP (page‑chunked), 0x20 SE, with busy polling and bounds checks.
- The Rust EEPROM emulation uses a two‑sector log with compaction and CRC‑guarded records; it is generic over a `Flash` backend. Use the pure‑Rust mock (`--features mock`) to avoid C/LLVM.
- Default workspace members target pure‑Rust crates for fast builds. Use `-p` or the aliases to build other crates on demand.
